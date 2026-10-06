// ============================================================================
//  woke.wtf — src/game/game_macro.cpp
//  Macro-automation accessors of game_state (methods split out of
//  game_state.cpp like game_combat.cpp, to keep every file small). These feed
//  Safe Anchor Macro, Shield Breaker and Pearl Catch. Client-state read/write
//  only:
//
//    * client_use_item()             — vanilla interactItem use-click (the
//                                      right-click-in-air path a pearl or
//                                      wind charge throw goes through)
//    * crosshair_target_using_item() — LivingEntity.isUsingItem on the
//                                      crosshair entity (Shield Breaker gate)
//    * find_inventory_slot()         — Items.<field> probe over the player's
//                                      own inventory rows (hotbar first)
//    * select_hotbar_slot()          — PlayerInventory.selectedSlot write,
//                                      the state the number keys write
//    * main_hand_item_any()          — multi-item held probe (the axe set)
//
//  No packet generation here: the item use travels through the vanilla
//  interaction manager exactly like a real click, and the selected-slot
//  write is plain client state the vanilla client syncs itself. All handles
//  come from the reflection cache (static JNI caching).
// ============================================================================
#include <cstring>

#include "jni/reflection_cache.hpp"

// game_state.hpp (same namespace, private helpers shared via the class).
#include "game/game_state.hpp"

