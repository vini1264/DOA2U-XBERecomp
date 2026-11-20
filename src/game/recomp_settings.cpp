// "RECOMP SETTINGS" as an eighth item of the SETTINGS menu, between WARNING and EXIT, drawn with the game's own text routine.

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

#include "app/app.h"
#include "audio/audio.h"
#include "kernel/memory.h"
#include "xdk/hle.h"
#include "../common/keyboard.h"
#include "../common/players.h"

using namespace kernel;

X86_EXTERN_FUNC(__imp__sub_0011AC90);
X86_EXTERN_FUNC(__imp__sub_0011CFC0);
X86_EXTERN_FUNC(__imp__sub_00095A70);
X86_EXTERN_FUNC(__imp__sub_00097830);
X86_EXTERN_FUNC(__imp__sub_00091560);
X86_EXTERN_FUNC(__imp__sub_000976F0);

namespace {

// ---- the game ---------------------------------------------------------------------------------------------------

constexpr uint32_t kCursors = 0x00DF4AC8;  // word per level
constexpr uint32_t kLevel = 0x00DF4AD4;
constexpr uint32_t kMenu = 0x00DF4AD8;
constexpr uint32_t kHelpBusy = 0x00884760;  // dword: the help line waits while not 0
constexpr uint32_t kSceneState = 0x008840BC;  // word: 2 while the menu runs

constexpr uint32_t kMoveCursor = 0x0011B8D0;  // (short* cursor, short count)
constexpr uint32_t kBackPressed = 0x0011BCA0;  // (short cursor, short count): B, or A over the last item at level 0
constexpr uint32_t kAPressed = 0x0011BDA0;  // (bool sound)
constexpr uint32_t kLeaveSound = 0x00134950;
constexpr uint32_t kLeave = 0x00020A70;
constexpr uint32_t kMenuSound = 0x00134F50;
constexpr uint32_t kControllerSlot = 0x0011CF20;
constexpr uint32_t kSound = 0x000FE450;  // (id, 0)
constexpr uint32_t kUp = 0x0011B5D0, kDown = 0x0011B690, kLeft = 0x0011B750, kRight = 0x0011B810;
constexpr uint32_t kGate0 = 0x00134840, kGate1 = 0x00091E20, kGate2 = 0x0009B7B0, kGate3 = 0x0009B7C0;
constexpr uint32_t kBar = 0x000976F0;  // (x, y, w, h, colors[4], 4, 0)
constexpr uint32_t kText = 0x000978E0;
constexpr uint32_t kMessage = 0x0006A3E0;  // (id, 0xFF) -> UTF-16 text
constexpr uint32_t kSwprintf = 0x002B8FCB;
constexpr uint32_t kButtonsFormat = 0x004151C8;
constexpr uint32_t kHelpLine = 0x000A0920;  // (0, 1, text, buttons, 0, 0)

constexpr int kGameItems = 7;  // BATTLE ... EXIT
constexpr int kOurItem = kGameItems;  // the cursor value of RECOMP SETTINGS
constexpr uint32_t kLabelX = 0xF0, kGlowX = 0xF6;  // the label sprites, as the game draws them

// layout (720x480 screen)
constexpr int kBarY0 = 0x5C, kRowStep = 0x1A, kBarW = 0x21E, kBarH = 0x16;
// On screen the new item takes EXIT's row (the seventh) and EXIT moves down to an eighth: the game's cursor values
// stay as they are (0..5 its items, 6 EXIT, 7 ours), only the order shown and walked is ours.
constexpr int kExitCursor = kGameItems - 1;
constexpr int kItemX = 0xF0, kItemY = kBarY0 + kExitCursor * kRowStep + 4;
constexpr uint32_t kExitLabelY = kExitCursor * kRowStep + 0x5D, kExitGlowY = kExitCursor * kRowStep + 0x56;
constexpr uint32_t kLineX = 0xFD, kLineW = 2;  // the vertical line right of the labels, 7 rows long
// the open list: names end where the labels end, values right of the line at x 0xFD
constexpr int kNameX = kItemX, kValueX = 0x1D0, kRowY0 = kBarY0 + 4;
constexpr float kScale = 0.85f;  // as the submenu lists
constexpr float kBright = 1.0f, kDim = 0.5f;

uint32_t Call(X86Context& ctx, uint32_t fn, std::initializer_list<uint32_t> args = {}) {
    return CallGuest(ctx, fn, args);
}
uint32_t F(float v) {
    uint32_t u;
    std::memcpy(&u, &v, 4);
    return u;
}
float AsFloat(uint32_t u) {
    float v;
    std::memcpy(&v, &u, 4);
    return v;
}

// UTF-16 strings in guest memory, 128 characters a slot
constexpr int kSlots = 48, kSlotChars = 128;
uint32_t Scratch() {
    static uint32_t at = mem::AllocSystem(kSlots * kSlotChars * 2 + 16);
    return at;
}
uint32_t Put(int slot, const std::string& text) {
    const uint32_t at = Scratch() + uint32_t(slot) * kSlotChars * 2;
    uint16_t* out = static_cast<uint16_t*>(HostPtr(at));
    size_t n = std::min<size_t>(text.size(), kSlotChars - 1);
    for (size_t i = 0; i < n; i++) out[i] = uint8_t(text[i]);
    out[n] = 0;
    return at;
}
uint32_t Row() {  // a word for the game's cursor routine, holding the row on screen
    static uint32_t at = mem::AllocSystem(16);
    return at;
}
int RowOf(int cursor) { return cursor == kOurItem ? kExitCursor : cursor == kExitCursor ? kOurItem : cursor; }
uint32_t Colors() {  // the bars' four corner colors
    static uint32_t at = mem::AllocSystem(16);
    return at;
}

bool g_open = false;   // the settings list is up
bool g_drawing = false;  // inside the scene's draw
int g_row = 0;
// the list's pages: 0 the settings; 1 INPUT, the players' devices and KEYBOARD KEYS; 2 KEYBOARD KEYS, a row per
// keyboard action and RESET TO DEFAULTS, kVisible rows at a time
int g_page = 0;
int g_input_row = 0;
int g_key_row = 0, g_key_slot = 0, g_key_top = 0;
int g_hold = 0;  // frames the list ignores the pad after opening, so the A that opened it changes nothing

bool AtTop() { return R16(kLevel) == 0 && R16(kMenu) == 0; }
bool OnOurItem() { return AtTop() && int16_t(R16(kCursors)) == kOurItem; }

// ---- the settings -----------------------------------------------------------------------------------------------

constexpr int kRows = 10;  // the eight settings, INPUT and CHARACTERS
constexpr int kInputRow = 8;
constexpr int kCharactersRow = 9;
constexpr int kInputRows = players::kPlayers + 1;  // PLAYER 1-4, KEYBOARD KEYS
constexpr int kVisible = 9;  // rows on screen, on bars 0 to 8
constexpr const char* kLabels[kRows] = {"DISPLAY MODE", "RESOLUTION",  "VSYNC",        "FRAME RATE",
                                        "RENDER SCALE", "ANISOTROPIC", "AUDIO OUTPUT", "CUSTOM TEXTURES",
                                        "INPUT",        "CHARACTERS"};
constexpr const char* kHelp[kRows] = {
    "Fullscreen, borderless or a window.",
    "Output resolution. DESKTOP is the monitor's own.",
    "Wait for the monitor's refresh, no tearing.",
    "60 FPS as the game, the monitor's rate, or no limit.",
    "Resolution the game is drawn at, 25% to 400%.",
    "Sharper textures seen at an angle.",
    "Speaker layout of the sound.",
    "Images in textures\\replacement replace the game's.",
    "Each player's controller or keyboard, and the keyboard's keys.",
    "A unlocks Bayman, Hitomi and Tengu, saved with the game data.",
};
constexpr const char* kItemHelp = "Display, graphics and audio settings of the PC version.";
constexpr uint32_t kAniso[] = {0, 2, 4, 8, 16};

using Size = std::pair<uint32_t, uint32_t>;
std::vector<Size> g_sizes;  // what RESOLUTION cycles through; {0, 0} is the desktop's own mode

// The modes this monitor lists, and the usual 4:3 sizes. Fullscreen only takes listed modes; a window or the
// borderless output takes any size up to the desktop's.
std::vector<Size> BuildSizes(app::DisplayMode mode) {
    std::vector<Size> listed;
    HMONITOR monitor = MonitorFromWindow(HWND(app::Window()), MONITOR_DEFAULTTOPRIMARY);
    MONITORINFOEXW info{};
    info.cbSize = sizeof(info);
    if (GetMonitorInfoW(monitor, &info)) {
        DEVMODEW dm{};
        dm.dmSize = sizeof(dm);
        for (DWORD i = 0; EnumDisplaySettingsW(info.szDevice, i, &dm); i++)
            if (dm.dmBitsPerPel >= 32 && dm.dmPelsWidth >= 640 && dm.dmPelsHeight >= 480)
                listed.push_back({dm.dmPelsWidth, dm.dmPelsHeight});
    }
    const app::Mode desktop = app::Desktop();
    std::vector<Size> sizes;
    if (mode == app::DisplayMode::Fullscreen) {
        sizes = listed;
    } else {
        for (const Size& s : listed)
            if (s.first <= desktop.width && s.second <= desktop.height) sizes.push_back(s);
        static const Size k43[] = {{640, 480},   {800, 600},   {1024, 768},  {1152, 864}, {1280, 960},
                                   {1400, 1050}, {1600, 1200}, {1920, 1440}, {2048, 1536}};
        for (const Size& s : k43)
            if (s.first <= desktop.width && s.second <= desktop.height) sizes.push_back(s);
    }
    std::sort(sizes.begin(), sizes.end(), [](const Size& a, const Size& b) {
        const uint64_t pa = uint64_t(a.first) * a.second, pb = uint64_t(b.first) * b.second;
        return pa != pb ? pa < pb : a.first < b.first;
    });
    sizes.erase(std::unique(sizes.begin(), sizes.end()), sizes.end());
    sizes.erase(std::remove(sizes.begin(), sizes.end(), Size{desktop.width, desktop.height}), sizes.end());
    sizes.insert(sizes.begin(), Size{0, 0});
    return sizes;
}

// The hidden characters' flags, in the game data the player's profile keeps (at [0x87CFA8], 0x2E48 into it): Tengu
// (story cleared with everyone), Bayman (story with Leon) and Hitomi (story with Ein, and a Dead or Alive 3 or Xtreme
// Beach Volleyball save on the hard disk). The game sets them itself as it unlocks them (0xDC100), and saves them with
// the profile.
constexpr uint32_t kGameData = 0x0087CFA8;
constexpr uint32_t kUnlockFlags[] = {0x2ED2, 0x2ED3, 0x2ED4};  // Tengu, Bayman, Hitomi
int g_unlock_hold = 0;  // frames "UNLOCKED!" stays

int UnlockedCount() {
    const uint32_t data = R32(kGameData);
    if (!data) return -1;
    int n = 0;
    for (uint32_t f : kUnlockFlags) n += G<uint8_t>(data + f) != 0;
    return n;
}

bool UnlockCharacters() {
    const uint32_t data = R32(kGameData);
    if (!data) return false;
    for (uint32_t f : kUnlockFlags) G<uint8_t>(data + f) = 1;
    g_unlock_hold = 90;
    return true;
}

std::string Value(int row) {
    std::lock_guard<std::recursive_mutex> lock(app::UiLock());
    const app::Settings& s = app::Get();
    switch (row) {
        case 0: {
            static const char* const modes[] = {"FULLSCREEN", "BORDERLESS", "WINDOWED"};
            return modes[std::min(int(s.display_mode), 2)];
        }
        case 1: {
            if (!s.width || !s.height) return "DESKTOP";
            std::string text = std::to_string(s.width) + "X" + std::to_string(s.height);
            if (uint64_t(s.width) * 3 == uint64_t(s.height) * 4) text += " 4:3";
            return text;
        }
        case 2: return s.vsync ? "ON" : "OFF";
        case 3:
            return s.fps_cap == 60 ? "60 FPS" : s.fps_cap == app::kFpsCapRefresh ? "MONITOR" : s.fps_cap == 0 ? "UNLIMITED" : "CUSTOM";
        case 4: return std::to_string(s.render_percent) + "%";
        case 5: return s.aniso == 0 ? "OFF" : std::to_string(s.aniso) + "X";
        case 6: {
            static const char* const outputs[] = {"AUTO", "STEREO", "SURROUND", "MONO"};
            return outputs[std::min<uint32_t>(s.audio_output, 3)];
        }
        case 7: return s.custom_textures ? "ON" : "OFF";
        case 8: return "OPEN";
        case 9: {
            const int n = UnlockedCount();
            if (n < 0) return "-";
            if (n == 3) return g_unlock_hold > 0 ? "UNLOCKED!" : "ALL UNLOCKED";
            return "UNLOCK (" + std::to_string(n) + "/3)";
        }
    }
    return "";
}

void Change(int row, int dir) {
    std::lock_guard<std::recursive_mutex> lock(app::UiLock());
    app::Settings& s = app::Get();
    switch (row) {
        case 0:
            s.display_mode = app::DisplayMode((int(s.display_mode) + dir + 3) % 3);
            g_sizes = BuildSizes(s.display_mode);
            if (std::find(g_sizes.begin(), g_sizes.end(), Size{s.width, s.height}) == g_sizes.end()) s.width = s.height = 0;
            app::ApplyDisplay();
            break;
        case 1: {
            if (g_sizes.empty()) g_sizes = BuildSizes(s.display_mode);
            auto at = std::find(g_sizes.begin(), g_sizes.end(), Size{s.width, s.height});
            int i = at == g_sizes.end() ? 0 : int(at - g_sizes.begin());
            const int n = int(g_sizes.size());
            i = (i + dir + n) % n;
            s.width = g_sizes[i].first;
            s.height = g_sizes[i].second;
            app::ApplyDisplay();
            break;
        }
        case 2: s.vsync = !s.vsync; break;
        case 3: {
            int i = s.fps_cap == app::kFpsCapRefresh ? 1 : s.fps_cap == 0 ? 2 : 0;
            i = (i + dir + 3) % 3;
            s.fps_cap = i == 0 ? 60 : i == 1 ? app::kFpsCapRefresh : 0;
            break;
        }
        case 4: s.render_percent = uint32_t(std::clamp(int(s.render_percent) + 25 * dir, 25, 400)); break;
        case 5: {
            int i = 0;
            for (int k = 0; k < 5; k++)
                if (s.aniso == kAniso[k]) i = k;
            s.aniso = kAniso[(i + dir + 5) % 5];
            break;
        }
        case 6:
            s.audio_output = uint32_t((int(s.audio_output) + dir + 4) % 4);
            audio::SetOutputMode(s.audio_output);
            break;
        case 7: s.custom_textures = !s.custom_textures; break;
    }
}

void Open() {
    {
        std::lock_guard<std::recursive_mutex> lock(app::UiLock());
        g_sizes = BuildSizes(app::Get().display_mode);  // the monitor may have changed since the last time
    }
    g_row = 0;
    g_hold = 8;
    g_page = 0;
    g_open = true;
}

void Close() {
    g_open = false;
    g_page = 0;
    std::lock_guard<std::recursive_mutex> lock(app::UiLock());
    app::Save();
}

// ---- input ------------------------------------------------------------------------------------------------------

bool InputAllowed(X86Context& ctx) {  // the checks each of the game's pad helpers makes first
    return Call(ctx, kGate0) != 3 && Call(ctx, kGate1) != 0 && Call(ctx, kGate2) == 0 && Call(ctx, kGate3) == 0;
}

// the KEYBOARD page: up and down pick an action, left and right its first or second key, A waits for the key
// (keyboard::Message takes it), B goes back to the settings
void KeysInput(X86Context& ctx) {
    const int rows = keyboard::Count() + 1;  // and RESET TO DEFAULTS
    if (keyboard::Capturing()) return;
    if (Call(ctx, kBackPressed, {0, uint32_t(rows)}) & 0xFF) {
        g_page = 1;
        g_hold = 4;
        keyboard::SaveIfChanged();
        return;
    }
    if (Call(ctx, kUp)) {
        g_key_row = (g_key_row + rows - 1) % rows;
        Call(ctx, kSound, {0x23, 0});
    } else if (Call(ctx, kDown)) {
        g_key_row = (g_key_row + 1) % rows;
        Call(ctx, kSound, {0x23, 0});
    } else if (Call(ctx, kLeft) || Call(ctx, kRight)) {
        g_key_slot ^= 1;
        Call(ctx, kSound, {0x23, 0});
    } else if (Call(ctx, kAPressed, {1}) & 0xFF) {
        if (g_key_row < keyboard::Count()) keyboard::BeginCapture(g_key_row, g_key_slot);
        else keyboard::ResetDefaults();
    }
    if (g_key_row < g_key_top) g_key_top = g_key_row;
    if (g_key_row >= g_key_top + kVisible) g_key_top = g_key_row - kVisible + 1;
}

// the INPUT page: left and right (or A) step a player's devices; A on KEYBOARD KEYS opens it; B goes back
void InputPageInput(X86Context& ctx) {
    if (Call(ctx, kBackPressed, {0, uint32_t(kInputRows)}) & 0xFF) {
        g_page = 0;
        g_hold = 4;
        return;
    }
    const bool player = g_input_row < players::kPlayers;
    if (Call(ctx, kUp)) {
        g_input_row = (g_input_row + kInputRows - 1) % kInputRows;
        Call(ctx, kSound, {0x23, 0});
    } else if (Call(ctx, kDown)) {
        g_input_row = (g_input_row + 1) % kInputRows;
        Call(ctx, kSound, {0x23, 0});
    } else if (player && Call(ctx, kLeft)) {
        players::Step(g_input_row, -1);
        Call(ctx, kSound, {0x23, 0});
    } else if (player && (Call(ctx, kRight) || (Call(ctx, kAPressed, {0}) & 0xFF))) {
        players::Step(g_input_row, 1);
        Call(ctx, kSound, {0x23, 0});
    } else if (!player && (Call(ctx, kAPressed, {1}) & 0xFF)) {
        g_page = 2;
        g_key_row = g_key_slot = g_key_top = 0;
        g_hold = 4;
    }
}

void ListInput(X86Context& ctx) {
    if (g_hold > 0) {
        g_hold--;
        return;
    }
    if (!InputAllowed(ctx)) return;
    if (g_page == 2) {
        KeysInput(ctx);
        return;
    }
    if (g_page == 1) {
        InputPageInput(ctx);
        return;
    }
    if (Call(ctx, kBackPressed, {0, kRows}) & 0xFF) {  // B (plays its own sound)
        Close();
        return;
    }
    if (Call(ctx, kUp)) {
        g_row = (g_row + kRows - 1) % kRows;
        Call(ctx, kSound, {0x23, 0});
    } else if (Call(ctx, kDown)) {
        g_row = (g_row + 1) % kRows;
        Call(ctx, kSound, {0x23, 0});
    } else if (g_row == kCharactersRow) {
        if (Call(ctx, kAPressed, {0}) & 0xFF) Call(ctx, kSound, {UnlockCharacters() ? 0x24u : 0x23u, 0});
    } else if (g_row == kInputRow) {
        if (Call(ctx, kAPressed, {1}) & 0xFF) {
            g_page = 1;
            g_input_row = 0;
            g_hold = 4;
        }
    } else if (Call(ctx, kLeft)) {
        Change(g_row, -1);
        Call(ctx, kSound, {0x23, 0});
    } else if (Call(ctx, kRight) || (Call(ctx, kAPressed, {0}) & 0xFF)) {
        Change(g_row, 1);
        Call(ctx, kSound, {0x23, 0});
    }
}

// The level 0 part of the controller, as the game's with eight items: EXIT stays the one A leaves on.
void TopInput(X86Context& ctx) {
    W16(Row(), uint16_t(RowOf(int16_t(R16(kCursors)))));
    Call(ctx, kMoveCursor, {Row(), kGameItems + 1});
    W16(kCursors, uint16_t(RowOf(int16_t(R16(Row())))));  // the swap is its own inverse
    const int16_t cursor = int16_t(R16(kCursors));
    if (Call(ctx, kBackPressed, {uint32_t(uint16_t(cursor)), kGameItems}) & 0xFF) {
        Call(ctx, kLeaveSound, {1});
        Call(ctx, kLeave);
        return;
    }
    if (!(Call(ctx, kAPressed, {1}) & 0xFF)) return;
    const int item = cursor + 1;
    if (cursor == kOurItem) {
        Open();
    } else if (item == 6) {
        Call(ctx, kMenuSound, {0x17});
        W16(kLevel, 4);
        W16(kMenu, 6);
    } else if (item == 5) {
        Call(ctx, kMenuSound, {0x18});
        W16(kLevel, 4);
        W16(kMenu, 0xF);
        W16(0x00DF4AD0, 0);
    } else {
        if (item == 4) W16(0x00DF4ADC, uint16_t(Call(ctx, kControllerSlot)));
        W16(kLevel, 1);
        W16(kCursors + 2, 0);
        W16(kMenu, uint16_t(item));
    }
}

// ---- drawing ----------------------------------------------------------------------------------------------------

void Text(X86Context& ctx, uint32_t text, int x, int y, float alpha, float bright) {
    Call(ctx, kText, {text, uint32_t(x), uint32_t(y), 3, F(alpha), F(bright), 2, F(kScale)});
}

constexpr int kKey1X = 0x178, kKey2X = 0x1F8;  // the KEYBOARD page's key columns, right aligned

void DrawKeys(X86Context& ctx, float alpha) {
    const int rows = keyboard::Count() + 1;
    const int capture = keyboard::CaptureTarget();
    int slot = 1;
    for (int i = 0; i < kVisible && g_key_top + i < rows; i++) {
        const int row = g_key_top + i;
        const bool sel = row == g_key_row;
        const int y = kRowY0 + i * kRowStep;
        if (row == keyboard::Count()) {
            Text(ctx, Put(slot++, "RESET TO DEFAULTS"), kNameX, y, alpha, sel ? kBright : kDim);
            continue;
        }
        Text(ctx, Put(slot++, keyboard::Name(row)), kNameX, y, alpha, sel ? kBright : kDim);
        for (int k = 0; k < keyboard::kSlots; k++) {
            std::string key = capture == row * keyboard::kSlots + k ? "PRESS A KEY" : keyboard::KeyName(keyboard::Key(row, k));
            const bool cell = sel && k == g_key_slot;
            if (cell) key = "[ " + key + " ]";
            Text(ctx, Put(slot++, key), k ? kKey2X : kKey1X, y, alpha, cell ? kBright : kDim);
        }
    }
    // more rows above or below
    if (g_key_top > 0) Text(ctx, Put(slot++, "^"), kKey2X + 0x18, kRowY0, alpha, kDim);
    if (g_key_top + kVisible < rows)
        Text(ctx, Put(slot++, "v"), kKey2X + 0x18, kRowY0 + (kVisible - 1) * kRowStep, alpha, kDim);
}

constexpr int kDevicesX = 0x2B0;  // the INPUT page's values, right aligned: "KEYBOARD + CONTROLLER 1" is long

void DrawInput(X86Context& ctx, float alpha) {
    for (int i = 0; i < kInputRows; i++) {
        const bool sel = i == g_input_row;
        const int y = kRowY0 + i * kRowStep;
        if (i == players::kPlayers) {
            Text(ctx, Put(1 + 2 * i, "KEYBOARD KEYS"), kNameX, y, alpha, sel ? kBright : kDim);
            Text(ctx, Put(2 + 2 * i, "OPEN"), kValueX, y, alpha, sel ? kBright : kDim);
            continue;
        }
        Text(ctx, Put(1 + 2 * i, "PLAYER " + std::to_string(i + 1)), kNameX, y, alpha, sel ? kBright : kDim);
        std::string value = players::ChoiceName(players::Choice(i));
        if (sel) value = "< " + value + " >";
        // a controller not plugged in stays dim even under the cursor
        const bool missing = players::Missing(players::Choice(i));
        Text(ctx, Put(2 + 2 * i, value), kDevicesX, y, alpha, sel && !missing ? kBright : kDim);
    }
}

void Draw(X86Context& ctx, float alpha) {
    // the eighth bar, and a ninth while the list is up, as the game's: black on the left, fading out
    const uint32_t a = uint32_t(std::clamp(alpha * 255.0f, 0.0f, 255.0f)) << 24;
    W32(Colors() + 0, a);
    W32(Colors() + 4, 0);
    W32(Colors() + 8, a);
    W32(Colors() + 12, 0);
    Call(ctx, kBar, {0, uint32_t(kBarY0 + kGameItems * kRowStep), kBarW, kBarH, Colors(), 4, 0});
    if (g_open) Call(ctx, kBar, {0, uint32_t(kBarY0 + (kGameItems + 1) * kRowStep), kBarW, kBarH, Colors(), 4, 0});

    if (!g_open) {
        Text(ctx, Put(0, "RECOMP SETTINGS"), kItemX, kItemY, alpha, OnOurItem() ? kBright : kDim);
        return;
    }
    if (g_page == 2) {
        DrawKeys(ctx, alpha);
        return;
    }
    if (g_page == 1) {
        DrawInput(ctx, alpha);
        return;
    }
    if (g_unlock_hold > 0) g_unlock_hold--;
    // kVisible rows at a time, scrolled to keep the cursor's
    static int top = 0;
    if (g_row < top) top = g_row;
    if (g_row >= top + kVisible) top = g_row - kVisible + 1;
    for (int k = 0; k < kVisible && top + k < kRows; k++) {
        const int i = top + k;
        const bool sel = i == g_row;
        const int y = kRowY0 + k * kRowStep;
        Text(ctx, Put(1 + i, kLabels[i]), kNameX, y, alpha, sel ? kBright : kDim);
        const bool arrows = sel && i != kInputRow && i != kCharactersRow;
        const std::string value = arrows ? "< " + Value(i) + " >" : Value(i);
        Text(ctx, Put(1 + kRows + i, value), kValueX, y, alpha, sel ? kBright : kDim);
    }
    if (top > 0) Text(ctx, Put(1 + 2 * kRows, "^"), kValueX + 0x18, kRowY0, alpha, kDim);
    if (top + kVisible < kRows)
        Text(ctx, Put(2 + 2 * kRows, "v"), kValueX + 0x18, kRowY0 + (kVisible - 1) * kRowStep, alpha, kDim);
}

}  // namespace

