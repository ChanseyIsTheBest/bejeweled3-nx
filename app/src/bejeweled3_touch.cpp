// Touch controls (the user's request, 2026-10-21: "being able to tap all the UI elements and being able to tap and
// swipe the gems to swap them"). The Switch's touch screen, and the mouse on the PC, become the game's own touches.
//
// Bejeweled 3's framework is PopCap's later SexyAppFramework, shared with its touch platforms; the Xbox 360 build
// still links its touch path (docs/bejeweled3-notes.md, "Touch controls"; out/touch/notes.txt):
//   - SexyAppBase's vtable slots 96 / 97 / 98 (0x823FE418 / 20 / 28) load mWidgetManager (+880) and jump to
//     WidgetManager::TouchBegan (sub_823FAAA0), TouchEnded (sub_823FACE8) and TouchMoved (sub_823FABD0);
//   - each takes a SexyTouch (+8 x, +12 y, +16 previous x, +20 previous y, in the game's coordinates), finds the
//     widget under it (GetWidgetAt, sub_823FA450), makes the point local to it and calls the widget's TouchBegan /
//     TouchMoved / TouchEnded (vtable slots 61 / 62 / 63). A widget that does not override them gets
//     MouseDown(x, y, 1) / MouseDrag(x, y) / MouseUp(x, y, 1): the PC game's mouse handling (buttons, dialogs, the
//     board's click-and-drag swaps).
// Nothing calls those three on the console. This file calls them on the game's own thread, between frames (from the
// D3DDevice_Swap hook, bejeweled3_frame_hooks.cpp), with the touches queued since the previous frame.
//
// The app object: SexyAppBase's constructor (sub_82406770: stores the SexyAppBase vtable 0x8204F00C and its second one
// at +4, then builds the members; called from SexyApp's, sub_824374A0) is hooked to keep `this`. (sub_82405E10, which
// also names that vtable, is the destructor.)

#include "bejeweled3_touch.h"

#include "bejeweled3_input_script.h"

#include <algorithm>
#include <atomic>
#include <bit>
#include <cstdint>
#include <cstring>
#include <deque>
#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <string>
#include <vector>

#include <fmt/format.h>

#include <rex/cvar.h>
#include <rex/hook.h>
#include <rex/logging.h>
#include <rex/system/kernel_state.h>
#include <rex/system/xmemory.h>

#if REX_PLATFORM_SWITCH
#include <switch.h>
#endif

REXCVAR_DEFINE_BOOL(bejeweled3_touch, true, "Bejeweled3",
                    "Touch controls: the Switch's touch screen (the mouse on the PC) taps the game's buttons and drags "
                    "its gems");
REXCVAR_DEFINE_INT32(bejeweled3_touch_width, 1280, "Bejeweled3",
                     "The game's coordinate width the touch screen (or window) is scaled to, when the game has not set "
                     "its mouse rectangle");
REXCVAR_DEFINE_INT32(bejeweled3_touch_height, 720, "Bejeweled3",
                     "The game's coordinate height the touch screen (or window) is scaled to, when the game has not set "
                     "its mouse rectangle");
REXCVAR_DEFINE_BOOL(bejeweled3_touch_log, false, "Bejeweled3", "Log every touch handed to the game");
REXCVAR_DEFINE_STRING(bejeweled3_touch_script, "", "Bejeweled3",
                      "Test runs: scripted touches, comma-separated \"<frame>:<down|move|up>:<x>:<y>\" in 1280x720 screen "
                      "pixels, frames counted by this file's dispatch (one a swap); empty = off");
REXCVAR_DEFINE_INT32(bejeweled3_autoplay, 0, "Bejeweled3",
                     "Test runs: every N frames a move on the board (the longest line it makes), tapped as a touch; "
                     "0 = off");
REXCVAR_DEFINE_INT32(bejeweled3_autoplay_color, -1, "Bejeweled3",
                     "Test runs: the byte offset of a piece's colour for bejeweled3_autoplay; -1 = log the pieces' small "
                     "integer and float fields instead (to find it)");

