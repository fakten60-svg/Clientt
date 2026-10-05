// ============================================================================
//  woke.wtf — core/platform.hpp
//  Tiny Linux platform helpers the logger depends on:
//    * get_tid()      — kernel thread id (Linux gettid) for multi-thread logs
//    * get_time_str() — local-timezone timestamp "[YYYY-MM-DD HH:MM:SS.mmm]"
//
//  Both are allocation-free and bounded (snprintf family only).
// ============================================================================
#pragma once

#include <cstddef>
#include <cstdio>
#include <ctime>
#include <time.h>          // clock_gettime, localtime_r (POSIX)
#include <sys/syscall.h>   // SYS_gettid
#include <unistd.h>        // ::syscall

namespace woke::platform {

// Kernel thread id of the calling thread — cheap enough for every log line.
inline long get_tid() noexcept {
    return ::syscall(SYS_gettid);   // syscall() already returns long
}

// Formats the current local-system time as "[YYYY-MM-DD HH:MM:SS.mmm]".
// Bounded: never writes more than `cap` bytes (including the terminator).
// Returns the number of characters that would have been produced
// (snprintf semantics); 0 on failure.
inline std::size_t get_time_str(char* buf, std::size_t cap) noexcept {
    if (buf == nullptr || cap == 0) {
        return 0;
    }

    struct timespec ts {};
    ::clock_gettime(CLOCK_REALTIME, &ts);

    struct tm local {};
    if (::localtime_r(&ts.tv_sec, &local) == nullptr) {
        buf[0] = '\0';
        return 0;
    }

    const int millis = static_cast<int>(ts.tv_nsec / 1000000L);
    const int n = std::snprintf(buf, cap, "[%04d-%02d-%02d %02d:%02d:%02d.%03d]",
                                local.tm_year + 1900, local.tm_mon + 1, local.tm_mday,
                                local.tm_hour, local.tm_min, local.tm_sec, millis);
    if (n < 0) {
        buf[0] = '\0';
        return 0;
    }
    return static_cast<std::size_t>(n);
}

} // namespace woke::platform
