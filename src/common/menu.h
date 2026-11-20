#pragma once
#include <cstdint>

namespace input {
struct Pad;
}

// the F10 settings menu (Dear ImGui); hooked into app::kGame in game_info.cpp
namespace menu {
bool Message(void* hwnd, uint32_t msg, uintptr_t wp, intptr_t lp, intptr_t* result);  // window thread: F10 opens and closes
void Ui(double fps);                                                                    // each frame, UiLock held
void Pad(int port, input::Pad& pad);  // the game sees a neutral pad while the menu is open
}  // namespace menu
