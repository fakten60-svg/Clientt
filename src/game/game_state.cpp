// ============================================================================
//  woke.wtf — src/game/game_state.cpp
//  Implementation notes:
//    * yarn-keyed lookups against the reflection cache (intermediary IDs)
//    * instance-then-static resolution lives in the cache; here we just call
//    * a pending JNI exception is cleared after every call site (Exception-
//      Check/ExceptionClear) so one failed lookup never poisons the next
// ============================================================================
#include "game/game_state.hpp"

#include <utility>

#include "core/logger.hpp"
#include "jni/reflection_cache.hpp"

namespace woke::game {

namespace {

// Yarn keys — resolved to intermediary handles through the reflection cache.
constexpr const char* kMinecraftClient = "net/minecraft/client/MinecraftClient";
constexpr const char* kGameOptions     = "net/minecraft/client/option/GameOptions";
constexpr const char* kSimpleOption    = "net/minecraft/client/option/SimpleOption";
constexpr const char* kEntity          = "net/minecraft/entity/Entity";

// Per-thread cached env + the VM it belongs to (invalidated on set_vm).
thread_local JNIEnv* t_env = nullptr;
thread_local JavaVM* t_env_vm = nullptr;

} // namespace

game_state& game_state::instance() {
    static game_state state;
    return state;
}

void game_state::set_vm(JavaVM* vm) {
    JNIEnv* old_env = nullptr;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (double_cls_ != nullptr) {
            // Bridge release needs an env; take the current one if attached.
            void* p = nullptr;
            if (vm_ != nullptr &&
                vm_->GetEnv(&p, JNI_VERSION_1_8) == JNI_OK && p != nullptr) {
                old_env = static_cast<JNIEnv*>(p);
            }
        }
        vm_ = vm;
    }
    if (old_env != nullptr) {
        release_bridge(old_env);
    }
    WOKE_INFO("game", "game_state %s (JavaVM %p)", vm != nullptr ? "armed" : "disarmed",
              static_cast<void*>(vm));
}

JavaVM* game_state::vm() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return vm_;
}

JNIEnv* game_state::env_for(JavaVM* vm) {
    if (vm == nullptr) {
        return nullptr;
    }
    if (t_env != nullptr && t_env_vm == vm) {
        return t_env;
    }
    void* p = nullptr;
    if (vm->GetEnv(&p, JNI_VERSION_1_8) == JNI_OK && p != nullptr) {
        t_env = static_cast<JNIEnv*>(p);
        t_env_vm = vm;
        return t_env;
    }
    if (vm->AttachCurrentThreadAsDaemon(&p, nullptr) != JNI_OK || p == nullptr) {
        return nullptr;
    }
    t_env = static_cast<JNIEnv*>(p);
    t_env_vm = vm;
    return t_env;
}

JNIEnv* game_state::env() {
    return env_for(vm());
}

void game_state::clear_exception(JNIEnv* env) {
    if (env != nullptr && env->ExceptionCheck()) {
        env->ExceptionClear();
    }
}

bool game_state::ensure_double_bridge(JNIEnv* env) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (double_cls_ != nullptr) {
        return double_value_of_ != nullptr && double_double_value_ != nullptr;
    }
    jclass local = env->FindClass("java/lang/Double");
    if (local == nullptr) {
        clear_exception(env);
        return false;
    }
    double_cls_ = static_cast<jclass>(env->NewGlobalRef(local));
    env->DeleteLocalRef(local);
    if (double_cls_ == nullptr) {
        return false;
    }
    double_value_of_ = env->GetStaticMethodID(double_cls_, "valueOf", "(D)Ljava/lang/Double;");
    if (double_value_of_ == nullptr) {
        clear_exception(env);
    }
    double_double_value_ = env->GetMethodID(double_cls_, "doubleValue", "()D");
    if (double_double_value_ == nullptr) {
        clear_exception(env);
    }
    return double_value_of_ != nullptr && double_double_value_ != nullptr;
}

void game_state::release_bridge(JNIEnv* env) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (double_cls_ != nullptr && env != nullptr) {
        env->DeleteGlobalRef(double_cls_);
    }
    double_cls_ = nullptr;
    double_value_of_ = nullptr;
    double_double_value_ = nullptr;
}

jobject game_state::client() {
    JNIEnv* env = this->env();
    if (env == nullptr) {
        return nullptr;
    }
    auto& cache = jni::reflection_cache::instance();
    jclass cls = cache.find_class(kMinecraftClient);
    jmethodID mid = cache.find_method(kMinecraftClient, "getInstance", nullptr);
    if (cls == nullptr || mid == nullptr) {
        return nullptr;
    }
    jobject obj = env->CallStaticObjectMethod(cls, mid);
    clear_exception(env);
    return obj;
}

