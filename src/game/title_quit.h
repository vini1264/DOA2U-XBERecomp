#pragma once

// Leaving the game from the title screen: B (or Esc) on "PRESS START" asks, A or START quits (title_quit.cpp).
namespace title_quit {
void Window(void* hwnd);  // GameInfo::window: the question's font (fonts/ui.ttf), before the atlas is built
void Ui();                // each frame from menu::Ui, UiLock held: the question while it is asked
}
