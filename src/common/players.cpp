// Players input devices: which controller each console port reads (input::SetController) and which port the keyboard plays on (keyboard::SetPort).

#include "players.h"

#include <mutex>
#include <string>
#include <toml++/toml.hpp>

#include "core/input.h"
#include "keyboard.h"

namespace players {

namespace {

struct Devices {
    int controller;  // 0-3, -1 none
    bool keyboard;
};

std::mutex g_lock;
Devices g_players[kPlayers] = {{0, true}, {1, false}, {2, false}, {3, false}};

Devices FromChoice(int choice) {
    if (choice < 4) return {choice, true};
    if (choice < 8) return {choice - 4, false};
    return {-1, choice == 8};
}

int ToChoice(Devices d) {
    if (d.controller >= 0) return d.keyboard ? d.controller : 4 + d.controller;
    return d.keyboard ? 8 : 9;
}

void ApplyLocked() {
    int keyboard_port = -1;
    for (int p = 0; p < kPlayers; p++) {
        input::SetController(p, g_players[p].controller);
        if (g_players[p].keyboard) keyboard_port = p;
    }
    keyboard::SetPort(keyboard_port);
}

std::string Key(int choice) {  // in the settings file
    Devices d = FromChoice(choice);
    std::string text = d.keyboard ? "keyboard" : "";
    if (d.controller >= 0) text += std::string(d.keyboard ? "+" : "") + "controller" + std::to_string(d.controller + 1);
    return text.empty() ? "none" : text;
}

}  // namespace

int Choice(int player) {
    std::lock_guard lock(g_lock);
    return ToChoice(g_players[player]);
}

void SetChoice(int player, int choice) {
    if (player < 0 || player >= kPlayers || choice < 0 || choice >= kChoices) return;
    std::lock_guard lock(g_lock);
    const Devices old = g_players[player];
    const Devices want = FromChoice(choice);
    for (int q = 0; q < kPlayers; q++) {
        if (q == player) continue;
        // the other player that had this controller takes this player's, and the keyboard likewise
        if (want.controller >= 0 && g_players[q].controller == want.controller) g_players[q].controller = old.controller;
        if (want.keyboard && g_players[q].keyboard) g_players[q].keyboard = old.keyboard;
    }
    g_players[player] = want;
    ApplyLocked();
}

void Step(int player, int dir) { SetChoice(player, (Choice(player) + dir + kChoices) % kChoices); }

std::string ChoiceName(int choice) {
    Devices d = FromChoice(choice);
    std::string text = d.keyboard ? "KEYBOARD" : "";
    if (d.controller >= 0) {
        text += std::string(d.keyboard ? " + " : "") + "CONTROLLER " + std::to_string(d.controller + 1);
    }
    return text.empty() ? "NONE" : text;
}

bool Missing(int choice) {
    Devices d = FromChoice(choice);
    return d.controller >= 0 && !input::ControllerConnected(d.controller);
}

int ControllerOf(int choice) { return FromChoice(choice).controller; }

void ResetDefaults() {
    std::lock_guard lock(g_lock);
    for (int p = 0; p < kPlayers; p++) g_players[p] = {p, p == 0};
    ApplyLocked();
}

void Load(const void* table) {
    keyboard::Load(table);
    const toml::table& t = *static_cast<const toml::table*>(table);
    std::lock_guard lock(g_lock);
    if (const toml::table* pt = t["players"].as_table()) {
        for (int p = 0; p < kPlayers; p++) {
            std::string value = (*pt)["player" + std::to_string(p + 1)].value_or(std::string());
            for (int c = 0; c < kChoices; c++)
                if (value == Key(c)) g_players[p] = FromChoice(c);
        }
        // a hand-edited file may give one device to two players: the first keeps it
        for (int p = 0; p < kPlayers; p++)
            for (int q = p + 1; q < kPlayers; q++) {
                if (g_players[q].controller >= 0 && g_players[q].controller == g_players[p].controller)
                    g_players[q].controller = -1;
                if (g_players[p].keyboard) g_players[q].keyboard = false;
            }
    }
    ApplyLocked();
}

void Save(void* table) {
    keyboard::Save(table);
    toml::table& t = *static_cast<toml::table*>(table);
    toml::table pt;
    {
        std::lock_guard lock(g_lock);
        for (int p = 0; p < kPlayers; p++) pt.insert("player" + std::to_string(p + 1), Key(ToChoice(g_players[p])));
    }
    t.insert_or_assign("players", std::move(pt));
}

}  // namespace players