namespace {

constexpr uint32_t kAppWidgetManager = 880;  // SexyAppBase::mWidgetManager (the slot-96..98 thunks load it)

std::atomic<uint32_t> g_app{0};

// The board (2026-10-22, out/touch/notes.txt): its widget's vtable is 0x8227AB18 (MouseDown with a button,
// sub_82A1AE50; MouseDrag sub_82A10BF0). It holds its 8x8 pieces at +204 (row * 8 + column); a piece's column is at +8,
// its row at +0xC, and its byte +604 marks the selected piece (sub_82A0FC20 returns the first such). On the console the
// gamepad cursor's gem carries that flag, and the board's MouseDown takes a click as the second gem of a swap with
// the selected piece: a tap next to the cursor swapped with it, any other tap gave "Illegal Move: You must select two
// gems that are adjacent to each other". So a touch on the board first clears the flag when it was not set by a
// touch: the touch then selects its own gem, and a drag or a tap on a neighbour swaps.
// The other modes' boards are subclasses (Zen's vtable is 0x82284C60, `zen4`) with Board's MouseDown, pieces and flags:
// a board is known by that MouseDown in its vtable slot 53 (until 2026-10-22 06:50 only Classic's vtable was, and
// touches on the other boards went to the game's mouse code as they were).
constexpr uint32_t kBoardMouseDown = 0x82A1AE50;
// Zen's board shows "Press RB for Zen Options" in a box on its left (the game's x 20..290, y 440..560): a tap there
// presses RB.
constexpr uint32_t kZenBoardVtable = 0x82284C60;
constexpr int32_t kZenOptionsBox[4] = {20, 440, 290, 560};
// The Badges dialog (vtable 0x82278F28): twenty badges in five columns (three rows, then the bottom row), the
// highlighted one chosen by the pad (its index at +0x33C, row * 5 + column: 10 -> 11 on RIGHT, -> 16 on DOWN,
// `badges3`) and taps ignored. A tap on a badge presses the pad's way to it; the dialog then names it and says how to
// earn it. The badges' middles in the dialog were measured on a shot of `badges3`.
constexpr uint32_t kBadgesVtable = 0x82278F28;
constexpr uint32_t kBadgesIndex = 0x33C;
constexpr int32_t kBadgeColumnX[5] = {65, 188, 311, 434, 557};
constexpr int32_t kBadgeRowY[4] = {57, 157, 257, 377};
constexpr int32_t kBadgeHalfSize = 50;

bool IsBoard(const uint8_t* base, uint32_t widget);
// The title screen (2026-10-22): "PRESS START" waits for a pad's START, and its widget (vtable 0x8227F068, built by
// sub_829A3060; the main menu that follows is the same widget, its entries child buttons) ignores the mouse, so a tap
// there did nothing. A touch on that widget presses START on player 1 (input_script::Press). On the main menu, START
// does nothing (pad test `startmenu2`), so a tap beside its entries stays harmless.
constexpr uint32_t kTitleVtable = 0x8227F068;
// The mode screen's gems (vtable 0x8227B7A0): the centred gem starts its mode on a tap, a side gem ignored taps, and the
// pad turns the ring with LEFT / RIGHT (`touchui12`). A tap on a side gem turns the ring one step towards it.
constexpr uint32_t kModeGemVtable = 0x8227B7A0;
constexpr int32_t kScreenCentreX = 480;  // the game's 960-wide screen
constexpr int32_t kSideGemOffset = 100;  // the centred gem's middle is within this of the centre
constexpr uint32_t kBoardPieces = 204;
constexpr uint32_t kPieceSelected = 604;
uint32_t g_touch_selected = 0;  // the piece a touch selected and left selected (0 = none)
struct Swipe {
  bool active = false;  // a touch on the board is in progress (handled as taps, below)
  bool done = false;    // its swipe has tapped the neighbour
  uint32_t board = 0;
  int32_t x = 0, y = 0;  // where it began, in the game's coordinates
};
Swipe g_swipe;

// Clears the selection flags of every piece but `keep` (the game's cursor gem: the game flags it again every frame).
void ClearOtherSelections(uint8_t* base, uint32_t board, uint32_t keep) {
  for (uint32_t i = 0; i < 64; ++i) {
    uint32_t be;
    std::memcpy(&be, base + board + kBoardPieces + i * 4, 4);
    const uint32_t piece = __builtin_bswap32(be);
    if (piece && piece != keep && (base[piece + kPieceSelected] || base[piece + kPieceSelected + 1])) {
      base[piece + kPieceSelected] = 0;
      base[piece + kPieceSelected + 1] = 0;
    }
  }
}

uint32_t SelectedPiece(const uint8_t* base, uint32_t board) {
  for (uint32_t i = 0; i < 64; ++i) {
    uint32_t be;
    std::memcpy(&be, base + board + kBoardPieces + i * 4, 4);
    const uint32_t piece = __builtin_bswap32(be);
    if (piece && base[piece + kPieceSelected]) return piece;
  }
  return 0;
}

enum class Phase : uint8_t { kBegan, kMoved, kEnded };
struct Touch {
  Phase phase;
  float x, y;  // 0..1 of the screen / window
};

std::mutex g_mutex;
std::deque<Touch> g_queue;
bool g_down = false;          // a touch is in progress (as the game saw it)
int32_t g_last_x = 0, g_last_y = 0;

void Push(Phase phase, float x, float y) {
  if (!REXCVAR_GET(bejeweled3_touch)) return;
  x = std::clamp(x, 0.0f, 1.0f);
  y = std::clamp(y, 0.0f, 1.0f);
  std::lock_guard<std::mutex> lock(g_mutex);
  // Coalesce moves: only the newest position of a run of moves matters.
  if (phase == Phase::kMoved && !g_queue.empty() && g_queue.back().phase == Phase::kMoved) {
    g_queue.back().x = x;
    g_queue.back().y = y;
    return;
  }
  if (g_queue.size() < 64) g_queue.push_back({phase, x, y});
}

inline void Store32(uint8_t* base, uint32_t address, uint32_t value) {
  const uint32_t be = __builtin_bswap32(value);
  std::memcpy(base + address, &be, 4);
}
inline uint32_t Load32(const uint8_t* base, uint32_t address) {
  uint32_t be;
  std::memcpy(&be, base + address, 4);
  return __builtin_bswap32(be);
}

// Whether the guest has [address, address + size) mapped, for a word that may not hold a pointer.
bool Readable(uint32_t address, uint32_t size) {
  auto* kernel = rex::system::kernel_state();
  auto* memory = kernel ? kernel->memory() : nullptr;
  auto* heap = memory ? memory->LookupHeap(address) : nullptr;
  return heap && heap->QueryRangeAccess(address, address + size - 1) != rex::memory::PageAccess::kNoAccess;
}

bool IsBoard(const uint8_t* base, uint32_t widget) {
  return widget && Load32(base, Load32(base, widget) + 53 * 4) == kBoardMouseDown;
}

// The console's list dialogs (2026-10-22: Help & Options and the pause menu, two classes whose MouseDown is
// sub_82A41AA0): their entries are ButtonWidgets (vtable 0x8206010C) parked at x = -1000 and drawn by the dialog, which
// keeps them at +0x234 (up to five, the pause menu's) and the highlighted one at +0x248 (`touchui5`: a DOWN moved only
// that pointer); the pad moves the highlight and A picks it. The mouse never reaches them, so a tap on an entry did
// nothing; and the dialog picks its own highlight on A, whatever +0x248 says (`touchui8`). A tap on an entry's row (its
// y and height in the dialog, +0x30 / +0x38) presses DOWN or UP from the highlighted entry to it, then A.
constexpr uint32_t kListDialogMouseDown = 0x82A41AA0;  // vtable slot 53
constexpr uint32_t kButtonWidgetVtable = 0x8206010C;
constexpr uint32_t kListEntries = 0x234;
constexpr uint32_t kListSelected = 0x248;
constexpr int32_t kParkedX = -1000;

bool ParkedButton(const uint8_t* base, uint32_t widget) {
  return Readable(widget, 0x40) && Load32(base, widget) == kButtonWidgetVtable &&
         int32_t(Load32(base, widget + 0x2C)) == kParkedX;
}

// A list dialog's entry on the row of a point local to the dialog: how many entries it is below the highlighted one
// (negative: above). False when the widget is not a list dialog or no entry is on that row.
bool ListStepsTo(const uint8_t* base, uint32_t dialog, int32_t local_y, int32_t* steps) {
  if (Load32(base, Load32(base, dialog) + 53 * 4) != kListDialogMouseDown) return false;
  const uint32_t selected = Load32(base, dialog + kListSelected);
  if (!ParkedButton(base, selected)) return false;
  int32_t highlighted = -1, target = -1;
  for (uint32_t o = kListEntries, i = 0; o < kListSelected; o += 4, ++i) {
    const uint32_t entry = Load32(base, dialog + o);
    if (!ParkedButton(base, entry)) break;
    if (entry == selected) highlighted = int32_t(i);
    const int32_t top = int32_t(Load32(base, entry + 0x30)), height = int32_t(Load32(base, entry + 0x38));
    if (local_y >= top && local_y < top + height) target = int32_t(i);
  }
  if (highlighted < 0 || target < 0) return false;
  *steps = target - highlighted;
  return true;
}

// Test runs: bejeweled3_touch_script, parsed once; the events due at each dispatch are queued as if touched.
struct ScriptedTouch {
  uint64_t frame;
  Phase phase;
  float x, y;
};
void PollScript(uint64_t frame) {
  static std::vector<ScriptedTouch> script;
  static size_t next = 0;
  static bool parsed = false;
  if (!parsed) {
    parsed = true;
    const std::string text = REXCVAR_GET(bejeweled3_touch_script);
    size_t at = 0;
    while (at < text.size()) {
      size_t end = text.find(',', at);
      if (end == std::string::npos) end = text.size();
      const std::string item = text.substr(at, end - at);
      at = end + 1;
      unsigned long long when = 0;
      char kind[8] = {};
      float x = 0, y = 0;
      if (std::sscanf(item.c_str(), "%llu:%7[a-z]:%f:%f", &when, kind, &x, &y) == 4) {
        const std::string k = kind;
        const Phase phase = k == "down" ? Phase::kBegan : k == "move" ? Phase::kMoved : Phase::kEnded;
        script.push_back({when, phase, x / 1280.0f, y / 720.0f});
      } else {
        REXLOG_WARN("[touch] bejeweled3_touch_script: \"{}\" is not <frame>:<down|move|up>:<x>:<y>", item);
      }
    }
    if (!script.empty()) REXLOG_INFO("[touch] scripted touches: {}", script.size());
  }
  while (next < script.size() && script[next].frame <= frame) {
    const ScriptedTouch& t = script[next++];
    Push(t.phase, t.x, t.y);
  }
}

#if REX_PLATFORM_SWITCH
// The Switch: the touch screen is polled once a frame (1280x720 panel coordinates in handheld and docked mode alike;
// docked, nothing touches it).
void PollSwitch() {
  static bool ready = false;
  static bool was_down = false;
  if (!ready) {
    hidInitializeTouchScreen();
    ready = true;
  }
  static float last_x = 0.0f, last_y = 0.0f;
  HidTouchScreenState state{};
  const bool down = hidGetTouchScreenStates(&state, 1) != 0 && state.count > 0;
  if (down) {
    last_x = float(state.touches[0].x) / 1280.0f;
    last_y = float(state.touches[0].y) / 720.0f;
    Push(was_down ? Phase::kMoved : Phase::kBegan, last_x, last_y);
  } else if (was_down) {
    // Where the finger was last (2026-10-22): the release had (-1, -1) for "the last position", which Push clamps to
    // (0, 0), so every release on the console reached the game at the screen's corner (the user's log: "ended at
    // (-160, 0)" after every tap). The scripted touches of the tests carry their own release point and never showed it.
    Push(Phase::kEnded, last_x, last_y);
  }
  was_down = down;
}
#endif

}  // namespace

