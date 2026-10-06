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
//    * nearest_combat_target()    — scans the client's own world entity list
//                                   for the nearest living entity in reach
//                                   (KillAura target acquisition, no aim assist)
//    * client_attack_entity()     — the same vanilla attack pair against a
//                                   specific entity (the KillAura path)
//    * offhand_totem()            — reads PlayerInventory.OFF_HAND_SLOT
//    * move_totem_to_offhand()    — swaps a totem into the offhand through
//                                   the vanilla clickSlot(SWAP) exchange
//
//  There is no aim assist and no packet generation here: the client itself
//  decides what "under the crosshair" means, attacks travel through the
//  vanilla interaction manager like every normal click, and the Auto Totem
//  swap reuses the vanilla inventory click. All handles come from the
//  reflection cache (static JNI caching).
// ============================================================================
#include <cstdio>
#include <string>
#include <utility>

#include "core/logger.hpp"
#include "jni/reflection_cache.hpp"

// game_state.hpp (same namespace, private helpers shared via the class).
#include "game/game_state.hpp"

#include <cstring>

namespace woke::game {

namespace {

// Yarn keys — resolved to intermediary handles through the reflection cache.
constexpr const char* kMinecraftClient  = "net/minecraft/client/MinecraftClient";
constexpr const char* kLivingEntity     = "net/minecraft/entity/LivingEntity";
constexpr const char* kPlayerEntity     = "net/minecraft/entity/player/PlayerEntity";
constexpr const char* kEntityType       = "net/minecraft/entity/EntityType";
constexpr const char* kEntityHitResult  = "net/minecraft/util/hit/EntityHitResult";
constexpr const char* kInteractionMgr   = "net/minecraft/client/network/ClientPlayerInteractionManager";
constexpr const char* kHand             = "net/minecraft/util/Hand";
constexpr const char* kClientWorld      = "net/minecraft/client/world/ClientWorld";
constexpr const char* kItems            = "net/minecraft/item/Items";
constexpr const char* kItemStack        = "net/minecraft/item/ItemStack";
constexpr const char* kPlayerInventory  = "net/minecraft/entity/player/PlayerInventory";
constexpr const char* kInventory        = "net/minecraft/inventory/Inventory";
constexpr const char* kScreenHandler    = "net/minecraft/screen/ScreenHandler";
constexpr const char* kSlotActionType   = "net/minecraft/screen/slot/SlotActionType";

// The one-argument swingHand overload (the two-arg variant also exists).
const std::string kSwingHandDesc = "(Lnet/minecraft/class_1268;)V";
// squaredDistanceTo(Entity) — the overload that takes the other entity directly
// (method_5858). The three-double and Vec3d overloads share the yarn name, so
// the cache would otherwise resolve the first mapped overload.
const std::string kSquaredDistanceEntityDesc = "(Lnet/minecraft/class_1297;)D";

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

// ----------------------------------------------------------------------------
// KillAura target acquisition: scan the client's own entity list.
// ----------------------------------------------------------------------------

jobject game_state::world_object(JNIEnv* env) {
    jobject client = this->client();
    if (client == nullptr) {
        return nullptr;
    }
    jfieldID fid = jni::reflection_cache::instance().find_field(kMinecraftClient, "world", nullptr);
    if (fid == nullptr) {
        return nullptr;
    }
    jobject obj = env->GetObjectField(client, fid);
    clear_exception(env);
    return obj;
}

bool game_state::nearest_combat_target(float max_distance, combat_target_info& out,
                                       char* name_buf, std::size_t cap) {
    out = combat_target_info{};
    if (name_buf != nullptr && cap > 0) {
        name_buf[0] = '\0';
    }
    JNIEnv* env = this->env();
    if (env == nullptr) {
        return false;
    }
    auto& cache = jni::reflection_cache::instance();

    jobject world = world_object(env);
    jobject player = player_object(env);
    if (world == nullptr || player == nullptr) {
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
    const jclass iterable_cls = env->FindClass("java/lang/Iterable");
    const jclass entity_cls = cache.find_class("net/minecraft/entity/Entity");
    const jclass living_cls = cache.find_class(kLivingEntity);
    const jmethodID iterator_mid =
        (iterable_cls != nullptr) ? env->GetMethodID(iterable_cls, "iterator", "()Ljava/util/Iterator;")
                                  : nullptr;
    if (iterator_mid == nullptr || entity_cls == nullptr) {
        clear_exception(env);
        env->DeleteLocalRef(iterable);
        return false;
    }
    jobject it = env->CallObjectMethod(iterable, iterator_mid);
    clear_exception(env);
    const jclass iterator_cls = env->FindClass("java/util/Iterator");
    const jmethodID has_next =
        (iterator_cls != nullptr) ? env->GetMethodID(iterator_cls, "hasNext", "()Z") : nullptr;
    const jmethodID next =
        (iterator_cls != nullptr) ? env->GetMethodID(iterator_cls, "next", "()Ljava/lang/Object;")
                                  : nullptr;
    const jmethodID is_alive = cache.find_method("net/minecraft/entity/Entity", "isAlive", nullptr);
    const jmethodID squared_to_entity =
        cache.find_method("net/minecraft/entity/Entity", "squaredDistanceTo",
                          &kSquaredDistanceEntityDesc);
    const jmethodID get_health = cache.find_method(kLivingEntity, "getHealth", nullptr);
    const jmethodID get_max = cache.find_method(kLivingEntity, "getMaxHealth", nullptr);
    const jmethodID get_type = cache.find_method("net/minecraft/entity/Entity", "getType", nullptr);
    const jmethodID get_name = cache.find_method(kEntityType, "getUntranslatedName", nullptr);

    if (has_next == nullptr || next == nullptr || squared_to_entity == nullptr ||
        is_alive == nullptr) {
        clear_exception(env);
        env->DeleteLocalRef(it);
        env->DeleteLocalRef(iterable);
        return false;
    }

    // Single pass: keep the nearest candidate as a local ref, drop the rest.
    // The world's entity list is bounded, so this stays inside the frame budget.
    const jdouble cutoff_sq =
        static_cast<double>(max_distance) * static_cast<double>(max_distance);
    jobject best = nullptr;
    jdouble best_sq = cutoff_sq;

    while (env->CallBooleanMethod(it, has_next) == JNI_TRUE) {
        jobject e = env->CallObjectMethod(it, next);
        clear_exception(env);
        if (e == nullptr) {
            continue;
        }
        if (env->IsSameObject(player, e) == JNI_TRUE ||
            env->IsInstanceOf(e, entity_cls) != JNI_TRUE) {
            clear_exception(env);
            env->DeleteLocalRef(e);
            continue;
        }
        const jdouble sq = env->CallDoubleMethod(e, squared_to_entity, player);
        clear_exception(env);
        if (sq < best_sq) {
            if (best != nullptr) {
                env->DeleteLocalRef(best);
            }
            best = e;   // take ownership of this local ref
            best_sq = sq;
        } else {
            env->DeleteLocalRef(e);
        }
    }
    env->DeleteLocalRef(it);
    env->DeleteLocalRef(iterable);

    if (best == nullptr) {
        return false;   // nothing within reach
    }

    out.entity = true;
    out.alive = env->CallBooleanMethod(best, is_alive) == JNI_TRUE;
    clear_exception(env);
    if (living_cls != nullptr && env->IsInstanceOf(best, living_cls) == JNI_TRUE) {
        out.living = true;
        if (get_health != nullptr) {
            out.health = env->CallFloatMethod(best, get_health);
            clear_exception(env);
        }
        if (get_max != nullptr) {
            out.max_health = env->CallFloatMethod(best, get_max);
            clear_exception(env);
        }
    }
    // Type name — only computed when the caller actually wants it.
    if (name_buf != nullptr && cap > 0 && get_type != nullptr && get_name != nullptr) {
        jobject type = env->CallObjectMethod(best, get_type);
        clear_exception(env);
        if (type != nullptr) {
            jobject jname = env->CallObjectMethod(type, get_name);
            clear_exception(env);
            if (jname != nullptr) {
                const char* utf = env->GetStringUTFChars(static_cast<jstring>(jname), nullptr);
                if (utf != nullptr) {
                    std::snprintf(name_buf, cap, "%s", utf);
                    env->ReleaseStringUTFChars(static_cast<jstring>(jname), utf);
                }
                env->DeleteLocalRef(jname);
            }
            env->DeleteLocalRef(type);
        }
    }

    // Hand the best entity to the caller as a local ref (client_attack_entity
    // consumes it inside the same JNI frame). Drop our own.
    out.target = env->NewLocalRef(best);
    env->DeleteLocalRef(best);
    return true;
}

bool game_state::client_attack_entity(jobject target) {
    JNIEnv* env = this->env();
    if (env == nullptr) {
        return false;
    }
    auto& cache = jni::reflection_cache::instance();
    const jmethodID attack_entity = cache.find_method(kInteractionMgr, "attackEntity", nullptr);
    const jmethodID swing_hand = cache.find_method(kLivingEntity, "swingHand", &kSwingHandDesc);
    const jfieldID main_hand = cache.find_field(kHand, "MAIN_HAND", nullptr);
    jclass hand_cls = cache.find_class(kHand);
    jobject manager = interaction_manager_object(env);
    jobject player = player_object(env);
    if (attack_entity == nullptr || manager == nullptr || player == nullptr || target == nullptr) {
        return false;
    }
    if (env->IsSameObject(player, target) == JNI_TRUE) {
        clear_exception(env);
        return false;   // never attack the local player
    }
    env->CallVoidMethod(manager, attack_entity, player, target);
    clear_exception(env);
    if (swing_hand != nullptr && hand_cls != nullptr && main_hand != nullptr) {
        jobject hand = env->GetStaticObjectField(hand_cls, main_hand);
        clear_exception(env);
        if (hand != nullptr) {
            env->CallVoidMethod(player, swing_hand, hand);
            clear_exception(env);
            env->DeleteLocalRef(hand);
        }
    }
    return true;
}

// ----------------------------------------------------------------------------
// Auto Totem: restock the offhand through the vanilla inventory click path.
// ----------------------------------------------------------------------------

bool game_state::move_totem_to_offhand() {
    JNIEnv* env = this->env();
    if (env == nullptr) {
        return false;
    }
    auto& cache = jni::reflection_cache::instance();
    jobject player = player_object(env);
    if (player == nullptr) {
        return false;
    }

    const jmethodID get_inventory = cache.find_method(kPlayerEntity, "getInventory", nullptr);
    const jmethodID get_stack = cache.find_method(kInventory, "getStack", nullptr);
    const jmethodID is_of = cache.find_method(kItemStack, "isOf", nullptr);
    const jfieldID totem_field = cache.find_field(kItems, "TOTEM_OF_UNDYING", nullptr);
    jclass items_cls = cache.find_class(kItems);
    const jfieldID handler_fid = cache.find_field(kPlayerEntity, "playerScreenHandler", nullptr);
    const jfieldID sync_fid = cache.find_field(kScreenHandler, "syncId", nullptr);
    const jfieldID swap_fid = cache.find_field(kSlotActionType, "SWAP", nullptr);
    jclass slot_cls = cache.find_class(kSlotActionType);
    const jmethodID click_slot = cache.find_method(kInteractionMgr, "clickSlot", nullptr);
    if (get_inventory == nullptr || get_stack == nullptr || is_of == nullptr ||
        totem_field == nullptr || items_cls == nullptr || handler_fid == nullptr ||
        sync_fid == nullptr || swap_fid == nullptr || slot_cls == nullptr ||
        click_slot == nullptr) {
        return false;   // member set incomplete (fixture JVM, menu, mappings)
    }

    jobject totem = env->GetStaticObjectField(items_cls, totem_field);
    clear_exception(env);
    if (totem == nullptr) {
        return false;
    }
    jobject inventory = env->CallObjectMethod(player, get_inventory);
    clear_exception(env);
    int totem_inv_slot = -1;
    if (inventory != nullptr) {
        // PlayerInventory layout: 0-8 hotbar, 9-35 main storage, 36-39 armor,
        // 40 offhand. Scan storage first, then the hotbar; armor and the
        // offhand itself are skipped on purpose.
        auto scan_slot = [&](jint slot) -> bool {
            jobject stack = env->CallObjectMethod(inventory, get_stack, slot);
            clear_exception(env);
            if (stack == nullptr) {
                return false;
            }
            const bool hit = env->CallBooleanMethod(stack, is_of, totem) == JNI_TRUE;
            clear_exception(env);
            env->DeleteLocalRef(stack);
            return hit;
        };
        for (jint slot = 9; slot <= 35 && totem_inv_slot < 0; ++slot) {
            if (scan_slot(slot)) {
                totem_inv_slot = slot;
            }
        }
        for (jint slot = 0; slot <= 8 && totem_inv_slot < 0; ++slot) {
            if (scan_slot(slot)) {
                totem_inv_slot = slot;
            }
        }
        env->DeleteLocalRef(inventory);
    }
    env->DeleteLocalRef(totem);
    if (totem_inv_slot < 0) {
        return false;   // no totem in the main inventory — nothing to swap
    }

    // Inventory index -> PlayerScreenHandler slot index: storage 9..35 maps
    // 1:1, the hotbar 0..8 maps to 36..44.
    const jint handler_slot = (totem_inv_slot <= 8) ? totem_inv_slot + 36 : totem_inv_slot;

    // The vanilla exchange: interactionManager.clickSlot(
    // playerScreenHandler.syncId, 45 (offhand slot), handler_slot,
    // SlotActionType.SWAP, player) — the same inventory click a manual
    // offhand drag performs; the client sends it itself, we never build a
    // packet.
    jobject handler = env->GetObjectField(player, handler_fid);
    clear_exception(env);
    jobject swap = env->GetStaticObjectField(slot_cls, swap_fid);
    clear_exception(env);
    jobject manager = interaction_manager_object(env);
    if (handler == nullptr || swap == nullptr || manager == nullptr) {
        if (handler != nullptr) {
            env->DeleteLocalRef(handler);
        }
        if (swap != nullptr) {
            env->DeleteLocalRef(swap);
        }
        return false;
    }
    const jint sync_id = env->GetIntField(handler, sync_fid);
    clear_exception(env);
    env->CallVoidMethod(manager, click_slot, sync_id, 45, handler_slot, swap, player);
    clear_exception(env);
    env->DeleteLocalRef(manager);
    env->DeleteLocalRef(swap);
    env->DeleteLocalRef(handler);
    return true;
}

bool game_state::offhand_totem() {
    JNIEnv* env = this->env();
    if (env == nullptr) {
        return false;
    }
    auto& cache = jni::reflection_cache::instance();
    jobject player = player_object(env);
    if (player == nullptr) {
        return false;
    }
    const jfieldID offhand_slot_fid = cache.find_field(kPlayerInventory, "OFF_HAND_SLOT", nullptr);
    const jmethodID get_stack = cache.find_method(kInventory, "getStack", nullptr);
    const jmethodID is_of = cache.find_method(kItemStack, "isOf", nullptr);
    const jfieldID totem_field = cache.find_field(kItems, "TOTEM_OF_UNDYING", nullptr);
    jclass items_cls = cache.find_class(kItems);
    if (offhand_slot_fid == nullptr || get_stack == nullptr || is_of == nullptr ||
        totem_field == nullptr || items_cls == nullptr) {
        return false;
    }
    // getInventory is a METHOD (method_31548), not a field — resolve it here.
    const jmethodID get_inventory = cache.find_method(kPlayerEntity, "getInventory", nullptr);
    if (get_inventory == nullptr) {
        return false;
    }
    jobject inventory = env->CallObjectMethod(player, get_inventory);
    clear_exception(env);
    if (inventory == nullptr) {
        return false;
    }
    const jint offhand_slot =
        env->GetStaticIntField(cache.find_class(kPlayerInventory), offhand_slot_fid);
    clear_exception(env);
    jobject stack = env->CallObjectMethod(inventory, get_stack, offhand_slot);
    clear_exception(env);
    bool is_totem = false;
    if (stack != nullptr) {
        // TOTEM_OF_UNDYING is a static field holding the shared Item instance.
        jobject item = env->GetStaticObjectField(items_cls, totem_field);
        clear_exception(env);
        if (item != nullptr) {
            is_totem = env->CallBooleanMethod(stack, is_of, item) == JNI_TRUE;
            clear_exception(env);
            env->DeleteLocalRef(item);
        }
        env->DeleteLocalRef(stack);
    }
    env->DeleteLocalRef(inventory);
    return is_totem;
}

} // namespace woke::game
