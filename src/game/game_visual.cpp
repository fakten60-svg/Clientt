// ============================================================================
//  woke.wtf — src/game/game_visual.cpp
//  Visual-snapshot accessors of game_state (methods split out of
//  game_state.cpp like game_combat.cpp). These feed Player ESP, Storage ESP,
//  Name Tags and Tracers. Client-state read only:
//
//    * esp_scan_players()        — snapshot of every OTHER player entity in
//                                  the client world (positions, rotation,
//                                  health, GameProfile display name)
//    * esp_scan_block_entities() — snapshot of the world's storage-like
//                                  block entities (BlockEntity.getPos +
//                                  the Vec3i getters) with a kind label index
//    * project_world_to_screen() — view-basis + perspective projection of a
//                                  world point onto the overlay viewport
//    * esp_storage_kind_label()  — human label for a storage kind index
//
//  No packet generation and no game writes here — pure observation of the
//  client's own world state. All handles come from the reflection cache
//  (static JNI caching); per-frame paths never call FindClass/GetMethodID
//  outside the cache.
// ============================================================================
#include <cmath>
#include <cstdio>
#include <vector>

#include "jni/reflection_cache.hpp"

// game_state.hpp (same namespace, private helpers shared via the class).
#include "game/game_state.hpp"

namespace woke::game {

namespace {

constexpr const char* kClientWorld  = "net/minecraft/client/world/ClientWorld";
constexpr const char* kEntity       = "net/minecraft/entity/Entity";
constexpr const char* kLivingEntity = "net/minecraft/entity/LivingEntity";
constexpr const char* kPlayerEntity = "net/minecraft/entity/player/PlayerEntity";
constexpr const char* kBlockEntity  = "net/minecraft/block/entity/BlockEntity";
constexpr const char* kVec3i        = "net/minecraft/util/math/Vec3i";

const std::string kZeroDoubleDesc = "()D";
const std::string kZeroFloatDesc  = "()F";
const std::string kSquaredDistanceEntityDesc = "(Lnet/minecraft/class_1297;)D";

// Storage kinds Storage ESP draws (checked with instanceof against the world's
// block-entity set; classes absent from a JVM are skipped by the caller).
struct storage_kind {
    const char* yarn;
    const char* label;
};
constexpr storage_kind kStorageKinds[] = {
    {"net/minecraft/block/entity/ChestBlockEntity", "Chest"},
    {"net/minecraft/block/entity/BarrelBlockEntity", "Barrel"},
    {"net/minecraft/block/entity/ShulkerBoxBlockEntity", "Shulker Box"},
    {"net/minecraft/block/entity/HopperBlockEntity", "Hopper"},
    {"net/minecraft/block/entity/DispenserBlockEntity", "Dispenser"},
    {"net/minecraft/block/entity/DropperBlockEntity", "Dropper"},
    {"net/minecraft/block/entity/AbstractFurnaceBlockEntity", "Furnace"},
    {"net/minecraft/block/entity/BrewingStandBlockEntity", "Brewing Stand"},
    {"net/minecraft/block/entity/LecternBlockEntity", "Lectern"},
    {"net/minecraft/block/entity/EnderChestBlockEntity", "Ender Chest"},
    {"net/minecraft/block/entity/ChiseledBookshelfBlockEntity", "Bookshelf"},
    {"net/minecraft/block/entity/DecoratedPotBlockEntity", "Decorated Pot"},
};
constexpr int kStorageKindCount =
    static_cast<int>(sizeof(kStorageKinds) / sizeof(kStorageKinds[0]));

// Iterator plumbing shared by both scans (java/lang/Iterable over whatever
// collection the world hands out). On success `it` holds the iterator local
// ref the caller's loop drives (and releases).
bool resolve_iterator(JNIEnv* env, jobject iterable, jobject& it, jmethodID& has_next,
                      jmethodID& next) {
    it = nullptr;
    has_next = nullptr;
    next = nullptr;
    if (env == nullptr || iterable == nullptr) {
        return false;
    }
    const jclass iterable_cls = env->FindClass("java/lang/Iterable");
    const jmethodID iterator_mid =
        (iterable_cls != nullptr)
            ? env->GetMethodID(iterable_cls, "iterator", "()Ljava/util/Iterator;")
            : nullptr;
    env->ExceptionClear();
    if (iterator_mid == nullptr) {
        return false;
    }
    it = env->CallObjectMethod(iterable, iterator_mid);
    env->ExceptionClear();
    if (it == nullptr) {
        return false;
    }
    const jclass iterator_cls = env->FindClass("java/util/Iterator");
    has_next = (iterator_cls != nullptr) ? env->GetMethodID(iterator_cls, "hasNext", "()Z")
                                         : nullptr;
    next = (iterator_cls != nullptr) ? env->GetMethodID(iterator_cls, "next",
                                                        "()Ljava/lang/Object;")
                                     : nullptr;
    env->ExceptionClear();
    if (has_next == nullptr || next == nullptr) {
        env->DeleteLocalRef(it);
        it = nullptr;
        return false;
    }
    return true;
}

} // namespace

int esp_storage_kind_count() {
    return kStorageKindCount;
}

const char* esp_storage_kind_label(int kind) {
    return (kind >= 0 && kind < kStorageKindCount) ? kStorageKinds[kind].label : nullptr;
}

bool game_state::esp_scan_players(double max_distance, std::vector<esp_player>& out) {
    out.clear();
    JNIEnv* env = this->env();
    if (env == nullptr) {
        return false;
    }
    auto& cache = jni::reflection_cache::instance();

    jobject world = world_object(env);
    jobject self = player_object(env);
    if (world == nullptr || self == nullptr) {
        return false;
    }
    const jmethodID get_entities = cache.find_method(kClientWorld, "getEntities", nullptr);
    if (get_entities == nullptr) {
        return false;
    }
    jobject iterable = env->CallObjectMethod(world, get_entities);
    clear_exception(env);
    if (iterable == nullptr) {
        return false;
    }

    jmethodID has_next = nullptr;
    jmethodID next = nullptr;
    jobject it = nullptr;
    if (!resolve_iterator(env, iterable, it, has_next, next)) {
        clear_exception(env);
        env->DeleteLocalRef(iterable);
        return false;
    }

    const jclass player_cls = cache.find_class(kPlayerEntity);
    const jclass living_cls = cache.find_class(kLivingEntity);
    const jmethodID is_alive = cache.find_method(kEntity, "isAlive", nullptr);
    const jmethodID get_x = cache.find_method(kEntity, "getX", &kZeroDoubleDesc);
    const jmethodID get_y = cache.find_method(kEntity, "getY", &kZeroDoubleDesc);
    const jmethodID get_z = cache.find_method(kEntity, "getZ", &kZeroDoubleDesc);
    const jmethodID get_yaw = cache.find_method(kEntity, "getYaw", &kZeroFloatDesc);
    const jmethodID get_pitch = cache.find_method(kEntity, "getPitch", &kZeroFloatDesc);
    const jmethodID squared_to = cache.find_method(kEntity, "squaredDistanceTo",
                                                   &kSquaredDistanceEntityDesc);
    const jmethodID get_profile = cache.find_method(kPlayerEntity, "getGameProfile", nullptr);
    const jmethodID get_health = cache.find_method(kLivingEntity, "getHealth", nullptr);
    const jmethodID get_max = cache.find_method(kLivingEntity, "getMaxHealth", nullptr);

    // GameProfile is a com.mojang class outside the yarn mappings — resolved
    // directly here (per call: one FindClass + one GetMethodID on a tiny
    // interface, both cached by the JVM itself).
    jclass profile_cls = env->FindClass("com/mojang/authlib/GameProfile");
    clear_exception(env);
    jmethodID profile_name =
        (profile_cls != nullptr)
            ? env->GetMethodID(profile_cls, "getName", "()Ljava/lang/String;")
            : nullptr;
    clear_exception(env);

    const jdouble cutoff_sq = max_distance * max_distance;
    while (env->CallBooleanMethod(it, has_next) == JNI_TRUE) {
        jobject e = env->CallObjectMethod(it, next);
        clear_exception(env);
        if (e == nullptr) {
            continue;
        }
        // Other players only: instanceof PlayerEntity, never the local one.
        if (env->IsSameObject(self, e) == JNI_TRUE || player_cls == nullptr ||
            env->IsInstanceOf(e, player_cls) != JNI_TRUE) {
            clear_exception(env);
            env->DeleteLocalRef(e);
            continue;
        }
        if (is_alive != nullptr && env->CallBooleanMethod(e, is_alive) != JNI_TRUE) {
            clear_exception(env);
            env->DeleteLocalRef(e);
            continue;
        }
        const jdouble sq = (squared_to != nullptr)
                               ? env->CallDoubleMethod(e, squared_to, self)
                               : 0.0;
        clear_exception(env);
        if (sq > cutoff_sq) {
            env->DeleteLocalRef(e);
            continue;
        }

        esp_player p{};
        if (get_x != nullptr) {
            p.x = env->CallDoubleMethod(e, get_x);
            clear_exception(env);
        }
        if (get_y != nullptr) {
            p.y = env->CallDoubleMethod(e, get_y);
            clear_exception(env);
        }
        if (get_z != nullptr) {
            p.z = env->CallDoubleMethod(e, get_z);
            clear_exception(env);
        }
        if (get_yaw != nullptr) {
            p.yaw = env->CallFloatMethod(e, get_yaw);
            clear_exception(env);
        }
        if (get_pitch != nullptr) {
            p.pitch = env->CallFloatMethod(e, get_pitch);
            clear_exception(env);
        }
        if (living_cls != nullptr && env->IsInstanceOf(e, living_cls) == JNI_TRUE) {
            if (get_health != nullptr) {
                p.health = env->CallFloatMethod(e, get_health);
                clear_exception(env);
            }
            if (get_max != nullptr) {
                p.max_health = env->CallFloatMethod(e, get_max);
                clear_exception(env);
            }
        }
        if (get_profile != nullptr && profile_name != nullptr) {
            jobject profile = env->CallObjectMethod(e, get_profile);
            clear_exception(env);
            if (profile != nullptr) {
                jobject jname = env->CallObjectMethod(profile, profile_name);
                clear_exception(env);
                if (jname != nullptr) {
                    const char* utf =
                        env->GetStringUTFChars(static_cast<jstring>(jname), nullptr);
                    if (utf != nullptr) {
                        std::snprintf(p.name, sizeof p.name, "%s", utf);
                        env->ReleaseStringUTFChars(static_cast<jstring>(jname), utf);
                    }
                    env->DeleteLocalRef(jname);
                }
                env->DeleteLocalRef(profile);
            }
        }
        out.push_back(p);
        env->DeleteLocalRef(e);
    }
    env->DeleteLocalRef(it);
    env->DeleteLocalRef(iterable);
    return true;
}

bool game_state::esp_scan_block_entities(double max_distance, std::vector<esp_storage>& out) {
    out.clear();
    JNIEnv* env = this->env();
    if (env == nullptr) {
        return false;
    }
    auto& cache = jni::reflection_cache::instance();

    jobject world = world_object(env);
    jobject self = player_object(env);
    if (world == nullptr || self == nullptr) {
        return false;
    }
    const jmethodID get_block_entities =
        cache.find_method(kClientWorld, "getBlockEntities", nullptr);
    if (get_block_entities == nullptr) {
        return false;
    }
    jobject set = env->CallObjectMethod(world, get_block_entities);
    clear_exception(env);
    if (set == nullptr) {
        return false;
    }

    jmethodID has_next = nullptr;
    jmethodID next = nullptr;
    jobject it = nullptr;
    if (!resolve_iterator(env, set, it, has_next, next)) {
        clear_exception(env);
        env->DeleteLocalRef(set);
        return false;
    }

    const jmethodID get_pos = cache.find_method(kBlockEntity, "getPos", nullptr);
    const jmethodID get_bx = cache.find_method(kVec3i, "getX", nullptr);
    const jmethodID get_by = cache.find_method(kVec3i, "getY", nullptr);
    const jmethodID get_bz = cache.find_method(kVec3i, "getZ", nullptr);
    const jmethodID get_px = cache.find_method(kEntity, "getX", &kZeroDoubleDesc);
    const jmethodID get_py = cache.find_method(kEntity, "getY", &kZeroDoubleDesc);
    const jmethodID get_pz = cache.find_method(kEntity, "getZ", &kZeroDoubleDesc);
    if (get_pos == nullptr || get_bx == nullptr || get_by == nullptr || get_bz == nullptr ||
        get_px == nullptr || get_py == nullptr || get_pz == nullptr) {
        clear_exception(env);
        env->DeleteLocalRef(it);
        env->DeleteLocalRef(set);
        return false;
    }

    const double px = env->CallDoubleMethod(self, get_px);
    clear_exception(env);
    const double py = env->CallDoubleMethod(self, get_py);
    clear_exception(env);
    const double pz = env->CallDoubleMethod(self, get_pz);
    clear_exception(env);
    const jdouble cutoff_sq = max_distance * max_distance;

    while (env->CallBooleanMethod(it, has_next) == JNI_TRUE) {
        jobject be = env->CallObjectMethod(it, next);
        clear_exception(env);
        if (be == nullptr) {
            continue;
        }
        int kind = -1;
        for (int i = 0; i < kStorageKindCount; ++i) {
            const jclass cls = cache.find_class(kStorageKinds[i].yarn);
            if (cls != nullptr && env->IsInstanceOf(be, cls) == JNI_TRUE) {
                kind = i;
                break;
            }
        }
        clear_exception(env);
        if (kind < 0) {
            env->DeleteLocalRef(be);
            continue;   // not a storage block entity (sign, spawner, ...)
        }
        jobject pos = env->CallObjectMethod(be, get_pos);
        clear_exception(env);
        env->DeleteLocalRef(be);
        if (pos == nullptr) {
            continue;
        }
        const jint bx = env->CallIntMethod(pos, get_bx);
        clear_exception(env);
        const jint by = env->CallIntMethod(pos, get_by);
        clear_exception(env);
        const jint bz = env->CallIntMethod(pos, get_bz);
        clear_exception(env);
        env->DeleteLocalRef(pos);

        // Block center; distance gate against the player's feet position.
        const double cx = static_cast<double>(bx) + 0.5;
        const double cy = static_cast<double>(by) + 0.5;
        const double cz = static_cast<double>(bz) + 0.5;
        const double dx = cx - px;
        const double dy = cy - py;
        const double dz = cz - pz;
        if (dx * dx + dy * dy + dz * dz > cutoff_sq) {
            continue;
        }
        esp_storage s;
        s.x = cx;
        s.y = cy;
        s.z = cz;
        s.kind = kind;
        out.push_back(s);
    }
    env->DeleteLocalRef(it);
    env->DeleteLocalRef(set);
    return true;
}

bool game_state::project_world_to_screen(double wx, double wy, double wz, double width,
                                         double height, double& sx, double& sy,
                                         bool& visible) {
    sx = 0.0;
    sy = 0.0;
    visible = false;
    if (width <= 0.0 || height <= 0.0) {
        return false;
    }
    JNIEnv* env = this->env();
    if (env == nullptr) {
        return false;
    }
    auto& cache = jni::reflection_cache::instance();

    const jmethodID get_x = cache.find_method(kEntity, "getX", &kZeroDoubleDesc);
    const jmethodID get_y = cache.find_method(kEntity, "getY", &kZeroDoubleDesc);
    const jmethodID get_z = cache.find_method(kEntity, "getZ", &kZeroDoubleDesc);
    const jmethodID get_eye_y = cache.find_method(kEntity, "getEyeY", &kZeroDoubleDesc);
    const jmethodID get_yaw = cache.find_method(kEntity, "getYaw", &kZeroFloatDesc);
    const jmethodID get_pitch = cache.find_method(kEntity, "getPitch", &kZeroFloatDesc);
    jobject player = player_object(env);
    if (get_x == nullptr || get_y == nullptr || get_z == nullptr || get_eye_y == nullptr ||
        get_yaw == nullptr || get_pitch == nullptr || player == nullptr) {
        return false;
    }
    const double px = env->CallDoubleMethod(player, get_x);
    clear_exception(env);
    const double pz = env->CallDoubleMethod(player, get_z);
    clear_exception(env);
    const double eye_y = env->CallDoubleMethod(player, get_eye_y);
    clear_exception(env);
    const float yaw_f = env->CallFloatMethod(player, get_yaw);
    clear_exception(env);
    const float pitch_f = env->CallFloatMethod(player, get_pitch);
    clear_exception(env);

    // View basis from the local rotation (the exact conventions aim_at_point
    // uses, so ESP boxes and aim angles agree). fov from the video setting;
    // fall back to the vanilla default when the read is unavailable.
    constexpr double kDegToRad = 3.14159265358979323846 / 180.0;
    const double yaw_rad = static_cast<double>(yaw_f) * kDegToRad;
    const double pitch_rad = static_cast<double>(pitch_f) * kDegToRad;
    const double cyaw = std::cos(yaw_rad);
    const double syaw = std::sin(yaw_rad);
    const double cpit = std::cos(pitch_rad);
    const double spit = std::sin(pitch_rad);

    const double fx = -syaw * cpit;
    const double fy = -spit;
    const double fz = cyaw * cpit;
    const double rx = cyaw;
    const double rz = syaw;
    const double ux = -spit * syaw;
    const double uy = cpit;
    const double uz = spit * cyaw;

    const double dx = wx - px;
    const double dy = wy - eye_y;
    const double dz = wz - pz;
    const double depth = dx * fx + dy * fy + dz * fz;
    if (depth <= 0.1) {
        return true;   // behind the camera — caller skips the draw
    }
    const double right_d = dx * rx + dz * rz;
    const double up_d = dx * ux + dy * uy + dz * uz;

    const int fov_opt = fov();
    const double fov_deg = (fov_opt > 1 && fov_opt < 179) ? static_cast<double>(fov_opt) : 70.0;
    const double scale = (height * 0.5) / std::tan(fov_deg * 0.5 * kDegToRad);
    sx = width * 0.5 + scale * right_d / depth;
    sy = height * 0.5 - scale * up_d / depth;
    visible = true;
    return true;
}

} // namespace woke::game
