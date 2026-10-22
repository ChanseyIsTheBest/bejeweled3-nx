// The preamble of the localized copies' sources (bejeweled3_hot_guest.cpp and the bulk list's shards,
// bejeweled3_hot_shard_<k>.cpp): the generated code's macros with plain guest memory, the store log of the
// leaf copies' checked calls and the loop barrier. Moved here from bejeweled3_hot_guest.cpp on 2026-10-22.
#pragma once

// The generated code's macros and helpers (the localized copies are made of generated code) ...
#include "../../generated/default/bejeweled3_pch.h"

// ... with plain guest memory: the recompiled code's accesses are volatile, which keeps GCC from pairing and
// scheduling them; a native replacement does not need that (a racing write from another thread is a race either way).
#undef REX_LOAD_U8
#undef REX_LOAD_U16
#undef REX_LOAD_U32
#undef REX_LOAD_U64
#undef REX_STORE_U8
#undef REX_STORE_U16
#undef REX_STORE_U32
#undef REX_STORE_U64
#define REX_LOAD_U8(x) (*(u8*)(base + (u32)(x) + REX_PHYS_HOST_OFFSET(x)))
#define REX_LOAD_U16(x) __builtin_bswap16(*(u16*)(base + (u32)(x) + REX_PHYS_HOST_OFFSET(x)))
#define REX_LOAD_U32(x) __builtin_bswap32(*(u32*)(base + (u32)(x) + REX_PHYS_HOST_OFFSET(x)))
#define REX_LOAD_U64(x) __builtin_bswap64(*(u64*)(base + (u32)(x) + REX_PHYS_HOST_OFFSET(x)))
#define REX_STORE_U8(x, y) (*(u8*)(base + (u32)(x) + REX_PHYS_HOST_OFFSET(x)) = (y))
#define REX_STORE_U16(x, y) (*(u16*)(base + (u32)(x) + REX_PHYS_HOST_OFFSET(x)) = __builtin_bswap16(y))
#define REX_STORE_U32(x, y) (*(u32*)(base + (u32)(x) + REX_PHYS_HOST_OFFSET(x)) = __builtin_bswap32(y))
#define REX_STORE_U64(x, y) (*(u64*)(base + (u32)(x) + REX_PHYS_HOST_OFFSET(x)) = __builtin_bswap64(y))

#include "cpu/bejeweled3_hot_guest.h"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include <fmt/format.h>

#include <rex/cvar.h>
#include <rex/logging.h>

#include <unordered_map>

#include "bejeweled3_enhancements.h"

namespace bejeweled3::hot {
// The store log of a checked call of a leaf localized copy (NativeLogged, tools/localize_native.py): every guest
// store first records the bytes it overwrites.
void LogBefore(uint8_t* base, uint32_t addr, uint32_t len);
}  // namespace bejeweled3::hot

#define BEJEWELED3_LOG_STORE_U8(x, y) (::bejeweled3::hot::LogBefore(base, (u32)(x), 1), REX_STORE_U8(x, y))
#define BEJEWELED3_LOG_STORE_U16(x, y) (::bejeweled3::hot::LogBefore(base, (u32)(x), 2), REX_STORE_U16(x, y))
#define BEJEWELED3_LOG_STORE_U32(x, y) (::bejeweled3::hot::LogBefore(base, (u32)(x), 4), REX_STORE_U32(x, y))
#define BEJEWELED3_LOG_STORE_U64(x, y) (::bejeweled3::hot::LogBefore(base, (u32)(x), 8), REX_STORE_U64(x, y))
// A register of the context for bejeweled3::hot::SmartCall (the localized copies' smart-sync check).
#define BEJEWELED3_SPAN(r)   ::bejeweled3::hot::RegSpan { uint16_t(__builtin_offsetof(PPCContext, r)), uint16_t(sizeof(PPCContext::r)) }
// D-form stores (codegen dform_disp_split): the address is reg + disp; it differs from (u32)(reg + disp) only when the
// sum crosses 2^32, a guest fault either way.
#define BEJEWELED3_LOG_STORE_D_U8(r, d, y) (::bejeweled3::hot::LogBefore(base, (u32)((u32)(r) + (d)), 1), REX_STORE_D_U8(r, d, y))
#define BEJEWELED3_LOG_STORE_D_U16(r, d, y) (::bejeweled3::hot::LogBefore(base, (u32)((u32)(r) + (d)), 2), REX_STORE_D_U16(r, d, y))
#define BEJEWELED3_LOG_STORE_D_U32(r, d, y) (::bejeweled3::hot::LogBefore(base, (u32)((u32)(r) + (d)), 4), REX_STORE_D_U32(r, d, y))
#define BEJEWELED3_LOG_STORE_D_U64(r, d, y) (::bejeweled3::hot::LogBefore(base, (u32)((u32)(r) + (d)), 8), REX_STORE_D_U64(r, d, y))
#define BEJEWELED3_LOG_STORE_V128(ea, value)                                  \
  do {                                                                     \
    ::bejeweled3::hot::LogBefore(base, (u32)(ea), 16);                        \
    simde_mm_store_si128((simde__m128i*)REX_RAW_ADDR(ea), value);          \
  } while (0)
#define BEJEWELED3_LOG_ZERO(ea, n)                                            \
  do {                                                                     \
    ::bejeweled3::hot::LogBefore(base, (u32)(ea), n);                         \
    memset((void*)REX_RAW_ADDR(ea), 0, n);                                 \
  } while (0)
// A compiler-only barrier at every loop header of a localized copy: guest memory is read without volatile there, and
// a load must not be hoisted out of a loop (the Mushihimesama port's console hang). Registers in locals stay put.
#define BEJEWELED3_LOOP_BARRIER() __asm__ __volatile__("" ::: "memory")
