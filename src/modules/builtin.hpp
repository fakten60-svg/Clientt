// ============================================================================
//  woke.wtf — src/modules/builtin.hpp
//  Built-in module registration (HUD, Fullbright, Zoom, Sprint, Sneak + the
//  combat set: Target HUD, Attack Cooldown, Auto Clicker).
// ============================================================================
#pragma once

// module.hpp declares woke::modules::register_builtins(); this header exists
// so call sites can be explicit about the built-in set.
#include "modules/module.hpp"

namespace woke::modules {

// Registers the combat set (Target HUD, Attack Cooldown, Auto Clicker).
// Called by register_builtins(); idempotent. Split into its own TU so the
// per-file size stays within the hygiene standard.
void register_combat_builtins();

} // namespace woke::modules
