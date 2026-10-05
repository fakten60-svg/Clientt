// ============================================================================
//  woke.wtf — core/filesystem.hpp
//  File-side logging support (header-only):
//    * get_timestamp_path()  — builds "<dir>/YYYY-MM-DD_HH-MM-SS.log" and
//                              creates the directory tree (mkdir -p)
//    * file_logger_attach()  — REDIRECTS all logging output into that file
//                              (stderr is muted for the duration; the previous
//                              stderr state is restored on detach)
//    * file_logger_detach()  — closes the file, returns output to stderr
//
//  This is the multi-session log-file piece of the logger engine: every .so
//  load attaches a fresh date/time-stamped file under logs/, while the caller
//  may separately maintain a logs/latest.log mirror by attaching that path.
// ============================================================================
#pragma once

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <sys/stat.h>
#include <sys/types.h>

#include "core/logger.hpp"

namespace woke::fs {

namespace detail {
inline bool  g_attached      = false;
inline bool  g_prev_stderr   = true;   // stderr state to restore on detach
} // namespace detail

// mkdir -p for an absolute or relative directory path. EEXIST is success.
inline bool make_directories(const char* path) noexcept {
    if (path == nullptr || path[0] == '\0') {
        return false;
    }
    char buf[512];
    const std::size_t len = std::strlen(path);
    if (len >= sizeof buf) {
        return false;
    }
    std::memcpy(buf, path, len + 1);

    for (char* p = buf + 1; *p != '\0'; ++p) {
        if (*p == '/') {
            *p = '\0';
            if (::mkdir(buf, 0755) != 0 && errno != EEXIST) {
                return false;
            }
            *p = '/';
        }
    }
    if (::mkdir(buf, 0755) != 0 && errno != EEXIST) {
        return false;
    }
    return true;
}

// "<dir>/YYYY-MM-DD_HH-MM-SS<ext>" (default: "logs/2026-10-02_05-30-11.log").
// Creates `dir` when missing. Returns false if the path would not fit.
inline bool get_timestamp_path(char* out, std::size_t cap,
                               const char* dir  = "logs",
                               const char* ext  = ".log") noexcept {
    if (out == nullptr || cap == 0 || dir == nullptr || ext == nullptr) {
        return false;
    }
    if (!make_directories(dir)) {
        return false;
    }

    std::time_t now = std::time(nullptr);
    std::tm local {};
    if (::localtime_r(&now, &local) == nullptr) {
        return false;
    }

    char stamp[32];
    if (std::strftime(stamp, sizeof stamp, "%Y-%m-%d_%H-%M-%S", &local) == 0) {
        return false;
    }

    const std::size_t dlen = std::strlen(dir);
    const char* sep = (dlen > 0 && dir[dlen - 1] == '/') ? "" : "/";
    const int n = std::snprintf(out, cap, "%s%s%s%s", dir, sep, stamp, ext);
    return n > 0 && static_cast<std::size_t>(n) < cap;
}

// Closes the log file and returns logging to its previous (stderr) sink.
// Defined before file_logger_attach(), which calls it when re-attaching.
inline void file_logger_detach() noexcept {
    if (!detail::g_attached) {
        return;
    }
    log::write(log::level::info, "logger", "=== session log ended ===");  // to file

    std::FILE* f = log::file_sink();
    log::set_file_sink(nullptr);            // stop writing before fclose
    log::set_stderr_enabled(detail::g_prev_stderr);
    detail::g_attached = false;
    if (f != nullptr) {
        std::fclose(f);
    }
}

// Redirects all subsequent log output into `path` (created/truncated).
// stderr is muted while attached; file_logger_detach() restores it.
// Returns false — leaving state untouched — if the file cannot be opened.
inline bool file_logger_attach(const char* path) noexcept {
    if (path == nullptr || path[0] == '\0') {
        return false;
    }
    if (detail::g_attached) {
        file_logger_detach();   // re-attach: close the previous file first
    }

    std::FILE* f = std::fopen(path, "w");   // fresh session file
    if (f == nullptr) {
        return false;
    }

    detail::g_prev_stderr = log::stderr_enabled();
    log::set_file_sink(f);
    log::set_stderr_enabled(false);         // redirect: the file takes over
    detail::g_attached = true;

    // Session header — lands in the file only.
    log::write(log::level::info, "logger", "=== session log started: %s ===", path);
    return true;
}

inline bool file_logger_attached() noexcept { return detail::g_attached; }

} // namespace woke::fs
