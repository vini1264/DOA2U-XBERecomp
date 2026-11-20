#pragma once
#include <string>

// Each player's input devices: player n is console port n, and reads one controller, the keyboard, both or neither.
// By default player 1 has the keyboard and controller 1, players 2 to 4 controllers 2 to 4. A controller or the
// keyboard belongs to one player at a time: giving it to another swaps what the two had. Kept in settings.toml's
// [players] table; changed in SETTINGS > RECOMP SETTINGS > INPUT and in the F10 menu.
namespace players {

constexpr int kPlayers = 4;
// the choices, in the order left and right step through them: KEYBOARD + CONTROLLER 1-4, CONTROLLER 1-4, KEYBOARD,
// NONE
constexpr int kChoices = 10;

int Choice(int player);
void SetChoice(int player, int choice);  // swaps with the player that had the controller or keyboard
void Step(int player, int dir);          // the next or previous choice
std::string ChoiceName(int choice);      // in capitals, as the game's menus write
bool Missing(int choice);                // its controller isn't plugged in
int ControllerOf(int choice);            // 0-3, -1 none
void ResetDefaults();

void Load(const void* table);  // GameInfo::load: [players], and the keyboard's [keyboard]
void Save(void* table);        // GameInfo::save
}  // namespace players
