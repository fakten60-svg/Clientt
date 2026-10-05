// ============================================================================
//  woke.wtf — src/jni/reflection_cache.cpp
//  Populate/release/query implementation. The populate loop builds a private
//  map first and swaps it in under the lock, so concurrent queries never see
//  a half-built cache. Every failed lookup is ExceptionClear()ed immediately.
// ============================================================================
#include "jni/reflection_cache.hpp"

#include <chrono>
#include <utility>

#include "core/logger.hpp"

namespace woke::jni {

reflection_cache& reflection_cache::instance() {
    static reflection_cache cache;
    return cache;
}

populate_stats reflection_cache::populate(JNIEnv* env, const mappings_db& db) {
    populate_stats stats{};
    if (env == nullptr) {
        return stats;
    }
    const auto t0 = std::chrono::steady_clock::now();

    // Re-populate path: the old generation must go before the new one lands.
    release(env);

    std::unordered_map<std::string, cached_class> built;
    built.reserve(db.class_count());
    long miss_samples = 0;

    for (const auto& [yarn_name, entry] : db.classes()) {
        jclass cls = env->FindClass(entry.intermediary.c_str());
        if (cls == nullptr) {
            env->ExceptionClear();
            ++stats.classes_missing;
            if (miss_samples < 3) {   // first few misses only — no log spam
                WOKE_DEBUG("jni", "FindClass miss: %s", entry.intermediary.c_str());
                ++miss_samples;
            }
            continue;
        }

        cached_class cached;
        cached.intermediary = entry.intermediary;
        cached.handle = static_cast<jclass>(env->NewGlobalRef(cls));
        env->DeleteLocalRef(cls);
        if (cached.handle == nullptr) {
            ++stats.classes_missing;
            continue;
        }
        ++stats.classes_ok;

        // ---- methods: instance first, then static fallback -----------------
        for (const auto& [yarn_key, overloads] : entry.methods) {
            auto& out = cached.methods[yarn_key];
            out.reserve(overloads.size());
            for (const auto& ov : overloads) {
                jmethodID id = env->GetMethodID(cached.handle,
                                                ov.intermediary.c_str(),
                                                ov.descriptor.c_str());
                if (id == nullptr) {
                    env->ExceptionClear();
                    id = env->GetStaticMethodID(cached.handle,
                                                ov.intermediary.c_str(),
                                                ov.descriptor.c_str());
                    if (id == nullptr) {
                        env->ExceptionClear();
                        ++stats.methods_missing;
                        continue;
                    }
                }
                out.push_back({ov.descriptor, id});
                ++stats.methods_ok;
            }
        }

        // ---- fields: instance first, then static fallback ------------------
        for (const auto& [yarn_key, slots] : entry.fields) {
            auto& out = cached.fields[yarn_key];
            out.reserve(slots.size());
            for (const auto& fs : slots) {
                jfieldID id = env->GetFieldID(cached.handle,
                                              fs.intermediary.c_str(),
                                              fs.descriptor.c_str());
                if (id == nullptr) {
                    env->ExceptionClear();
                    id = env->GetStaticFieldID(cached.handle,
                                               fs.intermediary.c_str(),
                                               fs.descriptor.c_str());
                    if (id == nullptr) {
                        env->ExceptionClear();
                        ++stats.fields_missing;
                        continue;
                    }
                }
                out.push_back({fs.descriptor, id});
                ++stats.fields_ok;
            }
        }

        built.emplace(yarn_name, std::move(cached));
    }

    long methods_total = 0;
    long fields_total = 0;
    for (const auto& [_, cc] : built) {
        for (const auto& [__, v] : cc.methods) {
            methods_total += static_cast<long>(v.size());
        }
        for (const auto& [__, v] : cc.fields) {
            fields_total += static_cast<long>(v.size());
        }
    }

    {
        std::lock_guard<std::mutex> lock(mutex_);
        classes_ = std::move(built);
        methods_total_ = methods_total;
        fields_total_ = fields_total;
    }

    stats.elapsed_ms =
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    return stats;
}

void reflection_cache::release(JNIEnv* env) {
    std::unordered_map<std::string, cached_class> dying;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        dying.swap(classes_);
        methods_total_ = 0;
        fields_total_ = 0;
    }
    if (env == nullptr) {
        return;   // entries just drop; JVM will reclaim anything unresolved
    }
    for (auto& [_, cc] : dying) {
        if (cc.handle != nullptr) {
            env->DeleteGlobalRef(cc.handle);
            cc.handle = nullptr;
        }
    }
}

void reflection_cache::clear() {
    std::lock_guard<std::mutex> lock(mutex_);
    classes_.clear();
    methods_total_ = 0;
    fields_total_ = 0;
}

const std::string* reflection_cache::intermediary_class(const std::string& yarn) const {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto it = classes_.find(yarn);
    return (it == classes_.end()) ? nullptr : &it->second.intermediary;
}

jclass reflection_cache::find_class(const std::string& yarn) const {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto it = classes_.find(yarn);
    return (it == classes_.end()) ? nullptr : it->second.handle;
}

jmethodID reflection_cache::find_method(const std::string& yarn_cls,
                                        const std::string& yarn_name,
                                        const std::string* descriptor) const {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto cls = classes_.find(yarn_cls);
    if (cls == classes_.end()) {
        return nullptr;
    }
    const auto m = cls->second.methods.find(yarn_name);
    if (m == cls->second.methods.end() || m->second.empty()) {
        return nullptr;
    }
    if (descriptor == nullptr) {
        return m->second.front().id;   // first resolved overload
    }
    for (const auto& ov : m->second) {
        if (ov.descriptor == *descriptor) {
            return ov.id;
        }
    }
    return nullptr;
}

jfieldID reflection_cache::find_field(const std::string& yarn_cls,
                                      const std::string& yarn_name,
                                      const std::string* descriptor) const {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto cls = classes_.find(yarn_cls);
    if (cls == classes_.end()) {
        return nullptr;
    }
    const auto f = cls->second.fields.find(yarn_name);
    if (f == cls->second.fields.end() || f->second.empty()) {
        return nullptr;
    }
    if (descriptor == nullptr) {
        return f->second.front().id;
    }
    for (const auto& slot : f->second) {
        if (slot.descriptor == *descriptor) {
            return slot.id;
        }
    }
    return nullptr;
}

long reflection_cache::class_count() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return static_cast<long>(classes_.size());
}

long reflection_cache::method_count() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return methods_total_;
}

long reflection_cache::field_count() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return fields_total_;
}

} // namespace woke::jni
