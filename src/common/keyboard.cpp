// Keyboard as Dead or Alive 2's own input, bound to the game's actions.

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cfloat>
#include <cstdint>
#include <cstdio>
#include <imgui.h>
#include <mutex>
#include <string>
#include <toml++/toml.hpp>

#include "app/app.h"
#include "core/input.h"
#include "keyboard.h"

namespace keyboard {

namespace {

enum class Target { Dpad, Analog, Digital, Fight };

struct Action {
    const char* key;   // in the [keyboard] table
    const char* name;  // in the menus
    const char* help;
    Section section;
    Target target;
    int value;  // Dpad/Digital: pad buttons bit; Analog: analog index; Fight: action bits
    int defaults[kSlots];
};

// pad buttons bit: 0 up, 1 down, 2 left, 3 right, 4 start, 5 back, 6 left thumb, 7 right thumb
// pad analog index: 0 A, 1 B, 2 X, 3 Y, 4 black, 5 white, 6 left trigger, 7 right trigger
// fight action bits, as DOA2 makes them from the pad (0x11CF0): 1 Free, 2 Punch, 4 Kick
constexpr Action kActions[] = {
    {"up", "UP", "Up: jump in a fight, the cursor in menus.", Section::Movement, Target::Dpad, 0, {'W', VK_UP}},
    {"down", "DOWN", "Down: crouch in a fight, the cursor in menus.", Section::Movement, Target::Dpad, 1, {'S', VK_DOWN}},
    {"left", "LEFT", "Left: move in a fight, the cursor in menus.", Section::Movement, Target::Dpad, 2, {'A', VK_LEFT}},
    {"right", "RIGHT", "Right: move in a fight, the cursor in menus.", Section::Movement, Target::Dpad, 3, {'D', VK_RIGHT}},
    {"free", "FREE", "Free (hold): blocks and holds.", Section::Fight, Target::Fight, 1, {'J', 0}},
    {"punch", "PUNCH", "Punch.", Section::Fight, Target::Fight, 2, {'K', 0}},
    {"kick", "KICK", "Kick.", Section::Fight, Target::Fight, 4, {'L', 0}},
    {"throw", "THROW", "Throw: Free and Punch together.", Section::Fight, Target::Fight, 3, {'U', 0}},
    {"punch_kick", "PUNCH + KICK", "Punch and Kick together.", Section::Fight, Target::Fight, 6, {'I', 0}},
    {"free_kick", "FREE + KICK", "Free and Kick together.", Section::Fight, Target::Fight, 5, {'O', 0}},
    {"appeal", "FREE + PUNCH + KICK", "Free, Punch and Kick together: the appeal.", Section::Fight, Target::Fight, 7, {'P', 0}},
    {"confirm", "CONFIRM (A)", "The A button: confirms in menus.", Section::Menu, Target::Analog, 0, {VK_RETURN, 0}},
    {"cancel", "CANCEL (B)", "The B button: goes back in menus.", Section::Menu, Target::Analog, 1, {VK_BACK, 0}},
    {"start", "START", "The Start button: pauses, starts.", Section::Menu, Target::Digital, 4, {VK_SPACE, 0}},
    {"back_button", "BACK BUTTON", "The Back button.", Section::Menu, Target::Digital, 5, {0, 0}},
    {"x_button", "X BUTTON", "The X button, for menus that ask for it.", Section::Menu, Target::Analog, 2, {0, 0}},
    {"y_button", "Y BUTTON", "The Y button, for menus that ask for it.", Section::Menu, Target::Analog, 3, {0, 0}},
    {"black_button", "BLACK BUTTON", "The Black button.", Section::Menu, Target::Analog, 4, {0, 0}},
    {"white_button", "WHITE BUTTON", "The White button.", Section::Menu, Target::Analog, 5, {0, 0}},
    {"left_trigger", "LEFT TRIGGER", "The left trigger.", Section::Menu, Target::Analog, 6, {0, 0}},
    {"right_trigger", "RIGHT TRIGGER", "The right trigger.", Section::Menu, Target::Analog, 7, {0, 0}},
};
constexpr int kCount = int(sizeof(kActions) / sizeof(kActions[0]));

// read on the game's threads, written on the window thread (capture) and the UI
std::atomic<int> g_keys[kCount][kSlots];
std::atomic<int> g_capture{-1};
std::atomic<bool> g_dirty{false};
std::atomic<bool> g_suspended{false};
std::atomic<int> g_port{0};  // the console port the keyboard plays on, -1 none (players.cpp sets it)
std::atomic<uint32_t> g_capture_done{0};  // GetTickCount when the last capture ended: its key is still down
bool g_open = false;
int g_nav_hold = 0;

struct Init {
    Init() { ResetDefaults(); g_dirty = false; }
} g_init;

bool WindowHasFocus() {
    DWORD pid = 0;
    GetWindowThreadProcessId(GetForegroundWindow(), &pid);
    return pid == GetCurrentProcessId();
}

bool Valid(int vk) { return vk > 0 && vk < 256 && vk != VK_F10; }

void Bind(int action, int slot, int vk) {  // one key, one action
    for (int a = 0; a < kCount; a++)
        for (int s = 0; s < kSlots; s++)
            if (g_keys[a][s] == vk) g_keys[a][s] = 0;
    g_keys[action][slot] = vk;
    g_dirty = true;
}

bool Down(int action) {
    for (int s = 0; s < kSlots; s++)
        if (int vk = g_keys[action][s]; vk && (GetAsyncKeyState(vk) & 0x8000)) return true;
    return false;
}

// no keys for the game: another window has the focus, the F10 menu is up, a key is being bound, or one just was
bool Quiet() {
    if (g_suspended || g_capture >= 0 || !WindowHasFocus()) return true;
    return GetTickCount() - g_capture_done.load() < 250;
}

}  // namespace

int Count() { return kCount; }
const char* Name(int action) { return kActions[action].name; }
const char* Help(int action) { return kActions[action].help; }
Section SectionOf(int action) { return kActions[action].section; }
int Key(int action, int slot) { return g_keys[action][slot]; }

void Clear(int action, int slot) {
    g_keys[action][slot] = 0;
    g_dirty = true;
}

void ResetDefaults() {
    for (int a = 0; a < kCount; a++)
        for (int s = 0; s < kSlots; s++) g_keys[a][s] = kActions[a].defaults[s];
    g_dirty = true;
}

std::string KeyName(int vk) {
    if (!vk) return "-";
    switch (vk) {  // the ones GetKeyNameText names badly or needs the extended bit for
        case VK_UP: return "UP";
        case VK_DOWN: return "DOWN";
        case VK_LEFT: return "LEFT";
        case VK_RIGHT: return "RIGHT";
        case VK_RETURN: return "ENTER";
        case VK_BACK: return "BACKSPACE";
        case VK_SPACE: return "SPACE";
        case VK_ESCAPE: return "ESC";
        case VK_LSHIFT: return "LEFT SHIFT";
        case VK_RSHIFT: return "RIGHT SHIFT";
        case VK_LCONTROL: return "LEFT CTRL";
        case VK_RCONTROL: return "RIGHT CTRL";
        case VK_LMENU: return "LEFT ALT";
        case VK_RMENU: return "RIGHT ALT";
        case VK_INSERT: return "INSERT";
        case VK_DELETE: return "DELETE";
        case VK_HOME: return "HOME";
        case VK_END: return "END";
        case VK_PRIOR: return "PAGE UP";
        case VK_NEXT: return "PAGE DOWN";
        case VK_DIVIDE: return "NUM /";
        case VK_LWIN: return "LEFT WIN";
        case VK_RWIN: return "RIGHT WIN";
        case VK_APPS: return "MENU";
    }
    std::string name;
    if (UINT scan = MapVirtualKeyW(UINT(vk), MAPVK_VK_TO_VSC)) {
        wchar_t text[64];
        if (int n = GetKeyNameTextW(LONG(scan << 16), text, 64); n > 0)
            for (int i = 0; i < n; i++) name += text[i] < 128 ? char(text[i]) : '?';  // the game's font is ASCII
    }
    if (name.empty()) {
        char text[16];
        std::snprintf(text, sizeof(text), "KEY %d", vk);
        name = text;
    }
    for (char& c : name) c = char(std::toupper(uint8_t(c)));
    return name;
}

void BeginCapture(int action, int slot) {
    if (action >= 0 && action < kCount && slot >= 0 && slot < kSlots) g_capture = action * kSlots + slot;
}
bool Capturing() { return g_capture >= 0; }
int CaptureTarget() { return g_capture; }

void Suspend(bool suspended) { g_suspended = suspended; }
void SetPort(int port) { g_port = port >= 0 && port < 4 ? port : -1; }
int Port() { return g_port; }

void Apply(int port, input::Pad& pad) {
    if (port != g_port) return;
    pad.connected = true;  // the keyboard is always there
    if (Quiet()) return;
    for (int a = 0; a < kCount; a++) {
        const Action& k = kActions[a];
        if (k.target == Target::Fight || !Down(a)) continue;
        if (k.target == Target::Analog) pad.analog[k.value] = 0xFF;
        else pad.buttons |= uint16_t(1u << k.value);
    }
}

uint16_t FightActions(int port) {
    if (port != g_port || Quiet()) return 0;
    uint16_t bits = 0;
    for (int a = 0; a < kCount; a++)
        if (kActions[a].target == Target::Fight && Down(a)) bits |= uint16_t(kActions[a].value);
    return bits;
}

void Load(const void* table) {
    const toml::table& t = *static_cast<const toml::table*>(table);
    const toml::table* kb = t["keyboard"].as_table();
    if (kb) {
        for (int a = 0; a < kCount; a++) {
            const toml::array* keys = (*kb)[kActions[a].key].as_array();
            if (!keys) continue;  // an action the file doesn't name keeps its default
            for (int s = 0; s < kSlots; s++) {
                int vk = s < int(keys->size()) ? int((*keys)[size_t(s)].value_or(int64_t(0))) : 0;
                g_keys[a][s] = Valid(vk) ? vk : 0;
            }
        }
    }
    g_dirty = false;
}

void Save(void* table) {
    toml::table& t = *static_cast<toml::table*>(table);
    toml::table kb;
    for (int a = 0; a < kCount; a++) {
        toml::array keys;
        for (int s = 0; s < kSlots; s++)
            if (int vk = g_keys[a][s]) keys.push_back(int64_t(vk));
        kb.insert(kActions[a].key, std::move(keys));
    }
    t.insert_or_assign("keyboard", std::move(kb));
    g_dirty = false;
}

void SaveIfChanged() {
    if (!g_dirty) return;
    std::lock_guard<std::recursive_mutex> lock(app::UiLock());
    app::Save();
}

bool Message(uint32_t msg, uintptr_t wp, intptr_t lp, intptr_t* result) {
    const int target = g_capture;
    if (target < 0) return false;
    if (msg == WM_KEYUP || msg == WM_SYSKEYUP) {
        if (wp == VK_F10) return false;
        *result = 0;
        return true;
    }
    if (msg != WM_KEYDOWN && msg != WM_SYSKEYDOWN) return false;
    if (lp & (1 << 30)) {  // a repeat of a key already down, as the one that started the capture
        *result = 0;
        return true;
    }
    int vk = int(wp);
    if (vk == VK_F10) return false;  // the menu's own key still works
    if (vk == VK_ESCAPE) {
        // nothing changes
    } else if (vk == VK_DELETE) {
        Clear(target / kSlots, target % kSlots);
    } else {
        // Shift, Ctrl and Alt come as one code for both sides: the scan code and the extended bit tell them apart
        const UINT scan = UINT(lp >> 16) & 0xFF;
        const bool extended = (lp >> 24) & 1;
        if (vk == VK_SHIFT) vk = int(MapVirtualKeyW(scan, MAPVK_VSC_TO_VK_EX));
        else if (vk == VK_CONTROL) vk = extended ? VK_RCONTROL : VK_LCONTROL;
        else if (vk == VK_MENU) vk = extended ? VK_RMENU : VK_LMENU;
        if (Valid(vk)) Bind(target / kSlots, target % kSlots, vk);
    }
    g_capture_done = GetTickCount();
    g_capture = -1;
    *result = 0;
    return true;
}

// ---- the F10 menu's window ----------------------------------------------------------------------------------

void OpenUi() { g_open = true; }

void Ui(bool menu_open) {
    ImGuiIO& io = ImGui::GetIO();
    // keys pressed for a binding would also move ImGui's focus and press its buttons
    if (g_capture >= 0) g_nav_hold = 10;
    if (g_nav_hold > 0) {
        g_nav_hold--;
        io.ConfigFlags &= ~ImGuiConfigFlags_NavEnableKeyboard;
    } else {
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    }
    if (!g_open) return;
    if (!menu_open) {  // F10 closed the menu with this window up
        g_open = false;
        g_capture = -1;
        SaveIfChanged();
        return;
    }

    ImGui::SetNextWindowPos(ImVec2(440, 24), ImGuiCond_FirstUseEver);
    bool open = true;
    if (ImGui::Begin("Keyboard mappings", &open, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextDisabled("Click a slot and press a key: Esc cancels, Delete clears. Right click clears too.");
        const int capture = g_capture;
        static const char* const kSectionNames[] = {"Movement", "Fight", "Menus and buttons"};
        int section = -1;
        if (ImGui::BeginTable("bindings", 3, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV)) {
            ImGui::TableSetupColumn("Action", ImGuiTableColumnFlags_WidthFixed, 170);
            ImGui::TableSetupColumn("Key 1", ImGuiTableColumnFlags_WidthFixed, 120);
            ImGui::TableSetupColumn("Key 2", ImGuiTableColumnFlags_WidthFixed, 120);
            ImGui::TableHeadersRow();
            for (int a = 0; a < kCount; a++) {
                if (int(kActions[a].section) != section) {
                    section = int(kActions[a].section);
                    ImGui::TableNextRow();
                    ImGui::TableSetColumnIndex(0);
                    ImGui::TextDisabled("%s", kSectionNames[section]);
                }
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::AlignTextToFramePadding();
                ImGui::TextUnformatted(kActions[a].name);
                for (int s = 0; s < kSlots; s++) {
                    ImGui::TableSetColumnIndex(1 + s);
                    const int id = a * kSlots + s;
                    std::string label = capture == id ? "Press a key..." : KeyName(g_keys[a][s]);
                    label += "##" + std::to_string(id);
                    if (ImGui::Button(label.c_str(), ImVec2(-FLT_MIN, 0)) && g_nav_hold == 0)
                        g_capture = capture == id ? -1 : id;
                    if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) {
                        Clear(a, s);
                        if (capture == id) g_capture = -1;
                    }
                }
            }
            ImGui::EndTable();
        }
        if (ImGui::Button("Reset to defaults")) {
            ResetDefaults();
            g_capture = -1;
        }
        ImGui::SameLine();
        if (ImGui::Button("Close")) open = false;
    }
    ImGui::End();
    if (!open) {
        g_open = false;
        g_capture = -1;
        SaveIfChanged();
    }
}

}  // namespace keyboard
