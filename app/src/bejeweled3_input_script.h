// A scripted controller for unattended test runs (the window stays minimized, so nobody can press a button): with
// bejeweled3_input_script set, a synthetic pad is added to the input system and replays the script. Synthetic devices
// belong to player 1 and are merged into its state (buttons OR-ed, the larger stick), so a real pad still works.
//
// Script: comma-separated events "<when>:<input>[:<hold>]".
//   <when>   game frames since start (D3DDevice_Swap count, 30 per second), or seconds with an "s" suffix ("40s")
//   <input>  buttons joined by '+': A B X Y START BACK UP DOWN LEFT RIGHT LB RB LS RS, or an axis LX= LY= RX= RY=
//            (-32768..32767) or LT= RT= (0..255)
//   <hold>   how long, in game frames (default 4); an axis keeps its value for the hold, then returns to 0
// Example: bejeweled3_input_script = "40s:A,44s:A,50s:DOWN,52s:A,60s:LX=-20000:45,64s:A"

#pragma once

#include <cstdint>
#include <functional>
#include <memory>

namespace rex::system {
class IInputSystem;
}

namespace bejeweled3::input_script {

using InputFactory = std::function<std::unique_ptr<rex::system::IInputSystem>(bool tool_mode)>;

// Returns a factory that builds what `base` builds and adds the touch controls' pad (Press) and, when
// bejeweled3_input_script is set, the scripted pad.
InputFactory Wrap(InputFactory base);

// The touch controls' buttons (bejeweled3_touch.cpp): `buttons` (XInput bits) held on player 1 for `frames` game frames,
// merged with the real pads like the scripted pad; presses made at once follow each other, each released before the
// next. A tap on the title screen presses START this way, and a tap on a list dialog's entry the pad's way to it.
void Press(uint16_t buttons, uint32_t frames);

}  // namespace bejeweled3::input_script
