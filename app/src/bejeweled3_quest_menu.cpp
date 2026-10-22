// The Quest menu's progress picture (2026-10-22, two crashes on the user's console, docs/bejeweled3-notes.md "A console
// crash in the Quest menu").
//
// The Quest menu (vtable 0x82281328; constructor sub_82973350, which loads "IMAGE_QUESTMENU_PROGRESS_%d_%d", destructor
// sub_82969228) keeps the picture it draws for the chosen relic at +1692: the constructor sets it from one of two arrays
// of pictures (+1628 and +1660), and sub_82968C40, which its Update (sub_8296F5F0) calls when a countdown at +1456 runs
// out, sets it to the +1660 array's entry for its index +320. Its Draw (sub_8296CA98) checks the pointer against 0 only
// and reads a float at +328 of it. Twice on the console the pointer was not a picture (0x00FFFFFF, then 0x0000000F) and
// the Draw faulted on guest 0x01000147 / 0x00000157, the menu itself still whole (its vtable called the Draw). Neither
// the PC nor Ryujinx reproduced it, so what wrote the value is not known yet.
//
// Before each Draw the pointer is checked: a picture is a mapped guest object whose first word is a vtable inside the
// image. When it is not one, the Draw gets the constructor's picture instead (+1628, else +1660; 0 when neither is one,
// which the Draw skips), and the log says so with the menu's index fields, for the next look.

#include <cstdint>
#include <cstring>

#include <rex/hook.h>
#include <rex/logging.h>
#include <rex/system/kernel_state.h>
#include <rex/system/xmemory.h>

namespace {

constexpr uint32_t kPicture = 1692;     // the drawn progress picture
constexpr uint32_t kPictures[2] = {1628, 1660};  // the constructor's arrays (first entries)
constexpr uint32_t kGroup = 316;        // the chosen relic
constexpr uint32_t kStage = 320;        // the index sub_82968C40 is called with (-1 from the constructor)
constexpr uint32_t kCountdown = 1456;   // frames until sub_82968C40 runs

uint32_t Load32(const uint8_t* base, uint32_t address) {
  uint32_t be;
  std::memcpy(&be, base + address, 4);
  return __builtin_bswap32(be);
}

void Store32(uint8_t* base, uint32_t address, uint32_t value) {
  const uint32_t be = __builtin_bswap32(value);
  std::memcpy(base + address, &be, 4);
}

bool Readable(uint32_t address, uint32_t size) {
  auto* kernel = rex::system::kernel_state();
  auto* memory = kernel ? kernel->memory() : nullptr;
  auto* heap = memory ? memory->LookupHeap(address) : nullptr;
  return heap && heap->QueryRangeAccess(address, address + size - 1) != rex::memory::PageAccess::kNoAccess;
}

// A picture object: aligned, mapped, and its first word a vtable inside the game's image.
bool IsPicture(const uint8_t* base, uint32_t object) {
  if (!object || (object & 3) || !Readable(object, 0x150)) return false;
  const uint32_t vtable = Load32(base, object);
  return vtable >= 0x82000000u && vtable < 0x83000000u;
}

}  // namespace

extern "C" REX_FUNC(__imp__sub_8296CA98);
REX_HOOK_RAW(sub_8296CA98) {  // the Quest menu's Draw (this r3, Graphics r4)
  const uint32_t menu = ctx.r3.u32;
  const uint32_t picture = Load32(base, menu + kPicture);
  if (picture && !IsPicture(base, picture)) {
    uint32_t replacement = 0;
    for (const uint32_t at : kPictures) {
      const uint32_t candidate = Load32(base, menu + at);
      if (IsPicture(base, candidate)) {
        replacement = candidate;
        break;
      }
    }
    static int logged = 0;
    if (logged < 8) {
      ++logged;
      REXLOG_WARN("[quest] the Quest menu 0x{:08X} had 0x{:08X} for its progress picture (relic {}, index {}, "
                  "countdown {}): drawn with 0x{:08X} instead",
                  menu, picture, int32_t(Load32(base, menu + kGroup)), int32_t(Load32(base, menu + kStage)),
                  int32_t(Load32(base, menu + kCountdown)), replacement);
    }
    Store32(base, menu + kPicture, replacement);
  }
  __imp__sub_8296CA98(ctx, base);
}
