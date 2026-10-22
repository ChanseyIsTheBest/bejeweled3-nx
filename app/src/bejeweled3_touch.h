// Touch controls: the Switch's touch screen and the PC's mouse as the game's own touches (bejeweled3_touch.cpp).

#pragma once

#include <cstdint>

#include <rex/ppc/context.h>

namespace bejeweled3::touch {

enum class MouseKind { kDown, kMove, kUp };

// The PC window's left mouse button and pointer, as 0..1 of the window (called from the window's input thread).
void OnWindowMouse(MouseKind kind, float x, float y);

// Hands the touches queued since the previous frame to the game (on the Switch it polls the touch screen first).
// Called on the game's thread between frames, from the D3DDevice_Swap hook.
void Dispatch(PPCContext& ctx, uint8_t* base);

}  // namespace bejeweled3::touch
