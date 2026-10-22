// h_822DFF80.h: hand-written native of sub_822DFF80, the 3x3 matrix product out(r3) = a(r4) * b(r5) (row-major
// floats, 36 bytes each): out[i][j] = fmadds(a[i][k2], b[k2][j], fmadds(a[i][k1], b[k1][j], fmuls(a[i][k0], b[k0][j]))).
// Reference: app/generated/hot/n_822DFF80.h (docs/hand-natives.md). 4.7 % of Lightning's main thread on the console.
//
// Optimisation: when out's 36 bytes overlap neither input, the 18 inputs are read once, nine big-endian words of each
// matrix together, instead of 54 single loads re-read after every store (the original re-reads because out may be an
// input: an in-place product), and the nine results are stored together.
// Audit:
//   - the arithmetic of each element is the copy's operation for operation: its fmuls pair, then its two fmadds in the
//     copy's order, each double(float(std::fma(...))) on the doubles of the loaded floats (REX_FMULS, std::fma);
//   - without an overlap no store can change an input, so every load of the copy reads what the first read returns;
//   - f0-f13 end with the copy's last values (traced block by block below), r3-r5 are unchanged, and the flush mode is
//     switched off first as the copy does;
//   - blocks at or above 0xDFFFFFDC (the PC's physical window starts at 0xE0000000) also take the copy;
//   - with an overlap the copy's own sequence runs (n_822DFF80::Native / NativeLogged), so an in-place product is the
//     copy exactly.
#pragma once

// n_822DFF80.h (the copy) is included just before this file by app/generated/hot/localized_hooks.inc.

namespace bejeweled3::hot::h_822DFF80 {

inline bool Overlaps(uint32_t a, uint32_t b) { return a < b + 36u && b < a + 36u; }

template <bool kLog>
inline void Body(PPCContext& ctx, uint8_t* base) {
  const uint32_t o = ctx.r3.u32, pa = ctx.r4.u32, pb = ctx.r5.u32;
  if (Overlaps(o, pa) || Overlaps(o, pb) || o > 0xDFFFFFDBu || pa > 0xDFFFFFDBu ||
      pb > 0xDFFFFFDBu) [[unlikely]] {
    if constexpr (kLog) n_822DFF80::NativeLogged(ctx, base); else n_822DFF80::Native(ctx, base);
    return;
  }
  ctx.fpscr.disableFlushMode();
  uint32_t wa[9], wb[9];
  std::memcpy(wa, REX_RAW_ADDR(pa), 36);
  std::memcpy(wb, REX_RAW_ADDR(pb), 36);
  double a[9], b[9];
  for (int i = 0; i < 9; ++i) {
    a[i] = double(std::bit_cast<float>(__builtin_bswap32(wa[i])));
    b[i] = double(std::bit_cast<float>(__builtin_bswap32(wb[i])));
  }
  auto fm = [](double x, double y, double z) { return double(float(std::fma(x, y, z))); };
  // Blocks in the copy's order (the products as the copy pairs them; the register each value ends in noted).
  const double m0 = REX_FMULS(a[0], b[0]);                 // f12
  const double e0 = fm(b[6], a[2], fm(a[1], b[3], m0));    // f6 (inner f7)
  const double m1 = REX_FMULS(b[7], a[2]);                 // f13
  const double e1 = fm(b[1], a[0], fm(a[1], b[4], m1));    // f11 (inner f12)
  const double m2 = REX_FMULS(b[8], a[2]);                 // f4
  const double i2 = fm(a[1], b[5], m2);                    // f3
  const double e2 = fm(b[2], a[0], i2);                    // f2
  const double m3 = REX_FMULS(a[5], b[6]);                 // f9
  const double e3 = fm(a[3], b[0], fm(a[4], b[3], m3));    // f7 (inner f8)
  const double m4 = REX_FMULS(a[4], b[4]);                 // f0
  const double e4 = fm(a[3], b[1], fm(a[5], b[7], m4));    // f12 (inner f13)
  const double m5 = REX_FMULS(a[4], b[5]);                 // f5
  const double e5 = fm(a[3], b[2], fm(a[5], b[8], m5));    // f3 (inner f4)
  const double m6 = REX_FMULS(b[6], a[8]);                 // f10
  const double e6 = fm(a[7], b[3], fm(a[6], b[0], m6));    // f8 (inner f9)
  const double m7 = REX_FMULS(b[4], a[7]);                 // f1
  const double i7 = fm(b[7], a[8], m7);                    // f0
  const double e7 = fm(b[1], a[6], i7);                    // f13
  const double m8 = REX_FMULS(b[5], a[7]);                 // f9
  const double i8 = fm(b[8], a[8], m8);                    // f5
  const double e8 = fm(b[2], a[6], i8);                    // f4
  (void)e2;
  (void)i2;
  const double out[9] = {e0, e1, e2, e3, e4, e5, e6, e7, e8};
  uint32_t wo[9];
  for (int i = 0; i < 9; ++i) wo[i] = __builtin_bswap32(std::bit_cast<uint32_t>(float(out[i])));
  if constexpr (kLog) ::bejeweled3::hot::LogBefore(base, o, 36);
  std::memcpy(REX_RAW_ADDR(o), wo, 36);
  // The registers as the copy leaves them: each one's last value (block 7 for f0-f3 and f13, block 8 for the rest).
  ctx.f0.f64 = i7;
  ctx.f1.f64 = m7;
  ctx.f2.f64 = a[7];
  ctx.f3.f64 = b[4];
  ctx.f4.f64 = e8;
  ctx.f5.f64 = i8;
  ctx.f6.f64 = a[6];
  ctx.f7.f64 = b[2];
  ctx.f8.f64 = a[8];
  ctx.f9.f64 = m8;
  ctx.f10.f64 = a[7];
  ctx.f11.f64 = b[5];
  ctx.f12.f64 = b[8];
  ctx.f13.f64 = e7;
}

inline void Native(PPCContext& ctx, uint8_t* base) { Body<false>(ctx, base); }
inline void NativeLogged(PPCContext& ctx, uint8_t* base) { Body<true>(ctx, base); }

}  // namespace bejeweled3::hot::h_822DFF80
