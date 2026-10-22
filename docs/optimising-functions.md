# Optimising game functions

How this port makes the game's own hot functions faster on the Switch. Written 2026-10-22 from the Lightning work (the
user's request: frame drops in Lightning, aiming at 15 ms a frame).

> [!IMPORTANT]
> **This is not a code generation step. It starts from a profile of the running game.**
>
> The code generator (ReXGlue and the post-codegen passes, docs/building.md) translates *every* PowerPC function to
> C++ the same way, knowing nothing about which ones matter. Nothing in this document happens because the code was
> generated: it happens because a **console profile** showed where the frame time goes. Every choice here (which
> functions get a localized copy, which are written by hand, which small callees are inlined) is a list made from
> profiles (`localized.txt`, `localized_bulk.txt`, `inline_hot.txt`, the `h_<ADDR>.h` files), and every change is
> judged by the next profile.
>
> The tools do run in the build (`tools/post_codegen.py` calls `localize_native.py` after the generator), but only to
> remake the copies the lists ask for, so that they stay in step with freshly generated code. They decide nothing on
> their own: with empty lists the build is the plain generated code.

## Contents

- [The cycle](#the-cycle)
- [The rule](#the-rule)
- [1. Finding the hot functions](#1-finding-the-hot-functions)
- [2. Localized copies](#2-localized-copies)
- [3. Write-backs of the changed registers only](#3-write-backs-of-the-changed-registers-only)
- [4. Small callees inlined into the copies](#4-small-callees-inlined-into-the-copies)
- [5. Direct hook dispatch](#5-direct-hook-dispatch)
- [6. Hand-written natives](#6-hand-written-natives)
- [7. Measuring](#7-measuring)
- [8. Checking](#8-checking)
- [9. Results](#9-results)
- [10. Workflow](#10-workflow)

## The cycle

1. **Build and play.** A normal NRO on the console, the profiler on in `bejeweled3.toml`, the scene that is slow
   (here: Lightning with its special-gem effects).
2. **Profile.** Which functions the main thread spends its time in, and inside them, on which instructions (section 1).
3. **Choose.** Functions still in generated code go on a list for a localized copy (section 2); hot small callees go
   on the inline list (section 4); a copy whose profile shows a cost a rewrite removes gets a hand-written native
   (section 6).
4. **Apply and check.** Regenerate the copies, build, check exactness (section 8).
5. **Measure** with the next console profile against the previous one (section 7). Then back to 1.

Sections 2 to 6 are the ways a chosen function is made faster; 1, 7 and 8 are the profiling, measuring and checking
that drive them.

## The rule

An optimised function does exactly what the recompiled one does: the same guest memory stores, the same registers
left in the context, the same calls in the same order, the same float rounding. Each method below says how it keeps
that:

| Method | Exact because |
|---|---|
| Localized copies (2) | made mechanically from the generated function's own statements (`localize_native.py`) |
| Changed-only write-backs (3) | a dataflow proves every store left out would write the value the context already holds |
| Inlined callees (4) | the callee's own generated code, run on the same context with the same sync around it |
| Direct dispatch (5) | the same native runs; only the use counter and the first-call self-check are left out |
| Hand-written natives (6) | written against the copy and audited (docs/hand-natives.md); leaf ones checked every call |

Faster is only worth having when it is the same game.

## 1. Finding the hot functions

The console decides, not the PC: the A57 at 1020 MHz has other costs than an x86 desktop.

- **Profile on the console.** Two lines in `bejeweled3.toml` next to the NRO: `switch_profile_report = true` and
  `switch_profile_stacks = true`. Every 10 s the profiler writes each thread's CPU use and sampled stacks to
  `logs/rex/rex_profile.log`; the previous run's report is kept as `logs/rex/rex_profile_prev.log` (a run that hung
  keeps its evidence). The game log needs `log_level = "info"` and `log_to_file = true` (a release has no log).
- **Count the calls when the analysis needs them.** With `bejeweled3_cpu_hook_counters = true` the game log's
  `[hot] 10 s` lines count every native's calls (section 7, cost a call). Play builds leave it off (section 5): then
  those lines have no counts.
- **Keep the map.** Every delivered NRO has its ELF and map in `out/nx/elf/`. A profile is read with the map of the
  build that wrote it, never another one.
- **Rank the functions.** `python tools/switch_hot_functions.py PROFILE --map MAP --thread "Main XThread" --top 40`.
  Module `default` is generated code, `app` our copies and natives: `n_<ADDR>` a localized copy, `h_<ADDR>` a
  hand-written native, `sub_<ADDR>` in `app` a hook with its native inlined into it, `inl::sub_<ADDR>` an inlined
  callee the compiler kept as a function.
- **Look inside one.** `python tools/profile_hot_pcs.py MAP ELF n_8245AAD86Native PROFILE` prints the sampled
  instructions of one function with their disassembly. This decides the method:
  - samples on stores and loads of the context (`[x19, #...]` / `[x0, #...]`) around calls and returns: the
    write-backs (section 3) or the call itself (section 4);
  - samples on one path of a function with a cheap early-out: a hand-written fast path (section 6, sub_82453938);
  - samples on stack stores and divisions in a pixel loop: a hand-written loop (section 6, sub_824D8FF8);
  - samples spread over loads of game data: memory latency, which a rewrite does not change (sub_8245AAD8, the effect
    update, is this kind);
  - a flat profile with no hot spot: the copy already does what can be done; leave it.

## 2. Localized copies

`tools/localize_native.py` makes a C++ copy of a recompiled function (`app/generated/hot/n_<ADDR>.h`) with its
registers in C++ locals instead of the shared context, plain guest memory (no volatile), and smart sync around calls
(a call writes back and reloads only what its callee can reach). A hook replaces the function with the copy; each has
an enhancement key `bejeweled3_cpu_native_sub_<ADDR>` (on by default; set it to `false` in the toml to compare).

- **The lists, made from profiles:**
  - `app/src/cpu/hot/localized.txt`: the hand-picked batches (the hottest 35, then the next 35). Their hooks are in
    `app/generated/hot/localized_hooks.inc`, compiled by `src/cpu/bejeweled3_hot_guest.cpp`.
  - `app/src/cpu/hot/localized_bulk.txt`: every function still in generated code with at least 3 samples in the
    console profiles (313 of them). Their hooks are spread over `localized_hooks_0..7.inc` by size, compiled by
    `src/cpu/bejeweled3_hot_shard_0..7.cpp`, so hundreds of copies compile in parallel; the shared preamble is
    `src/cpu/bejeweled3_hot_prelude.h`.
- **Remaking them.** The build remakes the listed copies after every codegen; by hand:
  `python tools/localize_native.py --list app/src/cpu/hot/localized.txt --bulk app/src/cpu/hot/localized_bulk.txt`.
- **Hook kinds.** A leaf copy (no calls) gets `BEJEWELED3_HOT_HOOK_LOGGED`: its stores are logged, so verify mode can
  run the original afterwards and compare every register and byte. A copy with calls gets
  `BEJEWELED3_HOT_HOOK_UNGUARDED` (its callees' effects cannot be rolled back).
- **Left out on purpose:**
  - tiny leaves (12 guest instructions or fewer, no calls): the generator already inlines copies of them into their
    callers, and a hook makes them slower (sub_82531F10 went from 0.15 % to 0.85 % of the main thread when hooked);
  - functions with atomics or the global lock (`localize_native.py` refuses them; the bulk step names what it skipped);
  - any address our app or SDK sources mention (our own hooks: two hooks on one function do not link).
- **A copy is not always faster.** Where the generator already kept a function's registers in locals, a copy can be
  as long as the original or longer (banjo-nx measured this on its own sweep). Judge every batch with the next
  profile, not by the number of copies.

## 3. Write-backs of the changed registers only

A copy wrote every register it keeps back to the context at each return and before each fully synced call, changed or
not. The compiler must keep those stores (the callee reads the context, and as far as it knows a guest store could
alias it). On the console they were the copies' hottest instructions.

`tools/hand_native_dirty_sync.py` (`rewrite_text`, applied by `localize_native.py` to each copy it remakes) replaces
each write-back with stores of only the registers that may differ from the context there, from a dataflow over the
copy: at entry nothing differs; an assignment makes a register differ; a full sync makes all equal; a smart sync makes
the registers it moves equal and leaves the rest as they were. A register that cannot differ already holds the stored
value, so the context ends exactly as before. Its docstring is the audit.

- Forms it does not know (a `switch` jump table, an unknown use of a register local) leave that copy with its full
  write-backs; the copy's header says which it got ("Write-backs limited ...: N register stores of M").
- 2026-10-22: 360 of 383 copies limited; 31,647 of 86,152 register stores at write-backs left (37 %).

## 4. Small callees inlined into the copies

A copy is made from the generated text with the generator's inline calls turned back into real calls, so every copy
called the tiny leaves for real (a call, the hook of a localized leaf, every register through the context and back),
where generated code calls them inline. `localize_native.py` (`inline_callees`) now gives each copy an inline copy of
the generated code of:

- the tiny leaves the generator inlines everywhere (its own rule, `inline_leaves.parse_functions`: at most 12 guest
  instructions, calling nothing);
- the console profile's hottest leaves of up to 160 instructions, listed in `app/src/cpu/hot/inline_hot.txt` (picked
  by calls a run and call sites in the copies).

Each inline copy is defined once per compiled file behind an include guard (`bejeweled3::hot::inl::sub_<ADDR>`), and
the call sites keep their sync around it. Left on their hooks: the hand-written natives (faster than the generated
code), the C runtime hooks, every address our sources mention. 2026-10-22: 1,247 call sites in 106 copies. The
compiler may still keep a large one as a function (`inl::sub_825DD678` in the profile); it then saves the hook and
the context round trip, not the call.

## 5. Direct hook dispatch

Every hook counted its native's calls for the `[hot] 10 s` report, and the guarded (leaf) hooks checked each thread's
first call against the original: a few instructions and a counter in the context on every one of millions of calls a
second. With `bejeweled3_cpu_hook_counters` off (the default), verify mode off and the guard keys at 0, every hook
calls its native directly (`bejeweled3::hot::g_fast_dispatch`, set at start by `ConfigureDispatch`; the log says
"[hot] dispatch: direct"). Turn the counters on for an analysis that needs call counts.

## 6. Hand-written natives

`app/src/cpu/hot/h_<ADDR>.h` replaces the copy in the hook when it exists (`localize_native.py` includes it after the
copy, which stays as its reference and fallback). The rules are docs/hand-natives.md: observably identical to the copy,
written against it, with an Optimisations + Audit block at the top. Worth it only where the profile shows work a
rewrite removes:

| Function | What it is | What the native does | Measured |
|---|---|---|---|
| sub_82453938 | a curve's value at t, with a one-entry cache | the cache hit and the linear interpolation with only the registers they change; the bezier search falls back to the copy before any store | 9.2 % of the main thread (copy and hook) -> 4.3 % |
| sub_822DFF80 | 3x3 matrix product | both inputs read once in blocks (in-place products take the copy) | 5.1 % -> 2.6 % |
| sub_824D8FF8 | PopCap's software triangle rasterizer (the 4/5-match effect) | `tools/hand_native_824D8FF8.py`: the copy with its stack spills as locals, no loop barriers, an exact reciprocal division (tested on 17.5 billion inputs) | 24 % of the heaviest windows -> 2.4 % (but see below) |
| sub_8255EF98 | a setter | its six-instruction leaf callee inlined: no call when the value is unchanged | small |

The shares come from different console sessions, so the scene differs between the two numbers of a row. The first two
are close to per-call savings (both functions run every frame in every scene). The rasterizer's only runs with the
4/5-match effect, so how often that effect came up decides most of its row.

Not worth it: a flat profile inside the copy (most of the top 20 after the copies), memory latency, a function whose
time is its callees. Two of the top three on the console, sub_8245AAD8 and sub_825652B0, are of those kinds.

## 7. Measuring

Whole-frame times depend on what was on screen: two Lightning runs of the same build differ by more than most
optimisations. Compare runs at the same CPU clock (`clocks: CPU ... MHz` in the profile; 1020 MHz is the console's
normal one) and with one of these:

- **Cost a call** (needs `bejeweled3_cpu_hook_counters = true`):
  `python tools/profile_per_call.py "LABEL|PROFILE|MAP|LOG" ...` gives each native's main-thread time a call: its
  profile share x the main thread's CPU seconds / its calls in the game log. Compare functions called millions of
  times with similar work a call. A function that loops over the effects on screen costs what the scene costs (the
  effect update sub_8245AAD8: 32 us a call at 10:21, 83 us at 10:41, 39 us at 11:03).
- **Busy windows with markers** (no counts needed):
  `python tools/profile_busy_markers.py "LABEL|PROFILE|MAP" ...` gives, over the windows where the main thread is busy,
  its milliseconds a game frame and those of marker functions whose code did not change between the runs (by default
  the hand-written sub_82453938, sub_822DFF80, sub_8255EF98). The markers' time a frame is the work the scene asked
  for, so they say how much heavier one run was than the other.
- **Frames** per window: `python tools/profile_frame_ms.py PROFILE` gives the main thread's milliseconds a game frame
  and the clock of every window.

Functions whose code did not change are the yardstick either way: when they got 20 % cheaper between two runs, the
scene did, not the code.

The PC is a proxy only (x86, other caches); its profiler (`bejeweled3_pc_profile_s`) is useful for A/B runs of the same
deterministic input, not for the console's numbers.

## 8. Checking

- **Verify mode.** `bejeweled3_cpu_verify = true` (PC: `REX_BEJEWELED3_CPU_VERIFY=true`): every call of every leaf
  native runs the original afterwards and compares all registers and stored bytes ("[hot] DIFFERENCE ..." in the log,
  and the hook switches off). `powershell -File tools\win\bj3_lightning_check.ps1 -Tag NAME -Verify` runs it on the PC
  with the test seed, the frame clock and the Lightning pad script.
  - With hundreds of natives the checks make loading very slow: the 2026-10-22 run with 388 did not leave the loading
    screen in 150 s. Use it for what it reached; play past loading without it.
  - A difference in a function that reads data another thread writes is a race between the two runs, not a bug
    (sub_828AFB28's f0 on 2026-10-22: an audio field that changed between the native's run and the original's).
- **A run without verify.** The same script without `-Verify` (the natives as the console runs them, direct dispatch):
  the game must reach and play Lightning.
- **Lockstep.** For copies with calls, the frame-by-frame check: the same deterministic run with the natives off
  (`bj3_lightning_check.ps1 -NativesOff -Record`, twice) and on (`-Record`), frame hashes compared with
  `tools/compare_hashes.py` (raw recordings are overwritten; only the hashes are kept). Not run for the 313 bulk copies
  of 2026-10-22: they were checked by verify mode (their leaf ones), a PC play-through and the console runs.
- **The Switch build.** `python tools/check_switch_build.py --elf <ELF> --nro <NRO> --ninja <build.ninja>`: among its
  checks, no polling loop that lost its barrier (a branch-to-self outside the game's own halts) and no fused vector
  multiply-add that is not one of the game's own (`std::fma`) packed into a vector instruction. Its lists of known
  cases are the game's; a new failure is examined in the disassembly before it is added to them.
- **The console.** The user's Lightning run with the profiler on: no "[hot] DIFFERENCE", "natives N of N on", and the
  frames.

## 9. Results

Main-thread milliseconds a game frame on the console at 1020 MHz, heavy Lightning:

| NRO | Natives | Heavy windows | Scene |
|---|---|---|---|
| 09:29 | 35 copies | 21-25 ms | - |
| 10:21 | 70 copies, 4 hand-written | 25-35 ms | effect update 32 us a call |
| 10:41 | + 17 with changed-only write-backs | 26-34 ms | effect update 83 us a call (the heaviest run) |
| 11:03 | 383 copies, all with changed-only write-backs | 21-25 ms (busy average 20.5) | effect update 39 us a call |
| 11:24 | + inlined callees, direct dispatch | 16-18.5 ms (busy average 15.4) | markers at about a quarter of 11:03's |

Read the last rows with their scene: the 11:24 run's effects were much lighter (its markers did about a quarter of the
11:03 run's work a frame), so part of its gain is the scene's.

Cost a call, 10:21 against 11:03 (a similar scene): sub_825652B0 1,912 -> 1,187 ns, sub_82454730 2,199 -> 1,551,
sub_825661E0 981 -> 531, sub_822E00F0 337 -> 125, sub_82453410 414 -> 251, sub_825328B8 262 -> 166. Unchanged natives
were 8-19 % cheaper in the same comparison, so take that much off each.

A heavy frame of the 11:24 run: 63 % in our copies and natives, 21 % still generated code (the functions with fewer
than 3 console samples), 16.5 % renderer and runtime; no function above 2.1 %. That flat profile is where this method
runs out: at 1020 MHz, halving the top functions would not reach 15 ms in the heaviest scenes.

Known: one console start of the 10:21 NRO hung before the PopCap logo (the intro video). It was not reproduced and the
cause is unknown; `rex_profile_prev.log` keeps the evidence of the next one.

## 10. Workflow

1. Profile Lightning on the console (section 1: the profiler lines, the log, and `bejeweled3_cpu_hook_counters = true`
   when the analysis needs call counts); archive the NRO's ELF and map.
2. Rank (`switch_hot_functions.py`); look inside the top ones (`profile_hot_pcs.py`).
3. Choose:
   - generated code that is hot: add it to `localized_bulk.txt` (or `localized.txt` for a hand-picked batch);
   - a hot small leaf that copies call: add it to `inline_hot.txt`;
   - a copy whose profile shows a removable cost: a hand-written native (docs/hand-natives.md), audited against the
     copy.
4. Remake the copies (`tools\win\bj3_regen_build.ps1`, or `localize_native.py --list ... --bulk ...` and a build).
5. Build PC and Switch (`tools\win\build_switch.ps1`).
6. Check (section 8): verify mode on the PC, a run without it through Lightning, the lockstep for copies with calls.
7. Deliver the NRO; the user's console run with the profiler; compare with `profile_per_call.py` (with counts) or
   `profile_busy_markers.py` (without), and `profile_frame_ms.py`. Then back to 2.
