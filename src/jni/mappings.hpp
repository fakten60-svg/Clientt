// ============================================================================
//  woke.wtf — src/jni/mappings.hpp
//  In-memory representation of mappings.json (Yarn -> Intermediary).
//  Loaded once during JNI startup (cold path — heap use is fine here).
//
//  Keys are always readable Yarn names; `intermediary`/`descriptor` carry the
//  runtime-safe values JNI needs (FindClass/GetMethodID run in the
//  intermediary namespace inside the Fabric-remapped game).
// ============================================================================
#pragma once

#include <cstddef>
#include <string>
#include <unordered_map>
#include <vector>

namespace woke::jni {

// One mapped member: runtime name + JNI descriptor (overloads share a key).
struct member_entry {
    std::string intermediary;   // "method_1507" / "field_1724"
    std::string descriptor;     // "(Lnet/minecraft/class_437;)V" / "Lnet/minecraft/class_746;"
};

struct class_entry {
    std::string intermediary;   // "net/minecraft/class_310"
    std::string descriptor;     // "Lnet/minecraft/class_310;"
    std::unordered_map<std::string, std::vector<member_entry>> methods;  // yarn -> overloads
    std::unordered_map<std::string, std::vector<member_entry>> fields;   // yarn -> entries
};

// Parsed mappings.json. Thread-safe for reads after load() completes
// (load() itself must not race with readers).
class mappings_db {
public:
    // Parses `path` (JSON). On failure returns false and fills *error_out.
    bool load(const char* path, std::string* error_out = nullptr);

    const class_entry* find_class(const std::string& yarn_name) const;

    std::size_t class_count() const { return classes_.size(); }
    std::size_t method_count() const { return total_methods_; }
    std::size_t field_count() const { return total_fields_; }

    const std::unordered_map<std::string, class_entry>& classes() const {
        return classes_;
    }

private:
    std::unordered_map<std::string, class_entry> classes_;  // yarn FQCN -> entry
    std::size_t total_methods_ = 0;
    std::size_t total_fields_ = 0;
};

} // namespace woke::jni