// SexyAppBase::SexyAppBase(): keep the app object.
extern "C" REX_FUNC(__imp__sub_82406770);
REX_HOOK_RAW(sub_82406770) {
  const uint32_t app = ctx.r3.u32;
  __imp__sub_82406770(ctx, base);
  g_app.store(app, std::memory_order_release);
  REXLOG_INFO("[touch] the app object is at 0x{:08X} (widget manager +{})", app, kAppWidgetManager);
}

extern "C" REX_FUNC(__imp__sub_823FA450);  // WidgetManager::GetWidgetAt(x, y, int* local_x, int* local_y)
extern "C" REX_FUNC(__imp__sub_823FAAA0);  // WidgetManager::TouchBegan(SexyTouch&)
extern "C" REX_FUNC(__imp__sub_823FABD0);  // WidgetManager::TouchMoved(SexyTouch&)
extern "C" REX_FUNC(__imp__sub_823FACE8);  // WidgetManager::TouchEnded(SexyTouch&)

// Test runs (bejeweled3_autoplay, off by default; 2026-10-22, the Lightning lockstep of the hand-written natives):
// every N frames a move on the board, the swap that makes the longest line (a five, then a four: the special gems and
// their effects, which random pad moves seldom make), tapped as a touch is: the gem, then its neighbour. It reads the
// pieces' colours from the game's state, so a run with the test seed and the frame clock plays the same moves again.
namespace {

constexpr int32_t kProbeX = 582, kProbeY = 380;  // a gem of the board in the game's coordinates (`light2`)

uint32_t WidgetAt(PPCContext& ctx, uint8_t* base, uint32_t manager, int32_t x, int32_t y, int32_t* lx, int32_t* ly) {
  PPCContext probe = ctx;
  const uint32_t sp = (ctx.r1.u32 - 0x400) & ~0xFu;
  const uint32_t out = sp + 0x280;
  probe.r1.u64 = sp;
  probe.r3.u64 = manager;
  probe.r4.u64 = uint32_t(x);
  probe.r5.u64 = uint32_t(y);
  probe.r6.u64 = out;
  probe.r7.u64 = out + 4;
  __imp__sub_823FA450(probe, base);
  *lx = int32_t(Load32(base, out));
  *ly = int32_t(Load32(base, out + 4));
  return probe.r3.u32;
}

// bejeweled3_autoplay_color = -1: the pieces' fields that could be a colour, column or row (all 64 small integers) or
// a position (all 64 plain floats), to find the offsets.
void DumpPieces(const uint8_t* base, uint32_t board, int32_t lx, int32_t ly) {
  static int dumps = 0;
  if (dumps >= 2) return;
  ++dumps;
  uint32_t pieces[64];
  for (uint32_t i = 0; i < 64; ++i) {
    pieces[i] = Load32(base, board + kBoardPieces + i * 4);
    if (!pieces[i]) return;
  }
  REXLOG_INFO("[autoplay] board 0x{:08X} at ({}, {}) size ({}, {}); the probe ({}, {}) is local ({}, {})", board,
              int32_t(Load32(base, board + 0x2C)), int32_t(Load32(base, board + 0x30)),
              int32_t(Load32(base, board + 0x34)), int32_t(Load32(base, board + 0x38)), kProbeX, kProbeY, lx, ly);
  for (uint32_t o = 0; o < 0x280; o += 4) {
    bool ints = true, floats = true, differ = false;
    std::string iv, fv;
    for (uint32_t i = 0; i < 64; ++i) {
      const uint32_t w = Load32(base, pieces[i] + o);
      const int32_t v = int32_t(w);
      const float f = std::bit_cast<float>(w);
      if (v < -1 || v > 63) ints = false;
      if (!(f > -200.0f && f < 1200.0f) || (w && (w & 0x7F800000u) == 0)) floats = false;
      if (w != Load32(base, pieces[0] + o)) differ = true;
      iv += fmt::format(" {}", v);
      fv += fmt::format(" {:.0f}", f);
    }
    if (!differ) continue;
    if (ints) REXLOG_INFO("[autoplay] int +{}:{}", o, iv);
    else if (floats) REXLOG_INFO("[autoplay] float +{}:{}", o, fv);
  }
}

// The length of the line through (r, c) (0 below three).
int LineAt(const int32_t g[8][8], int r, int c) {
  const int32_t color = g[r][c];
  if (color < 0) return 0;
  int h = 1, v = 1;
  for (int k = c - 1; k >= 0 && g[r][k] == color; --k) ++h;
  for (int k = c + 1; k < 8 && g[r][k] == color; ++k) ++h;
  for (int k = r - 1; k >= 0 && g[k][c] == color; --k) ++v;
  for (int k = r + 1; k < 8 && g[k][c] == color; ++k) ++v;
  return std::max(h >= 3 ? h : 0, v >= 3 ? v : 0);
}

void AutoPlay(PPCContext& ctx, uint8_t* base, uint64_t frame) {
  const int32_t period = REXCVAR_GET(bejeweled3_autoplay);
  if (period <= 0 || frame % uint64_t(period) != 0) return;
  const uint32_t app = g_app.load(std::memory_order_acquire);
  if (!app) return;
  const uint32_t manager = Load32(base, app + kAppWidgetManager);
  if (!manager) return;
  int32_t lx = 0, ly = 0;
  const uint32_t board = WidgetAt(ctx, base, manager, kProbeX, kProbeY, &lx, &ly);
  if (!IsBoard(base, board)) return;
  const int32_t color_offset = REXCVAR_GET(bejeweled3_autoplay_color);
  if (color_offset < 0) {
    DumpPieces(base, board, lx, ly);
    return;
  }
  (void)frame;
}

}  // namespace

