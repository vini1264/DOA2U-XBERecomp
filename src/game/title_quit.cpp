// Quitting from the title screen.

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <algorithm>
#include <atomic>
#include <cfloat>
#include <filesystem>
#include <string>
#include <cstdint>
#include <imgui.h>

#include "app/app.h"
#include "core/log.h"
#include "game/title_quit.h"
#include "xdk/hle.h"

using namespace kernel;

X86_EXTERN_FUNC(__imp__sub_000D9DB0);
X86_EXTERN_FUNC(__imp__sub_0013B850);

namespace {
constexpr uint32_t kPageBusy = 0x00068EF0;   // nonzero: the attract pages are changing
constexpr uint32_t kTitlePage = 0x0087D25A;  // 2: the title, waiting for START
constexpr uint32_t kSlotPort = 0x008A56F4;   // 4 bytes: a slot's port, 0xFF none
constexpr uint32_t kPortIn = 0x008A56D4;     // 4 bytes: the port has a player in
constexpr uint32_t kPressed = 0x00A94A78;    // port 0's buttons pressed this frame; ports 0x30 apart
constexpr uint32_t kPortStride = 0x30;
constexpr uint32_t kStart = 0x10, kA = 0x100, kB = 0x200;

std::atomic<bool> g_asking{false};
bool g_quitting = false;
// a key held from before the title doesn't count
bool g_esc_down = true;
bool g_enter_down = true;

// a key, once per press, while the game's window has the focus: Enter and Esc answer whoever has the keyboard
bool KeyPressed(int vk, bool& was_down) {
    const bool down = GetForegroundWindow() == static_cast<HWND>(app::Window()) && (GetAsyncKeyState(vk) & 0x8000);
    const bool pressed = down && !was_down;
    was_down = down;
    return pressed;
}

// the title is up and nobody has pressed START yet: the same tests the function makes before reading the pads
bool OnTitle(X86Context& ctx) {
    if (CallGuest(ctx, kPageBusy, {}) != 0) return false;
    for (uint32_t i = 0; i < 4; i++) {
        const uint8_t port = G<uint8_t>(kSlotPort + i);
        if (port != 0xFF && G<uint8_t>(kPortIn + port)) return false;
    }
    return G<uint8_t>(kTitlePage) == 2;
}
}  // namespace

// void TitleInput(void), cdecl
X86_FUNC(sub_000D9DB0) {
    const bool esc = KeyPressed(VK_ESCAPE, g_esc_down);
    const bool enter = KeyPressed(VK_RETURN, g_enter_down);
    if (OnTitle(ctx)) {
        uint32_t any = 0;
        for (uint32_t p = 0; p < 4; p++) any |= R32(kPressed + p * kPortStride);
        if (!g_asking && ((any & kB) || esc)) {
            g_asking = true;
        } else if (g_asking && !g_quitting) {
            if ((any & (kA | kStart)) || enter) {
                g_quitting = true;
                PostMessageW(static_cast<HWND>(app::Window()), WM_CLOSE, 0, 0);
            } else if ((any & kB) || esc) {
                g_asking = false;
                ctx.esp += 4;  // this frame's B stays with the question
                return;
            }
        }
        if (g_asking) {
            ctx.esp += 4;  // pop the return address, as the function's ret would
            return;
        }
    } else if (!g_quitting) {
        g_asking = false;  // the attract pages took over
    }
    __imp__sub_000D9DB0(ctx, base);
}

namespace {
constexpr uint32_t kOnlineItem = 0x12;         // DOA ONLINE's id in the item table
constexpr uint32_t kItems = 0x006E20A0;        // the items: 0x1C each, the id first
constexpr uint32_t kItemSize = 0x1C;
constexpr uint32_t kListed = 0x00DF5D18;       // bytes: the items shown, in order
constexpr uint32_t kListedCount = 0x006E2260;  // byte
constexpr uint32_t kCursor = 0x00DF5698 + 0x120;  // byte: the carousel's place in the list
constexpr uint32_t kSlots = 0x006E2264;        // 4 x 8 bytes: [0] port, [2] nonzero when the slot plays
constexpr uint32_t kMenuPressed = 0x0087D268;  // port 0's buttons pressed this frame as the menus read them
constexpr uint32_t kMenuStride = 0x30;
constexpr uint32_t kChoose = 0x110, kBack = 0x220;  // A or START; B or BACK

std::atomic<bool> g_online{false};
int g_swallow = 0;  // frames after the notice closes in which its answer keeps away from the carousel
bool g_online_esc_down = true;
bool g_online_enter_down = true;

uint32_t CursorItem() {
    uint8_t at = G<uint8_t>(kCursor);
    const uint8_t count = G<uint8_t>(kListedCount);
    if (at > count) at = count;
    return R32(kItems + uint32_t(G<uint8_t>(kListed + at)) * kItemSize);
}

uint32_t MenuPressed() {
    uint32_t any = 0;
    for (uint32_t i = 0; i < 4; i++)
        if (G<uint8_t>(kSlots + i * 8 + 2)) any |= R32(kMenuPressed + uint32_t(G<uint8_t>(kSlots + i * 8)) * kMenuStride);
    return any;
}
}  // namespace

