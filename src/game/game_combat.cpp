// ============================================================================
//  woke.wtf — src/game/game_combat.cpp
//  Combat accessors of game_state (methods split out of game_state.cpp to
//  keep every file small). Client-state read/write only:
//
//    * combat_target()            — reads the client's own crosshair raycast
//    * attack_cooldown_progress() — vanilla attack charge in [0,1]
//    * client_attack()            — the exact call pair a mouse click makes:
//                                   interactionManager.attackEntity(player,
//                                   target) + player.swingHand(MAIN_HAND)
//
//  There is no target scanning, no aim assist and no packet generation here:
//  the client itself decides what "under the crosshair" means, and attacks
//  travel through the vanilla interaction manager like every normal click.
//  All handles come from the reflection cache (static JNI caching).
// ============================================================================
#include <cstdio>
#include <string>
#include <utility>

#include "core/logger.hpp"
#include "jni/reflection_cache.hpp"

// game_state.hpp (same namespace, private helpers shared via the class).
#include "game/game_state.hpp"

namespace woke::game {

namespace {

// Yarn keys — resolved to intermediary handles through the reflection cache.
constexpr const char* kLivingEntity     = "net/minecraft/entity/LivingEntity";
constexpr const char* kPlayerEntity     = "net/minecraft/entity/player/PlayerEntity";
constexpr const char* kEntityType       = "net/minecraft/entity/EntityType";
constexpr const char* kEntityHitResult  = "net/minecraft/util/hit/EntityHitResult";
constexpr const char* kInteractionMgr   = "net/minecraft/client/network/ClientPlayerInteractionManager";
constexpr const char* kHand             = "net/minecraft/util/Hand";

// The one-argument swingHand overload (the two-arg variant also exists).
const std::string kSwingHandDesc = "(Lnet/minecraft/class_1268;)V";

} // namespace

jobject game_state::crosshair_target_object(JNIEnv* env) {
    jobject client = this->client();
    if (client == nullptr) {
        return nullptr;
    }
    jfieldID fid =
        jni::reflection_cache::instance().find_field("net/minecraft/client/MinecraftClient",
                                                      "crosshairTarget", nullptr);
    if (fid == nullptr) {
        return nullptr;
    }
    jobject obj = env->GetObjectField(client, fid);
    clear_exception(env);
    return obj;
}

jobject game_state::interaction_manager_object(JNIEnv* env) {
    jobject client = this->client();
    if (client == nullptr) {
        return nullptr;
    }
    jfieldID fid =
        jni::reflection_cache::instance().find_field("net/minecraft/client/MinecraftClient",
                                                      "interactionManager", nullptr);
    if (fid == nullptr) {
        return nullptr;
    }
    jobject obj = env->GetObjectField(client, fid);
    clear_exception(env);
    return obj;
}

bool game_state::combat_target(combat_target_info& out, char* name_buf, std::size_t cap) {
    out = combat_target_info{};
    if (name_buf != nullptr && cap > 0) {
        name_buf[0] = '\0';
    }
    JNIEnv* env = this->env();
    if (env == nullptr) {
        return false;
    }
    auto& cache = jni::reflection_cache::instance();

    jclass hit_result_cls = cache.find_class(kEntityHitResult);
    jobject hit = crosshair_target_object(env);
    if (hit_result_cls == nullptr || hit == nullptr) {
        return false;
    }
    if (env->IsInstanceOf(hit, hit_result_cls) != JNI_TRUE) {
        clear_exception(env);
        return false;   // crosshair is on a block or in the void
    }
    jmethodID get_entity = cache.find_method(kEntityHitResult, "getEntity", nullptr);
    if (get_entity == nullptr) {
        return false;
    }
    jobject target = env->CallObjectMethod(hit, get_entity);
    clear_exception(env);
    if (target == nullptr) {
        return false;
    }

    // Never act on the local player.
    jobject player = player_object(env);
    if (player != nullptr && env->IsSameObject(player, target) == JNI_TRUE) {
        clear_exception(env);
        return false;
    }

    out.entity = true;

    // Identity-cached type name: the JNI string is read only when the target
    // object changes; otherwise the cached copy is copied into the caller's
    // buffer (zero allocations on the per-frame path).
    const bool same_target = (combat_last_target_ != nullptr) &&
                             (env->IsSameObject(combat_last_target_, target) == JNI_TRUE);
    if (name_buf != nullptr && cap > 0) {
        if (same_target && combat_name_valid_) {
            std::snprintf(name_buf, cap, "%s", combat_last_name_);
        } else {
            jmethodID get_type = cache.find_method("net/minecraft/entity/Entity", "getType", nullptr);
            jmethodID get_name = cache.find_method(kEntityType, "getUntranslatedName", nullptr);
            bool ok = false;
            if (get_type != nullptr && get_name != nullptr) {
                jobject type = env->CallObjectMethod(target, get_type);
                clear_exception(env);
                if (type != nullptr) {
                    jobject jname = env->CallObjectMethod(type, get_name);
                    clear_exception(env);
                    if (jname != nullptr) {
                        const char* utf = env->GetStringUTFChars(static_cast<jstring>(jname), nullptr);
                        if (utf != nullptr) {
                            std::snprintf(combat_last_name_, sizeof combat_last_name_, "%s", utf);
                            env->ReleaseStringUTFChars(static_cast<jstring>(jname), utf);
                            ok = true;
                        }
                        env->DeleteLocalRef(jname);
                    }
                    env->DeleteLocalRef(type);
                }
            }
            combat_name_valid_ = ok;
            if (ok) {
                std::snprintf(name_buf, cap, "%s", combat_last_name_);
            } else {
                name_buf[0] = '\0';
            }
        }
    }
    if (!same_target) {
        if (combat_last_target_ != nullptr) {
            env->DeleteGlobalRef(combat_last_target_);
        }
        combat_last_target_ = env->NewGlobalRef(target);
    }
    env->DeleteLocalRef(target);
    env->DeleteLocalRef(hit);

    // Aliveness + health (cheap float/bool reads, refreshed every call).
    jmethodID is_alive = cache.find_method("net/minecraft/entity/Entity", "isAlive", nullptr);
    if (is_alive != nullptr) {
        out.alive = env->CallBooleanMethod(combat_last_target_, is_alive) == JNI_TRUE;
        clear_exception(env);
    }
    jclass living_cls = cache.find_class(kLivingEntity);
    if (living_cls != nullptr &&
        env->IsInstanceOf(combat_last_target_, living_cls) == JNI_TRUE) {
        out.living = true;
        const jmethodID get_health = cache.find_method(kLivingEntity, "getHealth", nullptr);
        const jmethodID get_max = cache.find_method(kLivingEntity, "getMaxHealth", nullptr);
        if (get_health != nullptr) {
            out.health = env->CallFloatMethod(combat_last_target_, get_health);
            clear_exception(env);
        }
        if (get_max != nullptr) {
            out.max_health = env->CallFloatMethod(combat_last_target_, get_max);
            clear_exception(env);
        }
    }
    return true;
}

float game_state::attack_cooldown_progress() {
    JNIEnv* env = this->env();
    if (env == nullptr) {
        return -1.0f;
    }
    const jmethodID mid = jni::reflection_cache::instance().find_method(
        "net/minecraft/entity/player/PlayerEntity", "getAttackCooldownProgress", nullptr);
    if (mid == nullptr) {
        return -1.0f;
    }
    jobject player = player_object(env);
    if (player == nullptr) {
        return -1.0f;
    }
    const jfloat progress = env->CallFloatMethod(player, mid, static_cast<double>(0.0f));
    clear_exception(env);
    return progress;
}

bool game_state::client_attack() {
    JNIEnv* env = this->env();
    if (env == nullptr) {
        return false;
    }
    auto& cache = jni::reflection_cache::instance();

    const jmethodID attack_entity = cache.find_method(kInteractionMgr, "attackEntity", nullptr);
    const jmethodID swing_hand =
        cache.find_method(kLivingEntity, "swingHand", &kSwingHandDesc);
    const jfieldID main_hand = cache.find_field(kHand, "MAIN_HAND", nullptr);
    jclass hand_cls = cache.find_class(kHand);
    if (attack_entity == nullptr) {
        return false;
    }

    jobject manager = interaction_manager_object(env);
    jobject player = player_object(env);
    if (manager == nullptr || player == nullptr) {
        return false;
    }

    // Resolve the crosshair target (same guards as combat_target()).
    jclass hit_result_cls = cache.find_class(kEntityHitResult);
    jobject hit = crosshair_target_object(env);
    if (hit_result_cls == nullptr || hit == nullptr ||
        env->IsInstanceOf(hit, hit_result_cls) != JNI_TRUE) {
        clear_exception(env);
        return false;
    }
    const jmethodID get_entity = cache.find_method(kEntityHitResult, "getEntity", nullptr);
    if (get_entity == nullptr) {
        return false;
    }
    jobject target = env->CallObjectMethod(hit, get_entity);
    clear_exception(env);
    if (target == nullptr || env->IsSameObject(player, target) == JNI_TRUE) {
        clear_exception(env);
        env->DeleteLocalRef(hit);
        return false;
    }

    env->CallVoidMethod(manager, attack_entity, player, target);
    clear_exception(env);

    // Swing animation — the same hand the vanilla click uses. Cosmetic only;
    // the attack already went through the interaction manager.
    if (swing_hand != nullptr && hand_cls != nullptr && main_hand != nullptr) {
        jobject hand = env->GetStaticObjectField(hand_cls, main_hand);
        clear_exception(env);
        if (hand != nullptr) {
            env->CallVoidMethod(player, swing_hand, hand);
            clear_exception(env);
            env->DeleteLocalRef(hand);
        }
    }

    env->DeleteLocalRef(target);
    env->DeleteLocalRef(hit);
    return true;
}

} // namespace woke::game
