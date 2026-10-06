// ============================================================================
//  woke.wtf — src/core/config.hpp
//  JSON configuration persistence (nlohmann/json):
//
//    {
//      "schema":  "woke.wtf/config.v1",
//      "gui":     { "keybind": 62 },
//      "modules": { "fullbright": false, "hud": true, "sprint": false },
//      "settings": { "Fullbright": { "Gamma": 16.0, "Restore": true } },
//      "keybinds": { "Fullbright": 71 }
//    }
//
//  The `settings` block is produced generically from each module's
//  core::setting_group, so a new BaseSetting<T> in a module is persisted
//  without touching this file. Path resolution: $WOKE_CONFIG_PATH when set,
//  else "woke_config.json" in the working directory. Every mutation saves
//  immediately, so a crash or hard unload never loses a toggle. Unknown keys
//  are ignored on load; names not present in the registry are skipped.
// ============================================================================
#pragma once

namespace woke::config {

// Resolved config file path (never null).
const char* path();

// File -> module registry + settings + keybinds. Returns false when the file
// is missing or invalid (defaults are kept). Modules are set through the
// registry's normal set_enabled() path so lifecycle hooks fire.
bool load();

// Module registry + settings + keybinds -> file. Returns false on I/O or
// serialize failure.
bool save();

// X11 keycode that toggles the click-gui (default 62 = Right Shift).
int keybind();
void set_keybind(int keycode);   // persists immediately

// Per-module keybind. 0 means "unbound". Unknown module names are rejected.
int module_keybind(const char* module_name);
bool set_module_keybind(const char* module_name, int keycode, bool persist = true);

// Restores every setting of one module (or all modules when name is null)
// to its declared default and persists. Returns how many settings changed.
int reset_settings(const char* module_name);

} // namespace woke::config