// int ModeSelectFrame(MenuState* state), cdecl: 1 to stay
X86_FUNC(sub_0013B850) {
    const uint32_t state = R32(ctx.esp + 4);
    const bool esc = KeyPressed(VK_ESCAPE, g_online_esc_down);
    const bool enter = KeyPressed(VK_RETURN, g_online_enter_down);
    if (R32(state + 0x20) == 1) {
        const uint32_t pressed = MenuPressed();
        bool hold = false;
        if (g_online) {
            if ((pressed & (kChoose | kBack)) || esc || enter) {
                g_online = false;
                g_swallow = 2;
            }
            hold = true;
        } else if (g_swallow > 0) {
            g_swallow--;
            hold = (pressed & (kChoose | kBack)) != 0;
        } else if (pressed & kChoose) {
            const uint32_t item = CursorItem();
            if (item == kOnlineItem) g_online = hold = true;
        }
        if (hold) {
            ctx.eax = 1;
            ctx.esp += 4;  // pop the return address, as the function's ret would
            return;
        }
    } else {
        g_online = false;
    }
    __imp__sub_0013B850(ctx, base);
}

namespace {
ImFont* g_font = nullptr;  // fonts/ui.ttf, or ImGui's own face rasterized large (Window)

constexpr ImU32 kWhite = IM_COL32(255, 255, 255, 255);
constexpr ImU32 kShadow = IM_COL32(0, 0, 0, 200);

// text with the drop shadow the game's menu text has, centered on x
void ShadowText(ImDrawList* dl, ImFont* font, float size, float cx, float y, const char* text, ImU32 color) {
    const ImVec2 sz = font->CalcTextSizeA(size, FLT_MAX, 0, text);
    const ImVec2 at(cx - sz.x * 0.5f, y);
    const float o = std::max(1.0f, size * 0.06f);
    dl->AddText(font, size, ImVec2(at.x + o, at.y + o), kShadow, text);
    dl->AddText(font, size, at, color, text);
}

// a round face button like the game's footer: colored, a dark rim, a light top, its letter in white
void FaceButton(ImDrawList* dl, ImFont* font, ImVec2 c, float r, ImU32 base, ImU32 light, const char* letter) {
    dl->AddCircleFilled(ImVec2(c.x + r * 0.08f, c.y + r * 0.1f), r, kShadow, 32);
    dl->AddCircleFilled(c, r, IM_COL32(20, 20, 20, 255), 32);
    dl->AddCircleFilled(c, r * 0.9f, base, 32);
    dl->AddCircleFilled(ImVec2(c.x - r * 0.15f, c.y - r * 0.2f), r * 0.55f, light, 32);
    const float size = r * 1.35f;
    const ImVec2 sz = font->CalcTextSizeA(size, FLT_MAX, 0, letter);
    dl->AddText(font, size, ImVec2(c.x - sz.x * 0.5f, c.y - sz.y * 0.5f), kWhite, letter);
}
// a keyboard key as a key cap: a light rounded square, a darker rim, its name in dark letters; returns its width
float KeyCapWidth(ImFont* font, float size, const char* label) {
    return font->CalcTextSizeA(size, FLT_MAX, 0, label).x + size * 0.9f;
}
void KeyCap(ImDrawList* dl, ImFont* font, float size, ImVec2 left_mid, const char* label) {
    const float w = KeyCapWidth(font, size, label), h = size * 1.45f;
    const ImVec2 a(left_mid.x, left_mid.y - h * 0.5f), b(left_mid.x + w, left_mid.y + h * 0.5f);
    const float round = h * 0.22f, o = std::max(1.0f, size * 0.08f);
    dl->AddRectFilled(ImVec2(a.x + o, a.y + o * 1.5f), ImVec2(b.x + o, b.y + o * 1.5f), kShadow, round);
    dl->AddRectFilled(a, b, IM_COL32(25, 25, 25, 255), round);
    dl->AddRectFilledMultiColor(ImVec2(a.x + o, a.y + o), ImVec2(b.x - o, b.y - o * 2), IM_COL32(235, 235, 235, 255),
                                IM_COL32(235, 235, 235, 255), IM_COL32(170, 170, 170, 255),
                                IM_COL32(170, 170, 170, 255));
    const ImVec2 sz = font->CalcTextSizeA(size, FLT_MAX, 0, label);
    dl->AddText(font, size, ImVec2(a.x + (w - sz.x) * 0.5f, a.y + (h - sz.y) * 0.5f - o * 0.5f),
                IM_COL32(30, 30, 30, 255), label);
}
}  // namespace

