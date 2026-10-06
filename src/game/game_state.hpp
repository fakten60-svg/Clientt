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
    };
    bool combat_target(combat_target_info& out, char* name_buf, std::size_t cap);

    // PlayerEntity.getAttackCooldownProgress(0) in [0,1]; -1.0 when absent.
    float attack_cooldown_progress();

    // Performs the vanilla client attack on the entity under the crosshair:
    // ClientPlayerInteractionManager.attackEntity(player, target) + the swing
    // hand animation — exactly the calls a mouse click performs. Refuses to
    // attack the local player. false when there is no entity target.
    bool client_attack();

private:
    game_state() = default;

    JNIEnv* env_for(JavaVM* vm);
    bool ensure_double_bridge(JNIEnv* env);   // java/lang/Double valueOf/doubleValue
    bool ensure_int_bridge(JNIEnv* env);      // java/lang/Integer valueOf/intValue
    jobject options_object(JNIEnv* env);
    jobject player_object(JNIEnv* env);
    jobject crosshair_target_object(JNIEnv* env);
    jobject interaction_manager_object(JNIEnv* env);
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