namespace bejeweled3::touch {

void OnWindowMouse(MouseKind kind, float x, float y) {
  switch (kind) {
    case MouseKind::kDown: Push(Phase::kBegan, x, y); break;
    case MouseKind::kMove:
      if (g_down) Push(Phase::kMoved, x, y);
      break;
    case MouseKind::kUp: Push(Phase::kEnded, x, y); break;
  }
}

void Dispatch(PPCContext& ctx, uint8_t* base) {
  if (!REXCVAR_GET(bejeweled3_touch)) return;
  static uint64_t frame = 0;
  PollScript(++frame);
  AutoPlay(ctx, base, frame);
#if REX_PLATFORM_SWITCH
  PollSwitch();
#endif
  std::deque<Touch> touches;
  {
    std::lock_guard<std::mutex> lock(g_mutex);
    if (g_queue.empty()) return;
    touches.swap(g_queue);
  }
  const uint32_t app = g_app.load(std::memory_order_acquire);
  if (!app) return;
  const uint32_t manager = Load32(base, app + kAppWidgetManager);
  if (!manager) return;
  if (REXCVAR_GET(bejeweled3_touch_log)) {
    static bool dumped = false;
    if (!dumped) {
      dumped = true;
      std::string words;
      for (uint32_t o = 0; o < 0x100; o += 4) words += fmt::format(" {:X}:{}", o, int32_t(Load32(base, manager + o)));
      REXLOG_INFO("[touch] widget manager 0x{:08X}:{}", manager, words);
    }
  }
  // The screen in the game's coordinates: WidgetManager's mouse destination rectangle (x, y, width, height at +0xBC;
  // the source rectangle, the 1280x720 screen, is at +0xCC). Bejeweled 3's widgets live in 960x720 (the manager's
  // width and height, +0x34 / +0x38), centred: the rectangle is (-160, 0, 1280, 720), so a screen x is the game's
  // x + 160. The keys are the fallback when the rectangle is not set.
  int32_t left = int32_t(Load32(base, manager + 0xBC)), top = int32_t(Load32(base, manager + 0xC0));
  int32_t width = int32_t(Load32(base, manager + 0xC4)), height = int32_t(Load32(base, manager + 0xC8));
  if (width <= 0 || height <= 0 || width > 8192 || height > 8192) {
    left = top = 0;
    width = REXCVAR_GET(bejeweled3_touch_width);
    height = REXCVAR_GET(bejeweled3_touch_height);
  }
  // One touch to the game: a SexyTouch in a fresh stack frame below the caller's stack pointer (above the callee's
  // frame and its back-chain / LR save words), then WidgetManager's TouchBegan / TouchMoved / TouchEnded.
  auto send = [&](Phase phase, int32_t x, int32_t y) {
    PPCContext call = ctx;
    const uint32_t sp = (ctx.r1.u32 - 0x400) & ~0xFu;
    const uint32_t touch = sp + 0x200;
    Store32(base, touch + 0, 1);  // the touch's id
    Store32(base, touch + 4, 1);  // tap count
    Store32(base, touch + 8, uint32_t(x));
    Store32(base, touch + 12, uint32_t(y));
    Store32(base, touch + 16, uint32_t(g_down ? g_last_x : x));
    Store32(base, touch + 20, uint32_t(g_down ? g_last_y : y));
    for (uint32_t o = 24; o < 64; o += 4) Store32(base, touch + o, 0);
    call.r1.u64 = sp;
    call.r3.u64 = manager;
    call.r4.u64 = touch;
    if (REXCVAR_GET(bejeweled3_touch_log)) {
      REXLOG_INFO("[touch] {} at ({}, {})", phase == Phase::kBegan ? "began" : phase == Phase::kMoved ? "moved" : "ended",
                  x, y);
    }
    switch (phase) {
      case Phase::kBegan:
        __imp__sub_823FAAA0(call, base);
        g_down = true;
        break;
      case Phase::kMoved:
        __imp__sub_823FABD0(call, base);
        break;
      case Phase::kEnded:
        __imp__sub_823FACE8(call, base);
        g_down = false;
        break;
    }
    g_last_x = x;
    g_last_y = y;
  };
  // The widget under a point (WidgetManager::GetWidgetAt).
  auto widget_at = [&](int32_t x, int32_t y, int32_t* local_x = nullptr, int32_t* local_y = nullptr) -> uint32_t {
    PPCContext probe = ctx;
    const uint32_t sp = (ctx.r1.u32 - 0x400) & ~0xFu;
    const uint32_t out = sp + 0x280;
    probe.r1.u64 = sp;
    probe.r3.u64 = manager;
    probe.r4.u64 = uint32_t(x);
    probe.r5.u64 = uint32_t(y);
    probe.r6.u64 = out;
    probe.r7.u64 = out + 4;
    __imp__sub_823FA450(probe, base);
    const uint32_t widget = probe.r3.u32;
    if (local_x) *local_x = int32_t(Load32(base, out));
    if (local_y) *local_y = int32_t(Load32(base, out + 4));
    if (widget && REXCVAR_GET(bejeweled3_touch_log)) {
      const uint32_t vtable = Load32(base, widget);
      std::string slots;
      for (uint32_t k = 53; k <= 63; ++k) slots += fmt::format(" {}:{:08X}", k, Load32(base, vtable + k * 4));
      REXLOG_INFO("[touch] widget 0x{:08X} (vtable 0x{:08X}, local {} {}):{}", widget, vtable,
                  int32_t(Load32(base, out)), int32_t(Load32(base, out + 4)), slots);
    }
    return widget;
  };
  // The board under a point, or 0.
  auto board_at = [&](int32_t x, int32_t y) -> uint32_t {
    const uint32_t widget = widget_at(x, y);
    return IsBoard(base, widget) ? widget : 0;
  };
  // A tap on the board: only the touch's own earlier selection may stay flagged (the game flags its cursor's gem again
  // every frame, and the board's MouseDown takes the first flagged piece as the first gem of a swap). The tap then
  // selects its gem, or swaps it with the touch's selection when the two are neighbours.
  auto tap_board = [&](uint32_t board, int32_t x, int32_t y) {
    ClearOtherSelections(base, board, g_touch_selected);
    send(Phase::kBegan, x, y);
    send(Phase::kEnded, x, y);
    const uint32_t selected = SelectedPiece(base, board);
    // A selection the tap left (none after a swap: then the flagged piece, if any, is the game's cursor again).
    g_touch_selected = selected && base[selected + kPieceSelected + 1] ? selected : 0;
  };

  for (const Touch& t : touches) {
    int32_t x = g_last_x, y = g_last_y;
    if (t.x >= 0.0f) {
      x = left + std::min<int32_t>(int32_t(t.x * float(width)), width - 1);
      y = top + std::min<int32_t>(int32_t(t.y * float(height)), height - 1);
    }
    // Gems: the board's own drag handling (MouseDrag, sub_82A10BF0) swapped the touch's gem and then, as the game
    // flags its cursor's gem again every frame, that one too (the swipe tests: "Illegal Move: Each swap must create a
    // row of three"). A touch on the board is taps instead, which the board handles right (a tap, then a tap on a
    // neighbour, swapped and matched): the touch taps its gem at once, and a swipe of half a cell or more taps the
    // neighbour in its main direction (a cell is 77.6 of the screen's 720 lines); the rest of the swipe is ignored.
    if (g_swipe.active) {
      if (t.phase == Phase::kMoved && !g_swipe.done) {
        const int32_t dx = x - g_swipe.x, dy = y - g_swipe.y;
        constexpr int32_t kHalfCell = 39, kCell = 78;
        if (std::max(std::abs(dx), std::abs(dy)) >= kHalfCell) {
          const int32_t tx = std::abs(dx) > std::abs(dy) ? g_swipe.x + (dx > 0 ? kCell : -kCell) : g_swipe.x;
          const int32_t ty = std::abs(dx) > std::abs(dy) ? g_swipe.y : g_swipe.y + (dy > 0 ? kCell : -kCell);
          if (board_at(tx, ty) == g_swipe.board) tap_board(g_swipe.board, tx, ty);
          g_swipe.done = true;
        }
      } else if (t.phase == Phase::kEnded) {
        g_swipe.active = false;
      }
      continue;
    }
    if (t.phase == Phase::kBegan && g_down) continue;  // one touch at a time
    if (t.phase != Phase::kBegan && !g_down) continue;
    if (t.phase == Phase::kBegan) {
      int32_t local_x = 0, local_y = 0;
      const uint32_t widget = widget_at(x, y, &local_x, &local_y);
      const uint32_t vtable = widget ? Load32(base, widget) : 0;
      if (IsBoard(base, widget)) {
        if (vtable == kZenBoardVtable && local_x >= kZenOptionsBox[0] && local_y >= kZenOptionsBox[1] &&
            local_x < kZenOptionsBox[2] && local_y < kZenOptionsBox[3]) {
          bejeweled3::input_script::Press(0x0200 /* RB */, 4);
          if (REXCVAR_GET(bejeweled3_touch_log)) REXLOG_INFO("[touch] Zen options box: RB");
          continue;
        }
        tap_board(widget, x, y);
        g_swipe = {true, false, widget, x, y};
        continue;
      }
      if (vtable == kBadgesVtable) {
        int32_t column = -1, row = -1;
        for (int32_t i = 0; i < 5; ++i) {
          if (std::abs(local_x - kBadgeColumnX[i]) < kBadgeHalfSize) column = i;
        }
        for (int32_t i = 0; i < 4; ++i) {
          if (std::abs(local_y - kBadgeRowY[i]) < kBadgeHalfSize) row = i;
        }
        const uint32_t index = Load32(base, widget + kBadgesIndex);
        if (column >= 0 && row >= 0 && index < 20) {
          const int32_t dx = column - int32_t(index % 5), dy = row - int32_t(index / 5);
          for (int32_t k = 0; k < std::abs(dx); ++k) {
            bejeweled3::input_script::Press(dx > 0 ? 0x0008 /* RIGHT */ : 0x0004 /* LEFT */, 4);
          }
          for (int32_t k = 0; k < std::abs(dy); ++k) {
            bejeweled3::input_script::Press(dy > 0 ? 0x0002 /* DOWN */ : 0x0001 /* UP */, 4);
          }
          if (REXCVAR_GET(bejeweled3_touch_log)) {
            REXLOG_INFO("[touch] badge row {} column {} (from {})", row, column, index);
          }
          continue;
        }
      }
      if (vtable == kTitleVtable) {
        bejeweled3::input_script::Press(0x0010 /* START */, 4);
        if (REXCVAR_GET(bejeweled3_touch_log)) REXLOG_INFO("[touch] title screen: START");
      }
      if (vtable == kModeGemVtable) {
        const int32_t middle = x - local_x + int32_t(Load32(base, widget + 0x34)) / 2;
        if (std::abs(middle - kScreenCentreX) > kSideGemOffset) {
          bejeweled3::input_script::Press(middle > kScreenCentreX ? 0x0008 /* RIGHT */ : 0x0004 /* LEFT */, 4);
          if (REXCVAR_GET(bejeweled3_touch_log)) REXLOG_INFO("[touch] mode gem at {}: the ring turns", middle);
          continue;
        }
      }
      int32_t steps = 0;
      if (widget && ListStepsTo(base, widget, local_y, &steps)) {
        for (int32_t k = 0; k < std::abs(steps); ++k) {
          bejeweled3::input_script::Press(steps > 0 ? 0x0002 /* DOWN */ : 0x0001 /* UP */, 4);
        }
        bejeweled3::input_script::Press(0x1000 /* A */, 4);
        if (REXCVAR_GET(bejeweled3_touch_log)) {
          REXLOG_INFO("[touch] list dialog 0x{:08X}: row {}, {} step(s) from the highlighted entry, then A", widget,
                      local_y, steps);
        }
        continue;  // the dialog itself would only start a drag
      }
    }
    send(t.phase, x, y);
  }
}

}  // namespace bejeweled3::touch
