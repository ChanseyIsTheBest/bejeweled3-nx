// h_82453938.h: hand-written native of sub_82453938, a curve's value at t (f1) with a one-entry cache: the object
// (r3) keeps the last t at +60 and its value at +64; its keys are 8-byte (t, value) pairs from [+16] to [+20], and
// +44 > 0 selects a bezier search (up to 1000 steps of sub_825413F0) over plain linear interpolation.
// Reference: app/generated/hot/n_82453938.h (docs/hand-natives.md). 8.3 % of Lightning's main thread on the console.
//
// Where the time goes (the console profile's samples inside the copy, the 09:29 NRO): the cache hit, about two thirds,
// almost all of it the copy's entry and exit (its 16 context registers loaded and written back around five
// instructions); then the linear interpolation's key search. The bezier search was hardly sampled.
// Optimisations:
//   - the cache hit, the one-key curve and linear interpolation are written here with only the registers they change;
//   - the key search keeps [+16] in a register (the copy reloads it after each iteration's barrier: no store in the
//     loop can change it) and runs without the barriers (a bounded loop, no waiting on another thread);
//   - the bezier search is the copy itself (n_82453938::Native), entered before anything is stored.
// Audit (each path against the copy, register by register):
//   - every path: the back-chain word at r1 - 240 (REX_CHAIN_STORE, as the copy), the flush mode switched off, f11 =
//     the float at +60; at the exit r1 = the copy's r1 (its low word - 240, then + 240 as a 64-bit add), r12 = that
//     - 40, lr = 0x82453C7C (the copy's last lr; its earlier lr values are overwritten);
//   - cache hit (f11 == f1, so NaN misses as with the copy's cr6.eq): f1 = the float at +64; nothing else changes;
//   - a miss stores float(f1) at +60 first, as the copy. The decision loads of +16, +20 and +44 are made before that
//     store here: fixed offsets of the same object, none of them overlapping +60..+63;
//   - r8 = ([+20] - [+16]).s32 >> 3, sign-extended; r10 = [+16] (zero-extended), r11 as the copy leaves it;
//   - one key (r8 == 1): r11 = r10 = [+16], f1 = the key's value (+4), stored at +64;
//   - bezier (r8 != 1, [+44] > 0): n_82453938::Native with the context untouched (only the chain word and the flush
//     mode, which the copy sets again to the same values), so it is the copy's path exactly;
//   - linear, r8 <= 0: r11 = [+44], r9 = 1, f1 = f2 stored at +64;
//   - linear, r8 > 1: the copy's loop with its locals f0, f13, r7, r9, r10, r11 (f0 keeps its last load when a step
//     skips it, r7 = r8 - 1 only once a step reaches it); not found: f1 = f2; found: f12 = the key at r11, f0 =
//     float(t - f13), f13 = fsubs(f12, f13), r10 = 0xFFFFFFFF82000000, f11 = fdivs(f0, f13), f12 = 1.0 unless f11 is
//     not above 1.0 (then fdivs again, as the copy), f0 / f13 the values around it, f1 = the fused multiply-add rounded
//     to single, stored at +64. The same macros (REX_FSUBS, REX_FDIVS, double(float(std::fma))) as the copy.
#pragma once

// n_82453938.h (the copy) is included just before this file by app/generated/hot/localized_hooks.inc.

