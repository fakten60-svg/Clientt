// ============================================================================
//  woke.wtf — src/modules/module.cpp
//  Module base + registry implementation.
// ============================================================================
#include "modules/module.hpp"

#include <utility>

#include "core/logger.hpp"

namespace woke::modules {

module::module(std::string name, std::string category, std::string description)
    : name_(std::move(name)),
      category_(std::move(category)),
      description_(std::move(description)) {}

void module::set_enabled(bool on) {
    if (enabled_ == on) {
        return;
    }
    enabled_ = on;
    if (on) {
        WOKE_INFO("module", "%s enabled (%s)", name_.c_str(), category_.c_str());
        on_enable();
    } else {
        WOKE_INFO("module", "%s disabled (%s)", name_.c_str(), category_.c_str());
        on_disable();
    }
}

module_registry& module_registry::instance() {
    static module_registry registry;
    return registry;
}

void module_registry::register_module(std::unique_ptr<module> m) {
    if (m == nullptr) {
        return;
    }
    std::lock_guard<std::mutex> lock(mutex_);
    modules_.push_back(std::move(m));
}

module* module_registry::find(const std::string& name) const {
    std::lock_guard<std::mutex> lock(mutex_);
    for (const auto& m : modules_) {
        if (m->name() == name) {
            return m.get();
        }
    }
    return nullptr;
}

std::vector<module*> module_registry::all() const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<module*> out;
    out.reserve(modules_.size());
    for (const auto& m : modules_) {
        out.push_back(m.get());
    }
    return out;
}

bool module_registry::set_enabled(const std::string& name, bool on) {
    module* m = nullptr;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        for (const auto& candidate : modules_) {
            if (candidate->name() == name) {
                m = candidate.get();
                break;
            }
        }
    }
    if (m == nullptr) {
        return false;
    }
    m->set_enabled(on);
    return true;
}

void module_registry::tick_all(game::game_state& gs) {
    std::lock_guard<std::mutex> lock(mutex_);
    for (const auto& m : modules_) {
        if (m->enabled()) {
            m->on_tick(gs);
        }
    }
}

void module_registry::clear() {
    std::lock_guard<std::mutex> lock(mutex_);
    modules_.clear();
}

} // namespace woke::modules
