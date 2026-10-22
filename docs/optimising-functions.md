# Optimising game functions

How this port makes the game's own hot functions faster on the Switch, separate from the code generator: the
generator (ReXGlue and the post-codegen passes, see docs/building.md) turns every PowerPC function into C++ once. This
document is about what happens to the functions the profiles single out: finding them, the three ways they are made
faster, how a change is measured and how it is checked. Written 2026-10-22 from the Lightning work (the user's request:
frame drops in Lightning, aiming at 15 ms a frame).

## Contents

- [The rule](#the-rule)
- [1. Finding the hot functions](#1-finding-the-hot-functions)
- [2. Localized copies](#2-localized-copies)
- [3. Write-backs of the changed registers only](#3-write-backs-of-the-changed-registers-only)
- [4. Hand-written natives](#4-hand-written-natives)
- [5. Measuring](#5-measuring)
- [6. Checking](#6-checking)
- [7. Results so far](#7-results-so-far)
- [8. Workflow](#8-workflow)

## The rule

An optimised function does exactly what the recompiled one does: the same guest memory stores, the same registers
left in the context, the same calls in the same order, the same float rounding. Every method below is either exact by
construction (a tool's transformation, with its audit in its docstring) or written against the generated copy and
checked call by call. Faster is only worth having when it is the same game.

## 1. Finding the hot functions

The console decides, not the PC: the A57 at 1020 MHz has other costs than an x86 desktop.

- **Profile on the console.** Two lines in `bejeweled3.toml` next to the NRO:
  `switch_profile_report = true` and `switch_profile_stacks = true`. Every 10 s the profiler writes each thread's CPU
  use and sampled stacks to `logs/rex/rex_profile.log`; the previous run's report is kept as
  `logs/rex/rex_profile_prev.log` (a run that hung keeps its evidence). The game log
  (`logs/bejeweled3_00N.log`) counts the calls of every native in its `[hot] 10 s` lines.
- **Keep the map.** Every delivered NRO has its ELF and map in `out/nx/elf/` (`bejeweled3_dev_<what>_nolto_<date>_<time>.*`).
  A profile is read with the map of the build that wrote it.
- **Rank the functions.** `python tools/switch_hot_functions.py PROFILE --map MAP --thread "Main XThread" --top 40`.
  Module `default` is generated code, `app` our copies and natives (`n_<ADDR>` a localized copy, `h_<ADDR>` a
  hand-written native, `sub_<ADDR>` in `app` a hook with its native inlined).
- **Look inside one.** `python tools/profile_hot_pcs.py MAP ELF n_8245AAD86Native PROFILE` prints the sampled
  instructions of one function. This decides the method:
  - samples on the stores and loads of the context (`[x19, #...]` / `[x0, #...]` around calls and returns): the
    write-backs, section 3;
  - samples on one path of a function that has a cheap early-out: a hand-written fast path, section 4;
  - samples spread over loads of game data: memory latency, which a rewrite does not change (sub_8245AAD8, the effect
    update, is this kind);
  - samples on stack stores and divisions in a pixel loop: a hand-written loop (sub_824D8FF8).

## 2. Localized copies

`tools/localize_native.py` makes a C++ copy of a recompiled function (`app/generated/hot/n_<ADDR>.h`) with its
registers in C++ locals instead of the shared context, plain guest memory (no volatile), and smart sync around calls (a
call writes back and reloads only what its callee can reach). A hook replaces the function with the copy; each has an
enhancement key `bejeweled3_cpu_native_sub_<ADDR>` (on by default, off in the toml to compare).

- **The lists.** `app/src/cpu/hot/localized.txt` (the hand-picked batches: their hooks in
  `app/generated/hot/localized_hooks.inc`, compiled by `src/cpu/bejeweled3_hot_guest.cpp`) and
  `app/src/cpu/hot/localized_bulk.txt` (every function still in generated code with at least 3 console samples: their
  hooks spread over `localized_hooks_0..7.inc`, compiled by `src/cpu/bejeweled3_hot_shard_0..7.cpp`, so hundreds of
  copies compile in parallel; the shared preamble is `src/cpu/bejeweled3_hot_prelude.h`).
- **The build makes them.** `tools/post_codegen.py` runs `localize_native.py --list ... --bulk ...` after every codegen;
  by hand: `python tools/localize_native.py --list app/src/cpu/hot/localized.txt --bulk app/src/cpu/hot/localized_bulk.txt`.
- **Hook kinds.** A leaf copy (no calls) gets `BEJEWELED3_HOT_HOOK_LOGGED`: its stores are logged, so verify mode can run
  the original afterwards and compare every register and byte. A copy with calls gets `BEJEWELED3_HOT_HOOK_UNGUARDED`
  (its callees' effects cannot be rolled back): checked by the copy's construction and the lockstep.
- **Left out on purpose:**
  - tiny leaves (12 guest instructions or fewer, no calls): the generator already inlines copies of them into their
    callers; a hook makes them slower (sub_82531F10 went from 0.15 % to 0.85 % of the main thread when hooked);
  - functions with atomics or the global lock (`localize_native.py` refuses them);
  - any address our app or SDK sources mention (our own hooks: two hooks on one function do not link);
  - a function the bulk step cannot localize is skipped and named in its output.

## 3. Write-backs of the changed registers only

A copy wrote every register it keeps back to the context at each return and before each fully synced call, changed or
not. The compiler must keep those stores (the callee reads the context, and as far as it knows a guest store could
alias it). On the console they were the copies' hottest instructions.

`tools/hand_native_dirty_sync.py` (`rewrite_text`, applied by `localize_native.py` to every copy it makes) replaces each
write-back with stores of only the registers that may differ from the context there, from a dataflow over the copy:
at entry nothing differs; an assignment makes a register differ; a full sync makes all equal; a smart sync makes the
registers it moves equal and leaves the rest as they were. A register that cannot differ already holds the stored
value, so the context ends exactly as before. Its docstring is the audit.

- Forms it does not know (a `switch` jump table, an unknown use of a register local) leave that copy with its full
  write-backs; the copy's header says which it got ("Write-backs limited ...: N register stores of M").
- 2026-10-22: 360 of 383 copies limited, 31,647 register stores of 86,152 (37 %) left.

## 4. Hand-written natives

`app/src/cpu/hot/h_<ADDR>.h` replaces the copy in the hook when it exists (`localize_native.py` includes it after the
copy, which stays as its reference and fallback). The rules are docs/hand-natives.md: observably identical to the copy,
written against it, with an Optimisations + Audit block at the top. Worth it only where the profile shows work a
rewrite removes:

| Function | What it is | What the native does | Effect |
|---|---|---|---|
| sub_82453938 | a curve's value at t, one-entry cache | the cache hit and the linear interpolation with only the registers they change; the bezier search falls back to the copy before any store | 9.2 % -> 4.3 % of the main thread |
| sub_822DFF80 | 3x3 matrix product | both inputs read once in blocks (in-place products take the copy) | 5.1 % -> 2.6 % |
| sub_824D8FF8 | PopCap's software triangle rasterizer (the 4/5-match effect) | `tools/hand_native_824D8FF8.py`: the copy with its stack spills as locals, no loop barriers, exact reciprocal division (tested on 17.5 billion inputs) | 24 % of the effect's frames -> 2.4 % |
| sub_8255EF98 | a setter | its six-instruction leaf callee inlined: no call when the value is unchanged | small |

Not worth it: a flat profile inside the copy (most of the top 20 after the copies), memory latency, a function whose
time is its callees.

## 5. Measuring

Whole-frame times depend on what was on screen: two Lightning runs of the same build differ by more than most
optimisations. Three ways that do not:

- **Cost per call.** `python tools/profile_per_call.py "LABEL|PROFILE|MAP|LOG" ...` gives each native's main-thread time
  a call: profile share x main-thread CPU seconds / calls from the game log. Compare functions called millions of times
  with similar work a call; functions whose code did not change show how much two runs differ by themselves (the 11:03
  run's unchanged natives were 8-20 % cheaper a call than the 10:41 run's: a lighter scene).
- **The effect load.** sub_8245AAD8 (the effect update) costs what the effects on screen cost: its time a call says how
  heavy a run was (32 us at 10:21, 83 us at 10:41, 39 us at 11:03).
- **Frames.** `python tools/profile_frame_ms.py PROFILE` gives the main thread's milliseconds a game frame per window and
  the CPU clock; compare only at the same clock (1020 MHz is the console's normal one) and similar effect load.

The PC is a proxy only (x86, other caches); its profiler (`bejeweled3_pc_profile_s`) is useful for A/B runs of the
same deterministic input, not for the console's numbers.

## 6. Checking

- **Verify mode.** `bejeweled3_cpu_verify = true` (PC: `REX_BEJEWELED3_CPU_VERIFY=true`): every call of every leaf native
  runs the original afterwards and compares all registers and stored bytes ("[hot] DIFFERENCE ..." in the log and the
  hook off). `powershell -File tools\win\bj3_lightning_check.ps1 -Tag NAME -Verify` runs it on the PC with the test
  seed, the frame clock and the Lightning pad script. With hundreds of natives the checks make loading very slow (the
  2026-10-22 run did not leave the loading screen in 150 s); the check counts still cover everything that ran. A
  difference in a function reading data another thread writes (sub_828AFB28's f0, an audio field) is a race between
  the two runs, not a bug.
- **Lockstep.** For copies with calls: the same deterministic run with the natives off
  (`bj3_lightning_check.ps1 -NativesOff -Record`, twice) and on (`-Record`), frame hashes compared with
  `tools/compare_hashes.py` (raw recordings are overwritten; only hashes are kept).
- **The console.** The user's Lightning run with the profiler on: no "[hot] DIFFERENCE", "natives N of N on", and the
  frames.

## 7. Results so far

Main-thread milliseconds a frame in the heaviest Lightning windows, console at 1020 MHz:

| NRO | Natives | Heavy windows | Effect load (sub_8245AAD8 a call) |
|---|---|---|---|
| 09:29 | 35 copies | 21-25 ms | - |
| 10:21 | 70 copies, 4 hand-written | 25-35 ms | 32 us |
| 10:41 | + 17 with changed-only write-backs | 26-34 ms | 83 us (the heaviest run) |
| 11:03 | 383 copies, all with changed-only write-backs | 21-25 ms | 39 us |

Cost a call, 10:21 (similar load) against 11:03: sub_825652B0 1,912 -> 1,187 ns, sub_82454730 2,199 -> 1,551,
sub_825661E0 981 -> 531, sub_822E00F0 337 -> 125, sub_82453410 414 -> 251, sub_825328B8 262 -> 166; unchanged natives
8-19 % less in the same comparison.

A heavy frame of the 11:03 run: 73 % in our copies and natives, 17.5 % still generated code (functions with fewer than
3 console samples), 8 % renderer and runtime. No function above 2.5 %.

Known: one console start of the 10:21 NRO hung before the PopCap logo (the intro video); not reproduced, cause unknown;
`rex_profile_prev.log` keeps the evidence of the next one.

## 8. Workflow

1. Profile Lightning on the console (toml lines above); archive the NRO's ELF and map.
2. Rank (`switch_hot_functions.py`), look inside the top ones (`profile_hot_pcs.py`).
3. Generated code that is hot: add it to `localized_bulk.txt` (or `localized.txt` for a hand-picked batch), regenerate
   (`tools\win\bj3_regen_build.ps1`, or `localize_native.py --list ... --bulk ...` and a build).
4. A copy whose profile shows a removable cost: a hand-written native (docs/hand-natives.md), audited against the copy.
5. Build PC and Switch (`tools\win\build_switch.ps1`; development builds without LTO).
6. Check: verify mode on the PC, the lockstep for copies with calls.
7. Deliver the NRO; the user's console run with the profiler; compare with `profile_per_call.py` and
   `profile_frame_ms.py`, then back to 2.
