// ============================================================================
//  woke.wtf — src/jni/mappings.cpp
//  mappings.json -> mappings_db via nlohmann/json. Cold path: runs once per
//  JNI startup, heap allocations and exceptions are acceptable here (never
//  called from on_render/on_tick).
// ============================================================================
#include "jni/mappings.hpp"

#include <fstream>
#include <utility>

#include <nlohmann/json.hpp>

namespace woke::jni {

bool mappings_db::load(const char* path, std::string* error_out) {
    using json = nlohmann::json;

    if (path == nullptr || path[0] == '\0') {
        if (error_out != nullptr) {
            *error_out = "empty mappings path";
        }
        return false;
    }

    std::ifstream in(path, std::ios::binary);
    if (!in) {
        if (error_out != nullptr) {
            *error_out = std::string("cannot open ") + path;
        }
        return false;
    }

    json doc;
    try {
        in >> doc;
    } catch (const json::exception& e) {
        if (error_out != nullptr) {
            *error_out = std::string("JSON parse error: ") + e.what();
        }
        return false;
    }

    if (!doc.contains("classes") || !doc["classes"].is_object()) {
        if (error_out != nullptr) {
            *error_out = "mappings file has no \"classes\" object";
        }
        return false;
    }

    classes_.clear();
    total_methods_ = 0;
    total_fields_ = 0;

    try {
        for (const auto& item : doc["classes"].items()) {
            const std::string& yarn_name = item.key();
            const json& node = item.value();

            class_entry entry;
            entry.intermediary = node.value("intermediary", std::string{});
            entry.descriptor = node.value("descriptor", std::string{});
            if (entry.intermediary.empty()) {
                continue;   // malformed record — skip rather than poison lookups
            }

            if (node.contains("methods") && node["methods"].is_object()) {
                for (const auto& m : node["methods"].items()) {
                    auto& overloads = entry.methods[m.key()];
                    for (const auto& ov : m.value()) {
                        overloads.push_back(
                            {ov.value("intermediary", std::string{}),
                             ov.value("descriptor", std::string{})});
                    }
                    total_methods_ += overloads.size();
                }
            }
            if (node.contains("fields") && node["fields"].is_object()) {
                for (const auto& f : node["fields"].items()) {
                    auto& slots = entry.fields[f.key()];
                    for (const auto& s : f.value()) {
                        slots.push_back(
                            {s.value("intermediary", std::string{}),
                             s.value("descriptor", std::string{})});
                    }
                    total_fields_ += slots.size();
                }
            }

            classes_.emplace(yarn_name, std::move(entry));
        }
    } catch (const json::exception& e) {
        if (error_out != nullptr) {
            *error_out = std::string("JSON structure error: ") + e.what();
        }
        return false;
    }

    if (classes_.empty()) {
        if (error_out != nullptr) {
            *error_out = "mappings file contained zero classes";
        }
        return false;
    }
    return true;
}

const class_entry* mappings_db::find_class(const std::string& yarn_name) const {
    const auto it = classes_.find(yarn_name);
    return (it == classes_.end()) ? nullptr : &it->second;
}

} // namespace woke::jni