namespace woke::game {

namespace {

constexpr const char* kInteractionMgr  = "net/minecraft/client/network/ClientPlayerInteractionManager";
constexpr const char* kHand            = "net/minecraft/util/Hand";
constexpr const char* kEntityHitResult = "net/minecraft/util/hit/EntityHitResult";
constexpr const char* kLivingEntity    = "net/minecraft/entity/LivingEntity";
constexpr const char* kPlayerEntity    = "net/minecraft/entity/player/PlayerEntity";
constexpr const char* kPlayerInventory = "net/minecraft/entity/player/PlayerInventory";
constexpr const char* kInventory       = "net/minecraft/inventory/Inventory";
constexpr const char* kItemStack       = "net/minecraft/item/ItemStack";
constexpr const char* kItems           = "net/minecraft/item/Items";

const std::string kInteractItemDesc =
    "(Lnet/minecraft/class_1657;Lnet/minecraft/class_1268;)Lnet/minecraft/class_1269;";

} // namespace

bool game_state::client_use_item() {
    JNIEnv* env = this->env();
    if (env == nullptr) {
        return false;
    }
    auto& cache = jni::reflection_cache::instance();

    const jmethodID interact_item = cache.find_method(kInteractionMgr, "interactItem", &kInteractItemDesc);
    const jfieldID main_hand = cache.find_field(kHand, "MAIN_HAND", nullptr);
    jclass hand_cls = cache.find_class(kHand);
    jobject manager = interaction_manager_object(env);
    jobject player = player_object(env);
    if (interact_item == nullptr || main_hand == nullptr || hand_cls == nullptr ||
        manager == nullptr || player == nullptr) {
        return false;
    }
    jobject hand = env->GetStaticObjectField(hand_cls, main_hand);
    clear_exception(env);
    if (hand == nullptr) {
        return false;
    }

    // The vanilla use-click on the held item — whether it throws a pearl,
    // fires a wind charge or eats is the game's own decision.
    jobject result = env->CallObjectMethod(manager, interact_item, player, hand);
    clear_exception(env);
    if (result != nullptr) {
        env->DeleteLocalRef(result);
    }
    env->DeleteLocalRef(hand);
    return true;
}

bool game_state::crosshair_target_using_item() {
    JNIEnv* env = this->env();
    if (env == nullptr) {
        return false;
    }
    auto& cache = jni::reflection_cache::instance();

    jclass hit_cls = cache.find_class(kEntityHitResult);
    jobject hit = crosshair_target_object(env);
    const jmethodID get_entity = cache.find_method(kEntityHitResult, "getEntity", nullptr);
    const jmethodID is_using = cache.find_method(kLivingEntity, "isUsingItem", nullptr);
    jclass living_cls = cache.find_class(kLivingEntity);
    if (hit_cls == nullptr || hit == nullptr || get_entity == nullptr || is_using == nullptr ||
        living_cls == nullptr) {
        clear_exception(env);
        return false;
    }
    if (env->IsInstanceOf(hit, hit_cls) != JNI_TRUE) {
        clear_exception(env);
        return false;   // crosshair is on a block or in the void
    }
    jobject target = env->CallObjectMethod(hit, get_entity);
    clear_exception(env);
    env->DeleteLocalRef(hit);
    if (target == nullptr) {
        return false;
    }

    // Never treat the local player as a breaking target.
    jobject player = player_object(env);
    bool using_item = false;
    if (player == nullptr || env->IsSameObject(player, target) != JNI_TRUE) {
        if (env->IsInstanceOf(target, living_cls) == JNI_TRUE) {
            using_item = env->CallBooleanMethod(target, is_using) == JNI_TRUE;
            clear_exception(env);
        }
    }
    env->DeleteLocalRef(target);
    return using_item;
}

bool game_state::find_inventory_slot(const char* items_field_yarn, bool hotbar_only,
                                     int& out_slot) {
    out_slot = -1;
    if (items_field_yarn == nullptr || items_field_yarn[0] == '\0') {
        return false;
    }
    JNIEnv* env = this->env();
    if (env == nullptr) {
        return false;
    }
    auto& cache = jni::reflection_cache::instance();
    jobject player = player_object(env);
    const jmethodID get_inventory = cache.find_method(kPlayerEntity, "getInventory", nullptr);
    const jmethodID get_stack = cache.find_method(kInventory, "getStack", nullptr);
    const jmethodID is_of = cache.find_method(kItemStack, "isOf", nullptr);
    const jfieldID item_fid = cache.find_field(kItems, items_field_yarn, nullptr);
    jclass items_cls = cache.find_class(kItems);
    if (player == nullptr || get_inventory == nullptr || get_stack == nullptr ||
        is_of == nullptr || item_fid == nullptr || items_cls == nullptr) {
        return false;
    }
    jobject item = env->GetStaticObjectField(items_cls, item_fid);
    clear_exception(env);
    if (item == nullptr) {
        return false;
    }
    jobject inventory = env->CallObjectMethod(player, get_inventory);
    clear_exception(env);
    if (inventory == nullptr) {
        env->DeleteLocalRef(item);
        return false;
    }

    // PlayerInventory layout: 0-8 hotbar, 9-35 storage, 36-39 armor, 40
    // offhand. Hotbar rows first (they are the ones the macros can select);
    // storage rows only when the caller asked for the full sweep.
    const jint last_slot = hotbar_only ? 8 : 35;
    for (jint slot = 0; slot <= last_slot; ++slot) {
        jobject stack = env->CallObjectMethod(inventory, get_stack, slot);
        clear_exception(env);
        if (stack == nullptr) {
            continue;
        }
        const jboolean hit = env->CallBooleanMethod(stack, is_of, item);
        clear_exception(env);
        env->DeleteLocalRef(stack);
        if (hit == JNI_TRUE) {
            out_slot = slot;
            break;
        }
    }
    env->DeleteLocalRef(inventory);
    env->DeleteLocalRef(item);
    return out_slot >= 0;
}

bool game_state::select_hotbar_slot(int slot) {
    if (slot < 0 || slot > 8) {
        return false;   // the vanilla hotbar is 9 slots wide
    }
    JNIEnv* env = this->env();
    if (env == nullptr) {
        return false;
    }
    auto& cache = jni::reflection_cache::instance();
    jobject player = player_object(env);
    const jmethodID get_inventory = cache.find_method(kPlayerEntity, "getInventory", nullptr);
    const jfieldID selected = cache.find_field(kPlayerInventory, "selectedSlot", nullptr);
    if (player == nullptr || get_inventory == nullptr || selected == nullptr) {
        return false;
    }
    jobject inventory = env->CallObjectMethod(player, get_inventory);
    clear_exception(env);
    if (inventory == nullptr) {
        return false;
    }
    env->SetIntField(inventory, selected, slot);
    clear_exception(env);
    env->DeleteLocalRef(inventory);
    return true;
}

bool game_state::main_hand_item_any(const char* const* items_fields, std::size_t count) {
    if (items_fields == nullptr) {
        return false;
    }
    for (std::size_t i = 0; i < count; ++i) {
        if (main_hand_item_is(items_fields[i])) {
            return true;
        }
    }
    return false;
}

} // namespace woke::game