namespace title_quit {
void Window(void*) {
    // ImGui's own face first, so the F10 menu keeps it; then the question's: fonts/ui.ttf beside the exe, shipped
    // with the release so every system draws the same, else ImGui's own three times larger, drawn down from there
    ImGuiIO& io = ImGui::GetIO();
    io.Fonts->AddFontDefault();
    const std::filesystem::path path = app::ExeDir() / "fonts" / "ui.ttf";
    std::error_code ec;
    if (std::filesystem::is_regular_file(path, ec)) {
        const std::u8string utf8 = path.u8string();
        g_font = io.Fonts->AddFontFromFileTTF(reinterpret_cast<const char*>(utf8.c_str()), 72.0f);
    }
    if (!g_font) {
        ImFontConfig large;
        large.SizePixels = 39.0f;
        g_font = io.Fonts->AddFontDefault(&large);
    }
}

void Ui() {
    if (!g_asking && !g_online) return;
    const ImGuiIO& io = ImGui::GetIO();
    ImDrawList* dl = ImGui::GetForegroundDrawList();
    ImFont* font = g_font ? g_font : ImGui::GetFont();
    const float w = io.DisplaySize.x, h = io.DisplaySize.y;
    const float u = h / 480.0f;  // one console pixel
    const float cx = w * 0.5f, cy = h * 0.5f;

    // the screen dimmed, and a dark band across it like the game's message bars
    dl->AddRectFilled(ImVec2(0, 0), ImVec2(w, h), IM_COL32(0, 0, 0, 110));
    const float top = cy - 46 * u, bottom = cy + 46 * u;
    dl->AddRectFilledMultiColor(ImVec2(0, top), ImVec2(w, cy), IM_COL32(0, 0, 0, 120), IM_COL32(0, 0, 0, 120),
                                IM_COL32(0, 0, 0, 210), IM_COL32(0, 0, 0, 210));
    dl->AddRectFilledMultiColor(ImVec2(0, cy), ImVec2(w, bottom), IM_COL32(0, 0, 0, 210), IM_COL32(0, 0, 0, 210),
                                IM_COL32(0, 0, 0, 120), IM_COL32(0, 0, 0, 120));
    dl->AddLine(ImVec2(0, top), ImVec2(w, top), IM_COL32(160, 160, 160, 160), std::max(1.0f, u));
    dl->AddLine(ImVec2(0, bottom), ImVec2(w, bottom), IM_COL32(160, 160, 160, 160), std::max(1.0f, u));

    if (g_online) {
        ShadowText(dl, font, 26 * u, cx, cy - 38 * u, "DOA ONLINE IS NOT AVAILABLE", kWhite);
        ShadowText(dl, font, 15 * u, cx, cy - 9 * u, "This port has no Xbox Live.", IM_COL32(200, 200, 200, 255));
    } else {
        ShadowText(dl, font, 30 * u, cx, cy - 36 * u, "QUIT GAME?", kWhite);
    }

    // the answers as the game's footer writes them: a face button, the keyboard's key, then the word
    const float size = 20 * u, r = 11 * u, y = g_online ? cy + 22 * u : cy + 12 * u;
    const float key_size = 11 * u;
    const float gap = 6 * u, between = 40 * u;
    struct Answer {
        const char* button;
        ImU32 base, light;
        const char* key;
        const char* word;
    };
    const Answer question[2] = {
        {"A", IM_COL32(40, 170, 40, 255), IM_COL32(130, 230, 110, 255), "ENTER", "Yes"},
        {"B", IM_COL32(200, 30, 30, 255), IM_COL32(250, 110, 100, 255), "ESC", "No"},
    };
    const Answer notice[1] = {
        {"A", IM_COL32(40, 170, 40, 255), IM_COL32(130, 230, 110, 255), "ENTER", "OK"},
    };
    const Answer* first = g_online ? notice : question;
    const Answer* last = g_online ? notice + 1 : question + 2;
    float total = g_online ? 0 : between;
    for (const Answer* p = first; p != last; ++p) {
        const Answer& a = *p;
        total += 2 * r + gap + KeyCapWidth(font, key_size, a.key) + gap +
                 font->CalcTextSizeA(size, FLT_MAX, 0, a.word).x;
    }
    float x = cx - total * 0.5f;
    for (const Answer* p = first; p != last; ++p) {
        const Answer& a = *p;
        FaceButton(dl, font, ImVec2(x + r, y), r, a.base, a.light, a.button);
        x += 2 * r + gap;
        KeyCap(dl, font, key_size, ImVec2(x, y), a.key);
        x += KeyCapWidth(font, key_size, a.key) + gap;
        const float word_w = font->CalcTextSizeA(size, FLT_MAX, 0, a.word).x;
        ShadowText(dl, font, size, x + word_w * 0.5f, y - size * 0.55f, a.word, kWhite);
        x += word_w + between;
    }
}
}  // namespace title_quit
