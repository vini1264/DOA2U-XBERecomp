#pragma once
#include <cstdint>
#include <string>

namespace input {
struct Pad;
}

namespace keyboard {

enum class Section { Movement, Fight, Menu };
constexpr int kSlots = 2;  // keys per action

int Count();
const char* Name(int action);  // as the game's menus write it, in capitals
const char* Help(int action);  // one line, for the help bar
Section SectionOf(int action);
int Key(int action, int slot);         // a Windows virtual-key code, 0 for none
std::string KeyName(int vk);           // "W", "ENTER", "LEFT SHIFT"; "-" for none
void Clear(int action, int slot);
void ResetDefaults();

// a binding waiting for its key: the next key down (Esc cancels, Delete clears) is taken by Message
void BeginCapture(int action, int slot);
bool Capturing();
int CaptureTarget();  // action * kSlots + slot, or -1

// GameInfo hooks: polled pad (through menu::Pad), settings file, window messages (through menu::Message)
void Apply(int port, input::Pad& pad);
void Load(const void* table);
void Save(void* table);
bool Message(uint32_t msg, uintptr_t wp, intptr_t lp, intptr_t* result);

void Suspend(bool suspended);  // the F10 menu is up: no keys reach the game
void SetPort(int port);        // the console port (player) the keyboard plays on, -1 none; port 0 at first
int Port();
uint16_t FightActions(int port);  // the fight action bits held now (1 Free, 2 Punch, 4 Kick), 0 off the keyboard's port

// changes not yet in the settings file; SaveIfChanged writes them (UiLock held or taken)
void SaveIfChanged();

void OpenUi();            // the F10 menu's mappings window
void Ui(bool menu_open);  // each frame from menu::Ui, UiLock held
}  // namespace keyboard