// short ItemCount(short menu), cdecl. Menu 8 is no submenu of ours: the preview under RECOMP SETTINGS stays empty.
X86_FUNC(sub_0011AC90) {
    if (int16_t(R16(ctx.esp + 4)) == kOurItem + 1 && OnOurItem()) {
        ctx.eax = (ctx.eax & 0xFFFF0000u);
        ctx.esp += 4;
        return;
    }
    __imp__sub_0011AC90(ctx, base);
}

// int Controller(a, b), cdecl, once a frame while the menu runs
X86_FUNC(sub_0011CFC0) {
    if (R16(kLevel) != 0) {
        g_open = false;
        __imp__sub_0011CFC0(ctx, base);
        return;
    }
    if (g_open && !OnOurItem()) g_open = false;
    if (g_open) ListInput(ctx);
    else TopInput(ctx);
    ctx.eax = 1;
    ctx.esp += 4;
}

// int DrawSettings(Object* obj), cdecl: the whole scene each frame
X86_FUNC(sub_00095A70) {
    const uint32_t obj = R32(ctx.esp + 4);
    if (g_open && !OnOurItem()) g_open = false;  // left the scene, or it started over
    g_drawing = true;
    __imp__sub_00095A70(ctx, base);  // pops its return address
    g_drawing = false;
    const uint32_t result = ctx.eax;
    Draw(ctx, AsFloat(R32(obj + 0x10)));
    ctx.eax = result;
}

