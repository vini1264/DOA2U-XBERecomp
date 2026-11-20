// F10 menu: the runtime's settings, changed while the game runs, and saved to settings.toml on request

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <atomic>
#include <cstdio>
#include <imgui.h>
#include <string>

#include "app/app.h"
#include "audio/audio.h"
#include "core/input.h"
#include "game/title_quit.h"
#include "keyboard.h"
#include "players.h"
#include "menu.h"

namespace menu {

namespace {

std::atomic<bool> g_open{false};

struct Resolution {
    const char* name;
    uint32_t width, height;  // 0, 0 is the desktop mode
};
constexpr Resolution kResolutions[] = {{"Desktop", 0, 0},        {"1280 x 720", 1280, 720},   {"1600 x 900", 1600, 900},
                                       {"1920 x 1080", 1920, 1080}, {"2560 x 1440", 2560, 1440}};
constexpr int kResolutionCount = int(sizeof(kResolutions) / sizeof(kResolutions[0]));

}  // namespace

bool Message(void*, uint32_t msg, uintptr_t wp, intptr_t lp, intptr_t* result) {
    if (keyboard::Message(msg, wp, lp, result)) return true;  // a key for a binding
    // F10 comes as a system key: left to Windows it starts the window menu loop, so it is taken here, down and up
    if (wp != VK_F10) return false;
    switch (msg) {
        case WM_KEYDOWN:
        case WM_SYSKEYDOWN:
            if (!(lp & (1 << 30))) g_open = !g_open;  // bit 30: held down, a repeat
            *result = 0;
            return true;
        case WM_KEYUP:
        case WM_SYSKEYUP:
            *result = 0;
            return true;
    }
    return false;
}

void Pad(int port, input::Pad& pad) {
    keyboard::Suspend(g_open);  // nor the keyboard's fight actions
    if (g_open) {
        const bool connected = pad.connected;
        pad = input::Pad{};
        pad.connected = connected;
        return;
    }
    keyboard::Apply(port, pad);
}

void Ui(double fps) {
    keyboard::Ui(g_open);  // the mappings window, beside this one
    title_quit::Ui();       // the title screen's "quit?" question
    if (!g_open) return;
    app::Settings& s = app::Get();  // UiLock is held by the caller, as for every settings access

    ImGui::SetNextWindowPos(ImVec2(24, 24), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(400, 0), ImGuiCond_FirstUseEver);
    bool open = true;
    static const std::string title = [] {  // the window's title is the game's
        char utf8[256] = {};
        WideCharToMultiByte(CP_UTF8, 0, app::kGame.title, -1, utf8, sizeof(utf8) - 1, nullptr, nullptr);
        return std::string(utf8) + "###menu";
    }();
    if (ImGui::Begin(title.c_str(), &open, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("%.1f FPS", fps);
        ImGui::TextDisabled("F10 closes this menu");

        ImGui::SeparatorText("Display");
        {
            static const char* const kModes[] = {"Fullscreen", "Borderless", "Windowed"};
            int mode = int(s.display_mode);
            if (ImGui::Combo("Display mode", &mode, kModes, 3)) {
                s.display_mode = app::DisplayMode(mode);
                app::ApplyDisplay();
            }
            const char* names[kResolutionCount + 1];
            int index = kResolutionCount;  // "Custom": a size set in the settings file
            for (int i = 0; i < kResolutionCount; i++) {
                names[i] = kResolutions[i].name;
                if (s.width == kResolutions[i].width && s.height == kResolutions[i].height) index = i;
            }
            names[kResolutionCount] = "Custom (settings file)";
            if (ImGui::Combo("Resolution", &index, names, kResolutionCount + 1) && index < kResolutionCount) {
                s.width = kResolutions[index].width;
                s.height = kResolutions[index].height;
                app::ApplyDisplay();
            }
            ImGui::Checkbox("VSync", &s.vsync);

            static const char* const kCaps[] = {"60", "Monitor refresh rate", "Unlimited", "Custom (settings file)"};
            int cap = s.fps_cap == 60 ? 0 : s.fps_cap == app::kFpsCapRefresh ? 1 : s.fps_cap == 0 ? 2 : 3;
            if (ImGui::Combo("Frame rate limit", &cap, kCaps, 4) && cap < 3)
                s.fps_cap = cap == 0 ? 60 : cap == 1 ? app::kFpsCapRefresh : 0;

            int percent = int(s.render_percent);
            if (ImGui::SliderInt("Render resolution", &percent, 25, 400, "%d%%")) s.render_percent = uint32_t(percent);

            static const uint32_t kAniso[] = {0, 2, 4, 8, 16};
            static const char* const kAnisoNames[] = {"As the game asks", "2x", "4x", "8x", "16x"};
            int aniso = 0;
            for (int i = 0; i < 5; i++)
                if (s.aniso == kAniso[i]) aniso = i;
            if (ImGui::Combo("Anisotropic filtering", &aniso, kAnisoNames, 5)) s.aniso = kAniso[aniso];
        }

        ImGui::SeparatorText("Audio");
        {
            static const char* const kOutputs[] = {"Automatic", "Stereo", "Surround", "Mono"};
            int output = int(s.audio_output);
            if (output < 0 || output > 3) output = 0;
            if (ImGui::Combo("Output", &output, kOutputs, 4)) {
                s.audio_output = uint32_t(output);
                audio::SetOutputMode(s.audio_output);
            }
        }

        ImGui::SeparatorText("Textures");
        {
            ImGui::Checkbox("Custom textures (textures\\replacement)", &s.custom_textures);
            ImGui::Checkbox("Dump textures (textures\\dumps)", &s.dump_textures);
            static const char* const kFormats[] = {"PNG", "DDS"};
            int format = s.texture_dump_format ? 1 : 0;
            if (ImGui::Combo("Dump format", &format, kFormats, 2)) s.texture_dump_format = uint32_t(format);
        }

        ImGui::SeparatorText("Input");
        for (int p = 0; p < players::kPlayers; p++) {
            const int choice = players::Choice(p);
            std::string preview = players::ChoiceName(choice) + (players::Missing(choice) ? " (not connected)" : "");
            char label[16];
            std::snprintf(label, sizeof(label), "Player %d", p + 1);
            if (ImGui::BeginCombo(label, preview.c_str())) {
                for (int c = 0; c < players::kChoices; c++) {
                    std::string name = players::ChoiceName(c) + (players::Missing(c) ? " (not connected)" : "");
                    if (ImGui::Selectable(name.c_str(), c == choice) && c != choice) {
                        players::SetChoice(p, c);
                        app::Save();  // UiLock is held
                    }
                }
                ImGui::EndCombo();
            }
        }
        if (ImGui::Button("Keyboard mappings")) keyboard::OpenUi();

        ImGui::Separator();
        static double saved_at = -10.0;
        if (ImGui::Button("Save settings")) {
            app::Save();
            saved_at = ImGui::GetTime();
        }
        ImGui::SameLine();
        if (ImGui::Button("Close")) open = false;
        ImGui::SameLine();
        // the same as the window's close button: the runtime shuts the game down
        if (ImGui::Button("Quit game")) PostMessageW(HWND(app::Window()), WM_CLOSE, 0, 0);
        if (ImGui::GetTime() - saved_at < 2.0) {
            ImGui::SameLine();
            ImGui::TextDisabled("saved");
        }
    }
    ImGui::End();
    if (!open) g_open = false;
}

}  // namespace menu
