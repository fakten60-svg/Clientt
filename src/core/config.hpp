// ============================================================================
//  woke.wtf — src/core/config.hpp
//  JSON configuration persistence (nlohmann/json):
//
//    {
//      "schema": "woke.wtf/config.v1",
//      "gui":     { "keybind": 62 },
//      "modules": { "fullbright": false, "hud": true, "sprint": false }
//    }
//
//  Path resolution: $WOKE_CONFIG_PATH when set, else "woke_config.json" in
//  the working directory. Every mutation saves immediately, so a crash or
//  hard unload never loses a toggle. Unknown keys are ignored on load;
//  module names not present in the registry are skipped.
// ============================================================================
#pragma once

namespace woke::config {

// Resolved config file path (never null).
const char* path();

// File -> module registry + keybind. Returns false when the file is missing
// or invalid (defaults are kept). Modules are set through the registry's
// normal set_enabled() path so lifecycle hooks fire.
bool load();

// Module registry + keybind -> file. Returns false on I/O/serialize failure.
bool save();

// X11 keycode that toggles the click-gui (default 62 = Right Shift).
int keybind();
void set_keybind(int keycode);   // persists immediately

} // namespace woke::config