// void Label(int sprite, int x, int y, ...), cdecl. Inside the scene's draw: no glow for the eighth item (its sprite
// would be another label's), and no labels at all while the list is up.
X86_FUNC(sub_00097830) {
    if (g_drawing) {
        const uint32_t x = R32(ctx.esp + 8);
        const bool label = x == kLabelX || x == kGlowX;
        if ((g_open && label) || (x == kGlowX && OnOurItem())) {
            ctx.esp += 4;
            return;
        }
        const uint32_t y = R32(ctx.esp + 12);
        if ((x == kLabelX && y == kExitLabelY) || (x == kGlowX && y == kExitGlowY))
            W32(ctx.esp + 12, y + kRowStep);  // EXIT one row down
    }
    __imp__sub_00097830(ctx, base);
}

// void Box(x, y, w, h, colors, 4, 0), cdecl: inside the scene's draw, the line right of the labels reaches the eighth row
X86_FUNC(sub_000976F0) {
    if (g_drawing && R32(ctx.esp + 4) == kLineX && R32(ctx.esp + 12) == kLineW) {
        const uint32_t h = R32(ctx.esp + 16);
        if (h == uint32_t(kGameItems * kRowStep)) W32(ctx.esp + 16, h + (g_open ? 2 : 1) * kRowStep);
    }
    __imp__sub_000976F0(ctx, base);
}
// void Help(short menu, short cursor, float alpha), cdecl: the help line under the menu
X86_FUNC(sub_00091560) {
    if (int16_t(R16(ctx.esp + 4)) != 0 || int16_t(R16(ctx.esp + 8)) != kOurItem) {
        __imp__sub_00091560(ctx, base);
        return;
    }
    if (R32(kHelpBusy) == 0 && R16(kSceneState) == 2) {
        const uint32_t buttons = Put(kSlots - 2, "");
        const uint32_t a = Call(ctx, kMessage, {0x166, 0xFF});
        const uint32_t b = Call(ctx, kMessage, {0x167, 0xFF});
        Call(ctx, kSwprintf, {buttons, kButtonsFormat, a, b});
        std::string help = g_open ? kHelp[g_row] : kItemHelp;
        if (g_open && g_page == 1) {
            if (g_input_row == players::kPlayers) {
                help = "The keyboard's keys for moving, fighting and the menus.";
            } else {
                const int choice = players::Choice(g_input_row);
                if (players::Missing(choice))
                    help = "Controller " + std::to_string(players::ControllerOf(choice) + 1) + " is not plugged in.";
                else
                    help = "Player " + std::to_string(g_input_row + 1) +
                           "'s controller or keyboard. Taking one from another player swaps them.";
            }
        }
        if (g_open && g_page == 2) {
            const int capture = keyboard::CaptureTarget();
            if (capture >= 0)
                help = std::string("Press the key for ") + keyboard::Name(capture / keyboard::kSlots) +
                       ". ESC cancels, DELETE clears.";
            else if (g_key_row < keyboard::Count())
                help = std::string(keyboard::Help(g_key_row)) + " LEFT/RIGHT: other key.";
            else
                help = "Back to the keys the port came with.";
        }
        const uint32_t text = Put(kSlots - 1, help);
        Call(ctx, kHelpLine, {0, 1, text, buttons, 0, 0});
    }
    ctx.esp += 4;
}
