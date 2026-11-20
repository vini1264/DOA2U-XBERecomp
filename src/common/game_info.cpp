#include "app/app.h"
#include "players.h"
#include "menu.h"
#include "game/title_quit.h"

// what the runtime asks of the game; strings must be set, hooks may stay null
const app::GameInfo app::kGame = {
    .title = L"Dead or Alive 2 Ultimate",               // window and splash
    .short_title = L"DOA2U",               // message boxes
    .name = L"DOA2U",                      // window classes, crash dumps, handover event
    .log = "DOA2U",                        // log file prefix
    .settings = "settings.toml",          // settings file beside the exe
    .splash = L"splash",                  // optional splash.png or splash.bmp beside the exe
    .missing = L"Put the game in the assets folder: its disc image (.iso), or default.xbe with its folders beside it.",
    .images = {{"DOA2.xbe", "DOA2U"}},  // each recompiled xbe and its exe, for title launches
    .options = {},                        // game settings kept in the settings file
    .load = players::Load,                // reads more from the settings file's toml::table: [players], [keyboard]
    .save = players::Save,                // writes more into it
    .init = nullptr,                      // after the settings load, before the window and boot
    .loaded = nullptr,                    // image in memory, before its entry point runs
    .window = title_quit::Window,         // the game window is up: the title's quit question font
    .message = menu::Message,             // window messages first; F10 opens the menu, keys for bindings
    .ui = menu::Ui,                       // ImGui drawing each frame
    .pad = menu::Pad,                     // the keyboard's keys on port 0; a neutral pad while the menu is up
    .rumble = nullptr,                    // changes a rumble before it is set
    .network = false,                     // offline: no sockets, no Windows firewall prompt
};
