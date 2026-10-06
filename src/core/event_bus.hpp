// ============================================================================
//  woke.wtf — src/core/event_bus.hpp
//  Centralized, type-safe event bus (header-only).
//
//  Modules and core systems publish facts ("this module was toggled", "this
//  frame ticked") instead of calling each other, so a listener can be added
//  without touching any producer. Each event type gets its own channel, which
//  makes dispatch compile-time type safe and allocation-free on the hot path:
//
//      bus::subscribe<module_toggled>([](const module_toggled& e) { ... });
//      bus::emit(module_toggled{"Fullbright", true});
//
//  subscribe() may allocate (it appends a std::function) and is expected to
//  run during startup; emit() only walks an already-sized vector, so it is
//  safe to call every frame. Listeners must not subscribe/unsubscribe from
//  inside their own handler.
// ============================================================================
#pragma once

#include <functional>
#include <string>
#include <vector>

namespace woke::core {

namespace event_detail {

// One channel per event type. Function-local statics keep the storage alive
// for the process lifetime without any registry bookkeeping.
template <typename E>
struct channel {
    static std::vector<std::function<void(const E&)>>& sinks() {
        static std::vector<std::function<void(const E&)>> v;
        return v;
    }
};

// Registered once per event type so clear() can reach channels it has no type
// information about.
inline std::vector<std::function<void()>>& resetters() {
    static std::vector<std::function<void()>> v;
    return v;
}

} // namespace event_detail

class event_bus {
public:
    using token = unsigned;

    // Registers a listener for one event type. Returns a token that can be
    // passed to unsubscribe() — useful for tests.
    template <typename E, typename Fn>
    static token subscribe(Fn&& fn) {
        ensure_resettable<E>();
        auto& sinks = event_detail::channel<E>::sinks();
        sinks.emplace_back(std::forward<Fn>(fn));
        return static_cast<token>(sinks.size());
    }

    // Delivers `e` to every listener registered for its type, in registration
    // order. No allocation.
    template <typename E>
    static void emit(const E& e) {
        const auto& sinks = event_detail::channel<E>::sinks();
        for (std::size_t i = 0; i < sinks.size(); ++i) {
            if (sinks[i]) {
                sinks[i](e);
            }
        }
    }

    // Number of listeners for one event type (tests/diagnostics).
    template <typename E>
    static std::size_t listener_count() {
        return event_detail::channel<E>::sinks().size();
    }

    // Removes the listener returned by subscribe() for one type.
    template <typename E>
    static void unsubscribe(token t) {
        auto& sinks = event_detail::channel<E>::sinks();
        if (t >= 1 && t <= sinks.size()) {
            sinks[t - 1] = nullptr;   // slot stays, order stays stable
        }
    }

    // Drops every listener of every event type ever subscribed to.
    static void clear();

private:
    // First subscription of a type registers its resetter, so clear() can
    // reach channels it has no type information about.
    template <typename E>
    static void ensure_resettable() {
        static bool registered = false;
        if (!registered) {
            registered = true;
            event_detail::resetters().push_back(
                [] { event_detail::channel<E>::sinks().clear(); });
        }
    }
};

// ---- concrete client events ------------------------------------------------

// Published when a module's enabled state actually changed.
struct module_toggled {
    const char* name = nullptr;
    const char* category = nullptr;
    bool enabled = false;
};

// Published once per presented frame.
struct frame_tick {
    double delta_seconds = 0.0;
    long long present_index = 0;
};

// Published when the ClickGUI opens/closes.
struct gui_visibility_changed {
    bool open = false;
};

// Published when the config engine writes a file.
struct config_persisted {
    const char* path = nullptr;
};

inline void event_bus::clear() {
    for (auto& reset : event_detail::resetters()) {
        if (reset) {
            reset();
        }
    }
    event_detail::resetters().clear();
}

} // namespace woke::core