jobject game_state::options_object(JNIEnv* env) {
    jobject client = this->client();
    if (client == nullptr) {
        return nullptr;
    }
    jfieldID fid =
        jni::reflection_cache::instance().find_field(kMinecraftClient, "options", nullptr);
    if (fid == nullptr) {
        return nullptr;
    }
    jobject obj = env->GetObjectField(client, fid);
    clear_exception(env);
    return obj;
}

jobject game_state::player_object(JNIEnv* env) {
    jobject client = this->client();
    if (client == nullptr) {
        return nullptr;
    }
    jfieldID fid =
        jni::reflection_cache::instance().find_field(kMinecraftClient, "player", nullptr);
    if (fid == nullptr) {
        return nullptr;
    }
    jobject obj = env->GetObjectField(client, fid);
    clear_exception(env);
    return obj;
}

jobject game_state::player() {
    JNIEnv* env = this->env();
    return (env != nullptr) ? player_object(env) : nullptr;
}

jobject game_state::options() {
    JNIEnv* env = this->env();
    return (env != nullptr) ? options_object(env) : nullptr;
}

bool game_state::client_ready() {
    return client() != nullptr;
}

int game_state::current_fps() {
    JNIEnv* env = this->env();
    if (env == nullptr) {
        return 0;
    }
    jobject client = this->client();
    if (client == nullptr) {
        return 0;
    }
    jmethodID mid =
        jni::reflection_cache::instance().find_method(kMinecraftClient, "getCurrentFps", nullptr);
    if (mid == nullptr) {
        return 0;
    }
    jint fps = env->CallIntMethod(client, mid);
    clear_exception(env);
    return fps;
}

double game_state::gamma() {
    JNIEnv* env = this->env();
    if (env == nullptr) {
        return 0.0;
    }
    auto& cache = jni::reflection_cache::instance();
    jobject options = options_object(env);
    if (options == nullptr) {
        return 0.0;
    }
    jfieldID fid = cache.find_field(kGameOptions, "gamma", nullptr);
    jmethodID mid = cache.find_method(kSimpleOption, "getValue", nullptr);
    if (fid == nullptr || mid == nullptr) {
        return 0.0;
    }
    jobject simple = env->GetObjectField(options, fid);
    clear_exception(env);
    if (simple == nullptr) {
        return 0.0;
    }
    jobject boxed = env->CallObjectMethod(simple, mid);
    clear_exception(env);
    if (boxed == nullptr) {
        return 0.0;
    }
    if (!ensure_double_bridge(env)) {
        return 0.0;
    }
    jdouble value = env->CallDoubleMethod(boxed, double_double_value_);
    clear_exception(env);
    return value;
}

bool game_state::set_gamma(double gamma) {
    JNIEnv* env = this->env();
    if (env == nullptr) {
        return false;
    }
    auto& cache = jni::reflection_cache::instance();
    jobject options = options_object(env);
    if (options == nullptr) {
        return false;
    }
    jfieldID fid = cache.find_field(kGameOptions, "gamma", nullptr);
    jmethodID mid = cache.find_method(kSimpleOption, "setValue", nullptr);
    if (fid == nullptr || mid == nullptr) {
        return false;
    }
    jobject simple = env->GetObjectField(options, fid);
    clear_exception(env);
    if (simple == nullptr) {
        return false;
    }
    if (!ensure_double_bridge(env)) {
        return false;
    }
    jobject boxed = env->CallStaticObjectMethod(double_cls_, double_value_of_, gamma);
    clear_exception(env);
    if (boxed == nullptr) {
        return false;
    }
    env->CallVoidMethod(simple, mid, boxed);
    clear_exception(env);
    return true;
}

bool game_state::is_sprinting() {
    JNIEnv* env = this->env();
    if (env == nullptr) {
        return false;
    }
    jobject player = player_object(env);
    if (player == nullptr) {
        return false;
    }
    jmethodID mid = jni::reflection_cache::instance().find_method(kEntity, "isSprinting", nullptr);
    if (mid == nullptr) {
        return false;
    }
    jboolean sprinting = env->CallBooleanMethod(player, mid);
    clear_exception(env);
    return sprinting == JNI_TRUE;
}

bool game_state::set_sprinting(bool on) {
    JNIEnv* env = this->env();
    if (env == nullptr) {
        return false;
    }
    jobject player = player_object(env);
    if (player == nullptr) {
        return false;
    }
    jmethodID mid = jni::reflection_cache::instance().find_method(kEntity, "setSprinting", nullptr);
    if (mid == nullptr) {
        return false;
    }
    env->CallVoidMethod(player, mid, on ? JNI_TRUE : JNI_FALSE);
    clear_exception(env);
    return true;
}

} // namespace woke::game
