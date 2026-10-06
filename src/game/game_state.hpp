// ============================================================================
//  woke.wtf — src/game/game_state.hpp
//  Client-state access layer over the JNI reflection cache.
//
//  Every accessor goes through cached handles only (blueprint: static JNI
//  caching — no FindClass/GetMethodID on hot paths) and is null-safe: when
//  the game object graph is not reachable (menu, world not loaded, fixture
//  JVM without the members) the call fails softly and returns a default.
//
//  Thread model: env() daemon-attaches the calling thread on demand and
//  caches the JNIEnv* in thread-local storage. Attachments are daemon so a
//  stuck render thread never blocks JVM shutdown; native threads stay
//  attached for their lifetime (standard injected-client practice).
//
//  Scope: client-state read/write only. No packet generation or network
//  mutation ever goes through this layer.
// ============================================================================
#pragma once

#include <jni.h>

#include <mutex>

namespace woke::game {

class game_state {
public:
    static game_state& instance();

    // Captured at JNI startup / cleared at shutdown. Must outlive all calls.
    void set_vm(JavaVM* vm);
    JavaVM* vm() const;

    // JNIEnv for the calling thread (daemon-attach on demand). nullptr when
    // no VM is captured.
    JNIEnv* env();

    // Exception hygiene: every JNI call in this layer clears a pending
    // exception before returning.
    static void clear_exception(JNIEnv* env);

    // ---- object graph (local refs; valid until the caller returns) ----------
    jobject client();        // MinecraftClient.getInstance() — null when absent
    jobject player();        // client.player (ClientPlayerEntity) — null in menus
    jobject options();       // client.options (GameOptions)

    // ---- client-state queries / writes -------------------------------------
    bool client_ready();     // game object graph reachable
    int current_fps();       // MinecraftClient.getCurrentFps() — 0 when absent

    double gamma();                       // GameOptions.gamma (SimpleOption) — 0.0 default
    bool set_gamma(double gamma);         // gamma.setValue(Double) — video setting only

    int fov();                            // GameOptions.fov (SimpleOption) — 0 default
    bool set_fov(int fov);                // fov.setValue(Integer) — video setting only

    bool is_sprinting();                  // Entity.isSprinting() on the player
    bool set_sprinting(bool on);          // Entity.setSprinting() — movement state

    bool is_sneaking();                   // Entity.isSneaking() on the player
    bool set_sneaking(bool on);           // Entity.setSneaking() — movement state

    // ---- combat (client-state read + vanilla client attack path) ------------

    // Snapshot of the entity under the crosshair. Reads the client's own
    // raycast result (MinecraftClient.crosshairTarget) — no targeting scan of
    // our own, no aim assist, no packet involvement. `name_buf` receives the
    // entity TYPE id (EntityType.getUntranslatedName) and is served from an
    // identity cache: the JNI string is only read when the target changes, so
    // the per-frame path stays allocation-free.
    struct combat_target_info {
        bool entity = false;    // crosshair is on an entity
        bool living = false;    // the entity is a LivingEntity (health readable)
        bool alive = false;     // Entity.isAlive()
        float health = 0.0f;    // LivingEntity.getHealth() (0 when !living)
        float max_health = 0.0f;
        jobject target = nullptr;  // local ref of the entity (nearest_target scan);
                                   // valid until the caller returns to JNI
    };
    bool combat_target(combat_target_info& out, char* name_buf, std::size_t cap);

    // PlayerEntity.getAttackCooldownProgress(0) in [0,1]; -1.0 when absent.
    float attack_cooldown_progress();

    // Performs the vanilla client attack on the entity under the crosshair:
    // ClientPlayerInteractionManager.attackEntity(player, target) + the swing
    // hand animation — exactly the calls a mouse click performs. Refuses to
    // attack the local player. false when there is no entity target.
    bool client_attack();

    // ---- combat target acquisition (KillAura) -------------------------------

    // Scans the client world for the nearest living, alive, non-self entity
    // within `max_distance` (Entity.squaredDistanceTo against the player).
    // Reads client state only — no aim assist: the distance is informational,
    // aiming stays with the player. On success `out.target` carries a local
    // ref of the best entity, valid until the caller returns to JNI; pass it
    // to client_attack_entity() in the same frame. Returns false when the
    // world/graph is unreachable or nothing is in reach.
    bool nearest_combat_target(float max_distance, combat_target_info& out, char* name_buf,
                               std::size_t cap);

    // The vanilla attack against a SPECIFIC entity (KillAura path): the same
    // interactionManager.attackEntity + swingHand pair, with the same guards
    // (living, alive, never the local player).
    bool client_attack_entity(jobject target);

