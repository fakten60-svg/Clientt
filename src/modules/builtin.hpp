// ============================================================================
//  woke.wtf — src/modules/builtin.hpp
//  Built-in module registration (HUD, Fullbright, Zoom, Sprint, Sneak + the
//  combat set: Target HUD, Attack Cooldown, Auto Clicker, KillAura, W-Tap,
//  Auto Totem + the pvp set: Triggerbot, AimAssist, Auto Hit Crystal,
//  Anchor Macro, SafeAnchor + the macro set: Safe Anchor Macro, Shield
//  Breaker, Pearl Catch + the weapon set: Auto Mace, Spear Lunge + the
//  overlay set: Player ESP, Storage ESP, Name Tags, Tracers).
// ============================================================================
#pragma once

// module.hpp declares woke::modules::register_builtins(); this header exists
// so call sites can be explicit about the built-in set.
#include "modules/module.hpp"

namespace woke::modules {

// Registers the classic combat set (Target HUD, Attack Cooldown, Auto Clicker,
// KillAura, W-Tap, Auto Totem). Called by register_builtins(); idempotent.
// Split into its own TU so the per-file size stays within the hygiene standard.
void register_combat_builtins();

// Registers the pvp combat automations (Triggerbot, AimAssist, Auto Hit
// Crystal, Anchor Macro, SafeAnchor). Called by register_builtins();
// idempotent; own TU for the same size reason.
void register_pvp_builtins();

// Registers the macro combat automations (Safe Anchor Macro, Shield Breaker,
// Pearl Catch). Called by register_builtins(); idempotent; own TU for the
// same size reason.
void register_macro_builtins();

// Registers the weapon automations (Auto Mace, Spear Lunge). Called by
// register_builtins(); idempotent; own TU for the same size reason.
void register_gear_builtins();

// Registers the world-overlay render modules (Player ESP, Storage ESP,
// Name Tags, Tracers). Called by register_builtins(); idempotent; own TU
// for the same size reason.
void register_visual_builtins();

} // namespace woke::modules