namespace bejeweled3::hot::h_82453938 {

inline void Native(PPCContext& ctx, uint8_t* base) {
  REX_FUNC_PROLOGUE();
  const uint32_t r31 = ctx.r3.u32;
  REX_CHAIN_STORE(REX_STORE_U32(ctx.r1.u32 - 240u, ctx.r1.u32));
  ctx.fpscr.disableFlushMode();
  const double t = ctx.f1.f64;
  const double f11 = double(REX_LOAD_D_F32(r31, 60));
  // The exit every path shares (loc_82453C70).
  auto leave = [&]() {
    PPCRegister r1 = ctx.r1;
    r1.u32 = r1.u32 - 240u;
    r1.s64 = r1.s64 + 240;
    ctx.r1 = r1;
    ctx.r12.s64 = r1.s64 + -40;
    ctx.lr = 0x82453C7C;
  };
  if (f11 == t) {
    ctx.f11.f64 = f11;
    ctx.f1.f64 = double(REX_LOAD_D_F32(r31, 64));
    leave();
    return;
  }
  const uint64_t keys = REX_LOAD_D_U32(r31, 16);
  const uint64_t keys_end = REX_LOAD_D_U32(r31, 20);
  PPCRegister r11;
  r11.u64 = keys_end - keys;
  PPCRegister r8;
  r8.s64 = r11.s32 >> 3;
  const uint64_t mode = REX_LOAD_D_U32(r31, 44);
  if (r8.s32 != 1 && int32_t(uint32_t(mode)) > 0) {
    n_82453938::Native(ctx, base);  // the bezier search: the copy, from its start
    return;
  }
  {
    PPCRegister v;
    v.f32 = float(t);
    REX_STORE_D_U32(r31, 60, v.u32);
  }
  ctx.f11.f64 = f11;
  ctx.r8 = r8;
  ctx.r10.u64 = keys;
  double f1;
  if (r8.s32 == 1) {
    ctx.r11.u64 = keys;
    f1 = double(REX_LOAD_D_F32(uint32_t(keys), 4));
  } else {
    ctx.r9.s64 = 1;
    r11.u64 = mode;
    if (!(r8.s32 > 1)) {
      ctx.r11 = r11;
      f1 = ctx.f2.f64;
    } else {
      double f0 = ctx.f0.f64, f13 = ctx.f13.f64;
      PPCRegister r7 = ctx.r7, r9, r10;
      r9.s64 = 1;
      r10.s64 = 8;
      bool found = false;
      for (;;) {
        r11.u64 = keys + r10.u64;
        f13 = double(REX_LOAD_D_F32(r11.u32, -8));
        if (!(t < f13)) {
          f0 = double(REX_LOAD_D_F32(r11.u32, 0));
          if (!(t > f0)) {
            found = true;
            break;
          }
        }
        r7.s64 = r8.s64 + -1;
        if (r9.s32 == r7.s32) {
          found = true;
          break;
        }
        r9.s64 = r9.s64 + 1;
        r10.s64 = r10.s64 + 8;
        if (!(r9.s32 < r8.s32)) break;
      }
      ctx.r7 = r7;
      ctx.r9 = r9;
      ctx.r11 = r11;
      if (!found) {
        ctx.f0.f64 = f0;
        ctx.f13.f64 = f13;
        ctx.r10 = r10;
        f1 = ctx.f2.f64;
      } else {
        double f12 = double(REX_LOAD_D_F32(r11.u32, 0));
        f0 = double(float(t - f13));
        f13 = REX_FSUBS(f12, f13);
        ctx.r10.s64 = -2113929216;
        f12 = double(std::bit_cast<float>(uint32_t(REX_RO_U32(0x820014F0, 0x3F800000))));
        const double q = REX_FDIVS(f0, f13);
        ctx.f11.f64 = q;
        if (!(q > f12)) f12 = REX_FDIVS(f0, f13);
        f0 = double(REX_LOAD_D_F32(r11.u32, -4));
        f13 = double(REX_LOAD_D_F32(r11.u32, 4));
        f13 = REX_FSUBS(f13, f0);
        f1 = double(float(std::fma(f13, f12, f0)));
        ctx.f0.f64 = f0;
        ctx.f12.f64 = f12;
        ctx.f13.f64 = f13;
      }
    }
  }
  ctx.f1.f64 = f1;
  {
    PPCRegister v;
    v.f32 = float(f1);
    REX_STORE_D_U32(r31, 64, v.u32);
  }
  leave();
}

}  // namespace bejeweled3::hot::h_82453938
