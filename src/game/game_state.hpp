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

    bool is_sprinting();                  // Entity.isSprinting() on the player
    bool set_sprinting(bool on);          // Entity.setSprinting() — movement state

private:
    game_state() = default;

    JNIEnv* env_for(JavaVM* vm);
    bool ensure_double_bridge(JNIEnv* env);   // java/lang/Double valueOf/doubleValue
    jobject options_object(JNIEnv* env);
    jobject player_object(JNIEnv* env);
    void release_bridge(JNIEnv* env);

    mutable std::mutex mutex_;
    JavaVM* vm_ = nullptr;

    // java/lang/Double bridge (global ref + method IDs), lazily built.
    jclass double_cls_ = nullptr;
    jmethodID double_value_of_ = nullptr;
    jmethodID double_double_value_ = nullptr;
};

} // namespace woke::game
