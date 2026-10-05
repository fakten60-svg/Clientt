// ============================================================================
//  woke.wtf — core/logger.hpp
//  Zero-allocation, snprintf-based logging engine (header-only).
//
//    * Levels:   trace, debug, info, warn, error, fatal
//    * Category: every line carries a subsystem tag — [jni] [render] [config]…
//    * Default sink: stderr (ANSI-colored, line-flushed)
//    * File sink:    core/filesystem.hpp :: file_logger_attach() (plain text)
//    * fatal: flushes all active sinks, then calls std::abort()
//
//  Formatting goes through the bounded snprintf family (vsnprintf/snprintf)
//  into fixed stack buffers — no heap traffic, ever. Call sites:
//
//      WOKE_INFO("jni", "resolved %s -> %s", yarn_name, interim_name);
//      WOKE_ERROR("hook", "MH_CreateHook failed: %s", status_str);
//
//  Line format:
//      [2026-10-02 05:30:11.123] [ INFO] [render] [tid:42] message
// ============================================================================
#pragma once

#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "core/platform.hpp"

namespace woke::log {

// Log levels, ordered by severity (lower = chattier).
enum class level : int {
    trace = 0,
    debug = 1,
    info  = 2,
    warn  = 3,
    error = 4,
    fatal = 5,
};

namespace detail {

// ---- sink state ------------------------------------------------------------
inline std::FILE* g_file_sink   = nullptr;  // nullptr = file logging off
inline bool       g_stderr_sink = true;     // default sink: stderr
inline level      g_min_level   = level::trace;

inline const char* level_name(level lv) noexcept {
    switch (lv) {
        case level::trace: return "TRACE";
        case level::debug: return "DEBUG";
        case level::info:  return "INFO";
        case level::warn:  return "WARN";
        case level::error: return "ERROR";
        case level::fatal: return "FATAL";
    }
    return "?????";
}

// ANSI colors (stderr only — files stay plain): DEBUG gray, INFO cyan,
// WARN yellow, ERROR red, FATAL bold red, TRACE dim gray.
inline const char* level_color(level lv) noexcept {
    switch (lv) {
        case level::trace: return "\033[90m";
        case level::debug: return "\033[37m";
        case level::info:  return "\033[96m";
        case level::warn:  return "\033[93m";
        case level::error: return "\033[91m";
        case level::fatal: return "\033[1;91m";
    }
    return "\033[0m";
}

} // namespace detail

// ---- sink control (also used by core/filesystem.hpp) -----------------------
inline void set_file_sink(std::FILE* f) noexcept { detail::g_file_sink = f; }
inline std::FILE* file_sink() noexcept { return detail::g_file_sink; }

inline void set_stderr_enabled(bool on) noexcept { detail::g_stderr_sink = on; }
inline bool stderr_enabled() noexcept { return detail::g_stderr_sink; }

inline void set_min_level(level lv) noexcept { detail::g_min_level = lv; }
inline level min_level() noexcept { return detail::g_min_level; }

// ---- the single write path -------------------------------------------------
// Variadic, printf-style. Formats into fixed stack buffers via vsnprintf,
// writes one pre-formatted line per active sink, flushes, and aborts on fatal.
inline void write(level lv, const char* category, const char* fmt, ...) noexcept {
    if (lv < detail::g_min_level) {
        return;
    }

    const bool to_file = detail::g_file_sink != nullptr;
    const bool to_err  = detail::g_stderr_sink;

    if (!to_file && !to_err) {
        // No sink can ever silence a fatal — it must terminate the process.
        if (lv == level::fatal) {
            std::abort();
        }
        return;
    }

    char ts[40];
    platform::get_time_str(ts, sizeof ts);

    char msg[512];
    va_list args;
    va_start(args, fmt);
    std::vsnprintf(msg, sizeof msg, fmt, args);  // bounded: snprintf family
    va_end(args);

    const char* cat = (category != nullptr && category[0] != '\0') ? category : "-";
    const long  tid = platform::get_tid();
    char line[768];
    int n = 0;

    if (to_file) {  // plain text — no ANSI escapes in log files
        n = std::snprintf(line, sizeof line, "%s [%5s] [%s] [tid:%ld] %s",
                          ts, detail::level_name(lv), cat, tid, msg);
        if (n > 0) {
            if (n >= static_cast<int>(sizeof line)) {
                n = static_cast<int>(sizeof line) - 1;   // clamp truncated line
            }
            if (n < static_cast<int>(sizeof line) - 1) {
                line[n++] = '\n';
            }
            std::fwrite(line, 1, static_cast<std::size_t>(n), detail::g_file_sink);
            std::fflush(detail::g_file_sink);            // session logs are crash-safe
        }
    }

    if (to_err) {   // ANSI-colored for terminals
        n = std::snprintf(line, sizeof line, "%s%s [%5s] [%s] [tid:%ld] %s\033[0m",
                          detail::level_color(lv), ts, detail::level_name(lv),
                          cat, tid, msg);
        if (n > 0) {
            if (n >= static_cast<int>(sizeof line)) {
                n = static_cast<int>(sizeof line) - 1;
            }
            if (n < static_cast<int>(sizeof line) - 1) {
                line[n++] = '\n';
            }
            std::fwrite(line, 1, static_cast<std::size_t>(n), stderr);
            std::fflush(stderr);
        }
    }

    if (lv == level::fatal) {
        std::abort();   // non-negotiable: fatal terminates the process
    }
}

} // namespace woke::log

// ---- call-site macros -------------------------------------------------------
#define WOKE_TRACE(category, ...) ::woke::log::write(::woke::log::level::trace, category, __VA_ARGS__)
#define WOKE_DEBUG(category, ...) ::woke::log::write(::woke::log::level::debug, category, __VA_ARGS__)
#define WOKE_INFO(category, ...)  ::woke::log::write(::woke::log::level::info,  category, __VA_ARGS__)
#define WOKE_WARN(category, ...)  ::woke::log::write(::woke::log::level::warn,  category, __VA_ARGS__)
#define WOKE_ERROR(category, ...) ::woke::log::write(::woke::log::level::error, category, __VA_ARGS__)
#define WOKE_FATAL(category, ...) ::woke::log::write(::woke::log::level::fatal, category, __VA_ARGS__)
