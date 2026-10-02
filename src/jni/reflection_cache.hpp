// ============================================================================
//  woke.wtf — src/jni/reflection_cache.hpp
//  Process-wide registry of resolved JNI handles, keyed by Yarn names.
//
//  Populated once at JNI startup from mappings_db: FindClass(intermediary),
//  NewGlobalRef, GetMethodID/GetStaticMethodID, GetFieldID/GetStaticFieldID —
//  NEVER inside on_render/on_tick loops (blueprint: static JNI caching).
//  Classes absent from the running JVM are skipped silently (counted, first
//  few logged) so the cache works both against the real game and fixtures.
//
//  All accessors lock a single mutex; returned handles stay valid until
//  release() (JNI_OnUnload).
// ============================================================================
#pragma once

#include <jni.h>

#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "jni/mappings.hpp"

namespace woke::jni {

struct cached_method {
    std::string descriptor;   // for overload-aware re-verification
    jmethodID id;
};

struct cached_field {
    std::string descriptor;
    jfieldID id;
};

struct cached_class {
    std::string intermediary;                          // net/minecraft/class_310
    jclass handle = nullptr;                           // global ref
    std::unordered_map<std::string, std::vector<cached_method>> methods;  // yarn ->
    std::unordered_map<std::string, std::vector<cached_field>> fields;    // yarn ->
};

struct populate_stats {
    long classes_ok = 0;
    long classes_missing = 0;
    long methods_ok = 0;
    long methods_missing = 0;
    long fields_ok = 0;
    long fields_missing = 0;
    double elapsed_ms = 0.0;
};

class reflection_cache {
public:
    static reflection_cache& instance();

    // Resolves every mapped class/member against the running JVM.
    // Missing classes/members are tolerated and counted. Safe to call again
    // (previous entries are released first).
    populate_stats populate(JNIEnv* env, const mappings_db& db);

    // JNI_OnUnload: DeleteGlobalRef every cached class and empty the cache.
    void release(JNIEnv* env);

    // Drop entries without touching JNI (defensive fallback).
    void clear();

    // Queries — return nullptr when absent (never throw).
    const std::string* intermediary_class(const std::string& yarn) const;
    jclass find_class(const std::string& yarn) const;
    jmethodID find_method(const std::string& yarn_cls, const std::string& yarn_name,
                          const std::string* descriptor) const;
    jfieldID find_field(const std::string& yarn_cls, const std::string& yarn_name,
                        const std::string* descriptor) const;

    long class_count() const;
    long method_count() const;
    long field_count() const;

private:
    reflection_cache() = default;

    mutable std::mutex mutex_;
    std::unordered_map<std::string, cached_class> classes_;
    long methods_total_ = 0;
    long fields_total_ = 0;
};

} // namespace woke::jni
