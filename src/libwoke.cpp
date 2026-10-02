// ============================================================================
//  woke.wtf — src/libwoke.cpp
//  libwoke.so lifecycle entry points, executed by the dynamic loader:
//
//    __attribute__((constructor))  — build a unique timestamped session log
//                                    under logs/, attach the logger to it,
//                                    symlink logs/latest.log -> session file,
//                                    then emit bootstrap confirmation lines.
//    __attribute__((destructor))   — detach the logger (flush + close file).
//
//  Both hooks run on arbitrary loader threads; the logger is allocation-free
//  and every write goes through stdio's internal lock, so early/late calls
//  are safe. All logger state is constant-initialized, so there is no
//  dynamic-initialization ordering problem with these attributes.
// ============================================================================
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <unistd.h>   // ::unlink, ::symlink

#include "core/filesystem.hpp"
#include "core/logger.hpp"

#ifndef WOKE_VERSION
#define WOKE_VERSION "0.1.0-dev"
#endif

namespace {

constexpr const char* kLogDir     = "logs";
constexpr const char* kLatestLink = "logs/latest.log";

// logs/latest.log -> "<timestamp>.log" (relative target, same directory).
// Replaces a stale link or a regular file left by an older build.
void update_latest_symlink(const char* session_path) {
    const char* base = std::strrchr(session_path, '/');
    base = (base != nullptr) ? base + 1 : session_path;

    if (::unlink(kLatestLink) != 0 && errno != ENOENT) {
        WOKE_WARN("core", "could not remove stale %s (errno=%d) — continuing",
                  kLatestLink, errno);
    }
    if (::symlink(base, kLatestLink) != 0) {
        WOKE_WARN("core", "symlink %s -> %s failed (errno=%d)",
                  kLatestLink, base, errno);
        return;
    }
    WOKE_INFO("core", "latest.log symlink: %s -> %s", kLatestLink, base);
}

} // namespace

// ----------------------------------------------------------------------------
// Runs when libwoke.so is loaded (dlopen / LD_PRELOAD).
// ----------------------------------------------------------------------------
__attribute__((constructor)) static void woke_on_load() {
    char session_path[512] = {};

    if (!woke::fs::get_timestamp_path(session_path, sizeof session_path, kLogDir, ".log")) {
        WOKE_ERROR("core", "bootstrap: cannot build session log path in %s — stderr only",
                   kLogDir);
        return;
    }
    if (!woke::fs::file_logger_attach(session_path)) {
        WOKE_ERROR("core", "bootstrap: cannot open session log '%s' (errno=%d) — stderr only",
                   session_path, errno);
        return;
    }

    update_latest_symlink(session_path);

    WOKE_INFO("core", "woke.wtf v%s native client loaded", WOKE_VERSION);
    WOKE_INFO("core", "session log attached: %s", session_path);
    WOKE_INFO("core", "logger initialized — core bootstrap complete");
}

// ----------------------------------------------------------------------------
// Runs on dlclose / process exit: flush and close the session file.
// ----------------------------------------------------------------------------
__attribute__((destructor)) static void woke_on_unload() {
    WOKE_INFO("core", "libwoke unloading — detaching session logger");
    woke::fs::file_logger_detach();
}
