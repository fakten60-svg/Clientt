// ============================================================================
//  woke.wtf — src/export/lifecycle.cpp
//  JVM-driven lifecycle entry points + the exported status / mappings /
//  reflection-cache introspection (dlsym-able through libwoke.so).
//
//  The lifecycle machinery itself (state machine, startup, shutdown, deferred
//  worker, constructor/destructor) lives in src/libwoke.cpp; this file is the
//  thin export surface of that core (see core/lifecycle.hpp).
// ============================================================================
#include <jni.h>

#include "core/lifecycle.hpp"
#include "core/logger.hpp"
#include "jni/reflection_cache.hpp"

// Must survive -fvisibility=hidden: the JVM and tests resolve these by name.
#define WOKE_API __attribute__((visibility("default")))

namespace {

using woke::lifecycle::g_mappings;
using woke::lifecycle::g_state;

} // namespace

// ----------------------------------------------------------------------------
// JVM-driven lifecycle
// ----------------------------------------------------------------------------
extern "C" {

WOKE_API jint JNICALL JNI_OnLoad(JavaVM* vm, void* reserved) {
    (void)reserved;
    if (vm == nullptr) {
        WOKE_ERROR("jni", "JNI_OnLoad called with null JavaVM");
        return JNI_ERR;
    }
    // The JVM is driving us, so the deferred-init worker is not needed; join it
    // before initializing so the two paths cannot interleave.
    woke::lifecycle::stop_deferred_init();
    if (!woke::lifecycle::jni_startup(vm)) {
        return JNI_ERR;
    }
    WOKE_INFO("jni", "JNI_OnLoad complete — returning JNI_VERSION_1_8 (0x%04x)",
              JNI_VERSION_1_8);
    return JNI_VERSION_1_8;
}

WOKE_API void JNICALL JNI_OnUnload(JavaVM* vm, void* reserved) {
    (void)vm;
    (void)reserved;
    WOKE_INFO("jni", "JNI_OnUnload called — releasing reflection cache");
    woke::lifecycle::jni_shutdown();
}

// ----------------------------------------------------------------------------
// Exported lifecycle + cache introspection (dlsym-able)
// ----------------------------------------------------------------------------

WOKE_API int woke_jni_status() {
    return g_state.load(std::memory_order_acquire);
}

// 1 while the pure-injection deferred-init worker is armed.
WOKE_API int woke_jni_deferred_armed() {
    return woke::lifecycle::deferred_init_armed() ? 1 : 0;
}

WOKE_API long woke_mappings_class_count() {
    return static_cast<long>(g_mappings.class_count());
}
WOKE_API long woke_mappings_method_count() {
    return static_cast<long>(g_mappings.method_count());
}
WOKE_API long woke_mappings_field_count() {
    return static_cast<long>(g_mappings.field_count());
}

WOKE_API long woke_jni_cached_class_count() {
    return woke::jni::reflection_cache::instance().class_count();
}
WOKE_API long woke_jni_cached_method_count() {
    return woke::jni::reflection_cache::instance().method_count();
}
WOKE_API long woke_jni_cached_field_count() {
    return woke::jni::reflection_cache::instance().field_count();
}

// Yarn class name -> runtime intermediary name ("" when mappings lack it).
WOKE_API const char* woke_reflection_intermediary_class(const char* yarn_class) {
    if (yarn_class == nullptr) {
        return nullptr;
    }
    const std::string* s =
        woke::jni::reflection_cache::instance().intermediary_class(yarn_class);
    return (s != nullptr) ? s->c_str() : nullptr;
}

// Yarn class name -> cached jclass (global ref; null when unresolved).
WOKE_API void* woke_reflection_class(const char* yarn_class) {
    if (yarn_class == nullptr) {
        return nullptr;
    }
    return static_cast<void*>(woke::jni::reflection_cache::instance().find_class(yarn_class));
}

// Yarn class + method -> cached jmethodID. `descriptor` may be null to get
// the first resolved overload (pass the exact descriptor to disambiguate).
WOKE_API void* woke_reflection_method(const char* yarn_class, const char* method,
                                      const char* descriptor) {
    if (yarn_class == nullptr || method == nullptr) {
        return nullptr;
    }
    const std::string desc = (descriptor != nullptr) ? descriptor : std::string{};
    return static_cast<void*>(woke::jni::reflection_cache::instance().find_method(
        yarn_class, method, (descriptor != nullptr) ? &desc : nullptr));
}

WOKE_API void* woke_reflection_field(const char* yarn_class, const char* field,
                                     const char* descriptor) {
    if (yarn_class == nullptr || field == nullptr) {
        return nullptr;
    }
    const std::string desc = (descriptor != nullptr) ? descriptor : std::string{};
    return static_cast<void*>(woke::jni::reflection_cache::instance().find_field(
        yarn_class, field, (descriptor != nullptr) ? &desc : nullptr));
}

} // extern "C"