    // True when the player's offhand currently holds a totem of undying.
    // Reads PlayerInventory.OFF_HAND_SLOT through Inventory.getStack.
    bool offhand_totem();

    // Moves one totem of undying from the main inventory (storage, then
    // hotbar) into the offhand through the vanilla inventory click exchange:
    // interactionManager.clickSlot(playerScreenHandler.syncId, 45 (offhand),
    // handler_slot, SlotActionType.SWAP, player) — the same client-side
    // inventory click a manual offhand drag performs; no packet of our own.
    // false when there is no totem to move or the handler path is absent.
    bool move_totem_to_offhand();

    // ---- view rotation / motion (client-state) ------------------------------

    // Reads the local player's view rotation through Entity#getYaw/#getPitch.
    // false when the player graph is absent. Angles in degrees.
    bool player_rotation(float& yaw, float& pitch);

    // Aims the local view toward `target` — client-state only, the rotation
    // lives in the local Entity exactly like mouse-look. Computes the needed
    // yaw/pitch for the target's chest position, optionally applies ONE step
    // of at most `max_step_deg` scaled by `gain` (fraction of the remaining
    // delta), and reports the full angular delta (deg) BEFORE the step via
    // `delta_deg` so callers can gate on an FOV cone. Never snaps: the caller
    // chooses the per-tick budget. false when handles/positions are absent.
    bool aim_angle_to(jobject target, double max_step_deg, double gain, bool apply,
                      double& delta_deg);

    // Entity.fallDistance on the local player; -1.0 when absent.
    double player_fall_distance();

    // LivingEntity.getHealth() on the local player; -1.0f when absent.
    float player_health();

    // Entity.setVelocity(x, y, z) — the vanilla movement-state write (the
    // same call knockback and elytra boosts use). Client state only.
    bool boost_player(double vx, double vy, double vz);

    // True when the main hand currently holds the Items.<field> item named by
    // `items_field_yarn` (e.g. "MACE", "RESPAWN_ANCHOR", "TRIDENT"). Reads
    // LivingEntity.getMainHandStack().isOf(Items.<field>).
    bool main_hand_item_is(const char* items_field_yarn);

    // The vanilla use-click on the block under the crosshair:
    // interactionManager.interactBlock(player, MAIN_HAND, blockHitResult) —
    // the same call a right-click performs (place / charge / detonate is
    // decided by the game itself). false when the crosshair is not on a
    // BlockHitResult or the path is absent.
    bool client_use_block();

    // ---- combat target acquisition (Auto Hit Crystal) -----------------------

    // Same scan as nearest_combat_target(), but only entities that are
    // instances of the End Crystal class qualify (crystals are not living,
    // so `out.living` stays false and no health is read).
    bool nearest_crystal_target(float max_distance, combat_target_info& out,
                                char* name_buf, std::size_t cap);

private:
    game_state() = default;

    JNIEnv* env_for(JavaVM* vm);
    bool ensure_double_bridge(JNIEnv* env);   // java/lang/Double valueOf/doubleValue
    bool ensure_int_bridge(JNIEnv* env);      // java/lang/Integer valueOf/intValue
    jobject options_object(JNIEnv* env);
    jobject player_object(JNIEnv* env);
    jobject crosshair_target_object(JNIEnv* env);
    jobject interaction_manager_object(JNIEnv* env);
    jobject world_object(JNIEnv* env);
    // Shared entity-list scan behind nearest_combat_target() and
    // nearest_crystal_target(): picks the nearest, alive, non-self Entity
    // within reach; when `only_class_yarn` is non-null, only instances of
    // that yarn class qualify.
    bool scan_nearest_entity(float max_distance, const char* only_class_yarn,
                             combat_target_info& out, char* name_buf, std::size_t cap);
    void release_bridge(JNIEnv* env);

    mutable std::mutex mutex_;
    JavaVM* vm_ = nullptr;

    // Combat identity cache (render/tick thread only; released on disarm).
    // Holds a global ref of the last crosshair target so the name string is
    // only re-read when the target object actually changes.
    jobject combat_last_target_ = nullptr;
    char combat_last_name_[64] = "";
    bool combat_name_valid_ = false;

    // java/lang/Double bridge (global ref + method IDs), lazily built.
    jclass double_cls_ = nullptr;
    jmethodID double_value_of_ = nullptr;
    jmethodID double_double_value_ = nullptr;

    // java/lang/Integer bridge — the FOV option is boxed as an Integer.
    jclass int_cls_ = nullptr;
    jmethodID int_value_of_ = nullptr;
    jmethodID int_int_value_ = nullptr;
};

} // namespace woke::game
