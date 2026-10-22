# Hand-written natives (batch 2, 2026-10-22)

The user's request: hand-write the 35 hottest functions of Lightning's main thread (app/src/cpu/hot/localized.txt,
batch 2) and the top three of batch 1 (sub_82453938, sub_8245AAD8, sub_822DFF80), audited, then verified.

## The reference

Every function already has a correct C++ form: its localized copy, `app/generated/hot/n_<ADDR>.h`
(tools/localize_native.py), made mechanically from the recompiled function. The hand-written native,
`app/src/cpu/hot/h_<ADDR>.h`, is an optimisation of that copy and must be **observably identical** to it:

- every guest memory store: the same addresses, the same bytes, and the same order wherever two stores or a store
  and a later load may overlap;
- every register the copy writes back to the context (its `BEJEWELED3_SYNC_OUT` list), with the same final values, and
  everything the copy changes in the context directly (`ctx.fpscr`, `ctx.lr`, ...);
- every call: the same callee, in the same order, with the context as the copy hands it over (write back every
  localized register before the call and reload them after, as the copy's full sync does; a call is never skipped,
  merged or reordered);
- floating point: the same operations in the same precision, rounded where the copy rounds. `double(float(std::fma(a,
  b, c)))` stays that (a single-precision fused multiply-add can round differently); `REX_FMULS`/`REX_FADDS` etc. keep
  their semantics; NaN handling (`QuietSingleBits` on stores, `SingleToDoublePure`) and the flush-mode calls
  (`ctx.fpscr.disableFlushMode()` / `enableFlushMode()`) stay exactly where they are.

## What may be optimised

- Redundant work the copy repeats: the same guest word loaded twice with no store in between that could overlap it,
  the same conversion computed twice, constants re-loaded from the read-only image (`REX_RO_CFG_*` gives the value).
- Several independent loads or stores at once (byte-swapped with `__builtin_bswap32`/NEON `vrev32q_u8`), keeping the
  per-element arithmetic identical; independent double-precision lanes with NEON (`float64x2_t`, `vfmaq_f64` is a
  fused double multiply-add like `std::fma` on doubles).
- Inlining a callee **only** when the callee is a leaf the native reproduces exactly, including every register and
  store the callee itself leaves behind (the caller's context after the inlined part must equal the context after
  the real call); otherwise the call stays a call.
- Loops: the same iterations in the same order; hoisting a load out of a loop only when no store in the loop can reach
  it.
- Control flow: branches may be reordered or merged when every path keeps its observable effects.

## Form

```cpp
// h_<ADDR>.h: hand-written native of sub_<ADDR> (<what it does>). Reference: app/generated/hot/n_<ADDR>.h.
// Optimisations: <list>. Audit: <how each optimisation keeps the copy's effects>.
#pragma once
namespace bejeweled3::hot::h_<ADDR> {
template <bool kLog> inline void Body(PPCContext& ctx, uint8_t* base) { ... }
inline void Native(PPCContext& ctx, uint8_t* base) { Body<false>(ctx, base); }
inline void NativeLogged(PPCContext& ctx, uint8_t* base) { Body<true>(ctx, base); }  // leaf functions only
}
```

The function stays in app/src/cpu/hot/localized.txt: tools/localize_native.py still makes its copy (the reference and
the fallback) and, when `app/src/cpu/hot/h_<ADDR>.h` exists, writes the hook with `bejeweled3::hot::h_<ADDR>` after
including the copy (`n_<ADDR>.h` comes first in app/generated/hot/localized_hooks.inc, so the hand-written header does
not include it). A hand-written native may call `n_<ADDR>::Native` / `NativeLogged` for the cases it does not cover.

Stores in `Body` go through a local macro that uses `BEJEWELED3_LOG_STORE_*` when `kLog` (leaf functions: the guard
rolls the stores back and compares, bejeweled3_hot_guest.cpp) and `REX_STORE_*` otherwise. Loads use the `REX_LOAD_*`
macros of bejeweled3_hot_guest.cpp (plain guest memory). Calls are made the way the copy makes them.

## Verification

- Leaf functions: `BEJEWELED3_HOT_HOOK_LOGGED`, so verify mode (`bejeweled3_cpu_verify`) checks every call: all
  registers and every stored byte against the recompiled original (PC and Ryujinx).
- Functions with calls cannot be checked call by call (their callees' effects cannot be rolled back):
  `BEJEWELED3_HOT_HOOK_UNGUARDED`, an audit of each against its copy, and a deterministic Lightning run on the PC (test
  seed, frame clock) whose frame hashes must match the runs of the build with the copies (tools/compare_hashes.py: the
  frames that reproduce between two baseline runs must all appear).
