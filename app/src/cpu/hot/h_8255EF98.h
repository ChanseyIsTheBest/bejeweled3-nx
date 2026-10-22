// h_8255EF98.h: hand-written native of sub_8255EF98, a setter: v = sub_82531F68(this->[116], r4, r5) (a cell of a
// 148-byte-row table); when v != r6 it calls this's virtual method 336 (vtable +0x150) and then
// sub_82531F88(this->[116], r4, r5, r6), which stores the new value. Reference: app/generated/hot/n_8255EF98.h
// (docs/hand-natives.md). 1.2 % of Lightning's main thread on the console.
//
// Optimisation: the first callee, sub_82531F68, is a six-instruction leaf (lwz r10,392(r3); rlwinm r9,r5,4,0,27;
// mulli r11,r4,148; lwzx r10,r10,r9; add r11,r10,r11; lwz r3,32(r11); blr) and runs inline instead of as a call with
// its smart sync (the common path, v == r6, is then the function without any call). The virtual call and
// sub_82531F88 (which calls further) stay calls with the copy's full sync.
// Audit:
//   - the inlined callee is __imp__sub_82531F68's recompiled code statement for statement (bejeweled3_recomp.103.cpp):
//     the same loads (REX_LOAD_D_U32 at 392 and 32, REX_LOAD_U32 of r10 + r9 with its 32-bit wrap) and the same 64-bit
//     register values: r9 = u32(r5 << 4), r11 = s64(r4.u64 * 148) + r10.u64, r10 and r3 zero-extended words. It leaves
//     r3, r9, r10 and r11 changed and nothing else (no store, no lr, cr or fpscr), so ctx.r9 / ctx.r10 get its values
//     and r3 / r11 go on as the copy's locals do;
//   - the copy's other effects in order: lr read into r12, the back-chain word at r1 - 128 (REX_CHAIN_STORE as the
//     copy has it), r1 = r1 - 128 by its 32 low bits, ctx.lr = 0x8255EFBC at the first call site, the unsigned
//     compare of the callee's r3 with r6 (cr6 / xer are locals of the copy: not in the context);
//   - v == r6: r1 + 128 and the copy's write-back (r1, r3 = v, r4-r6 as they came in, r11 = the callee's, r12 = lr);
//   - v != r6: r11 = this->vtable[336 / 4], r3 = this, ctx.lr = 0x8255EFD8, every localized register written back,
//     REX_CALL_INDIRECT_FUNC, then r3 = this->[116] (reloaded after the call as the copy does), r4-r6 = the inputs,
//     ctx.lr = 0x8255EFEC, sub_82531F88 (__imp__, as the copy calls it), r1 + 128. Around both calls the context is
//     the copy's context: its locals are only the registers it writes back, so the calls work on ctx directly here;
//   - r28-r31 are locals of the copy (the save helper is not part of it), so they are not stored or restored here
//     either.
#pragma once

REX_EXTERN(__imp__sub_82531F88);

namespace bejeweled3::hot::h_8255EF98 {

template <bool kLog>
inline void Body(PPCContext& ctx, uint8_t* base) {
  REX_FUNC_PROLOGUE();
  const PPCRegister r31 = ctx.r3, r30 = ctx.r4, r29 = ctx.r5, r28 = ctx.r6;
  const uint64_t lr = ctx.lr;
  PPCRegister r1 = ctx.r1;
  const uint32_t ea = -128 + r1.u32;
  REX_CHAIN_STORE(REX_STORE_U32(ea, r1.u32));
  r1.u32 = ea;
  // sub_82531F68(r3 = this->[116], r4, r5), inline.
  PPCRegister r3, r9, r10, r11;
  r3.u64 = REX_LOAD_D_U32(r31.u32, 116);
  r10.u64 = REX_LOAD_D_U32(r3.u32, 392);
  r9.u64 = ((uint32_t)(r29.u32 << 4));
  r11.s64 = static_cast<int64_t>(r30.u64 * static_cast<uint64_t>(148));
  r10.u64 = REX_LOAD_U32(r10.u32 + r9.u32);
  r11.u64 = r10.u64 + r11.u64;
  r3.u64 = REX_LOAD_D_U32(r11.u32, 32);
  ctx.r9 = r9;
  ctx.r10 = r10;
  ctx.lr = 0x8255EFBC;
  ctx.r12.u64 = lr;
  if (r3.u32 == r28.u32) {
    r1.s64 = r1.s64 + 128;
    ctx.r1 = r1;
    ctx.r3 = r3;
    ctx.r11 = r11;
    return;
  }
  // this->vtable[336 / 4](this), with the copy's full write-back (r4-r6 are unchanged in ctx).
  r11.u64 = REX_LOAD_D_U32(r31.u32, 0);
  r11.u64 = REX_LOAD_D_U32(r11.u32, 336);
  ctx.r1 = r1;
  ctx.r3 = r31;
  ctx.r11 = r11;
  ctx.lr = 0x8255EFD8;
  REX_CALL_INDIRECT_FUNC(r11.u32);
  // sub_82531F88(this->[116], r4, r5, r6).
  ctx.r6 = r28;
  ctx.r3.u64 = REX_LOAD_D_U32(r31.u32, 116);
  ctx.r5 = r29;
  ctx.r4 = r30;
  ctx.lr = 0x8255EFEC;
  __imp__sub_82531F88(ctx, base);
  ctx.r1.s64 = ctx.r1.s64 + 128;
}

inline void Native(PPCContext& ctx, uint8_t* base) { Body<false>(ctx, base); }

}  // namespace bejeweled3::hot::h_8255EF98
