// ============================================================================
//  woke.wtf — tests/logger_test.cpp
//  End-to-end demonstration/verification of the logging headers:
//
//    1. platform.hpp  — get_time_str() format, get_tid() > 0
//    2. logger.hpp    — every level (trace..error) reaches stderr (default)
//                       with categories; captured fd2 is inspected afterwards
//    3. filesystem.hpp— get_timestamp_path() yields logs/YYYY-MM-DD_HH-MM-SS.log,
//                       file_logger_attach() REDIRECTS output into that file
//                       (stderr goes quiet), file_logger_detach() restores it
//    4. fatal          — exercised in a forked child: must terminate with
//                       SIGABRT (std::abort), while this process exits 0
//
//  Build:  g++ -std=c++20 -Wall -Wextra -Wpedantic -Isrc tests/logger_test.cpp
//          -o .cache/logger_test
// ============================================================================
#include <cctype>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include <fcntl.h>
#include <sys/stat.h>   // ::mkdir
#include <sys/wait.h>
#include <unistd.h>

#include "core/filesystem.hpp"
#include "core/logger.hpp"
#include "core/platform.hpp"

namespace {

int g_failures = 0;

void check(bool ok, const char* what) {
    std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", what);
    if (!ok) {
        ++g_failures;
    }
}

// Captured copy of fd2 (the test redirects stderr here at startup).
constexpr const char* kStderrCapture = ".cache/logger_test.stderr";

std::string read_all(const char* path) {
    std::FILE* f = std::fopen(path, "rb");
    if (f == nullptr) {
        return {};
    }
    std::string out;
    char buf[4096];
    std::size_t n = 0;
    while ((n = std::fread(buf, 1, sizeof buf, f)) > 0) {
        out.append(buf, n);
    }
    std::fclose(f);
    return out;
}

// logs/YYYY-MM-DD_HH-MM-SS.log — exact structural validation of the path.
bool is_timestamped_log_path(const char* p) {
    if (p == nullptr || std::strlen(p) != 28) {
        return false;
    }
    //  "logs/"  YYYY-MM-DD  "_"  HH-MM-SS  ".log"   (28 chars total)
    const bool prefix = std::strncmp(p, "logs/", 5) == 0;
    const bool date   = std::isdigit(static_cast<unsigned char>(p[5])) &&
                        std::isdigit(static_cast<unsigned char>(p[6])) &&
                        std::isdigit(static_cast<unsigned char>(p[7])) &&
                        std::isdigit(static_cast<unsigned char>(p[8])) && p[9] == '-' &&
                        std::isdigit(static_cast<unsigned char>(p[10])) &&
                        std::isdigit(static_cast<unsigned char>(p[11])) && p[12] == '-' &&
                        std::isdigit(static_cast<unsigned char>(p[13])) &&
                        std::isdigit(static_cast<unsigned char>(p[14]));
    const bool underscore = p[15] == '_';
    const bool time_part  = std::isdigit(static_cast<unsigned char>(p[16])) &&
                            std::isdigit(static_cast<unsigned char>(p[17])) && p[18] == '-' &&
                            std::isdigit(static_cast<unsigned char>(p[19])) &&
                            std::isdigit(static_cast<unsigned char>(p[20])) && p[21] == '-' &&
                            std::isdigit(static_cast<unsigned char>(p[22])) &&
                            std::isdigit(static_cast<unsigned char>(p[23]));
    const bool ext = std::strcmp(p + 24, ".log") == 0;
    return prefix && date && underscore && time_part && ext;
}

} // namespace

int main() {
    // ---- fd2 capture so the program can assert on its own stderr ----------
    std::printf("=== woke.wtf logger test ===\n");
    ::mkdir(".cache", 0755);
    const int cap_fd = ::open(kStderrCapture, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (cap_fd < 0 || ::dup2(cap_fd, STDERR_FILENO) < 0) {
        std::perror("stderr capture");
        return 2;
    }
    ::close(cap_fd);

    // ---- 1) platform.hpp ---------------------------------------------------
    char ts[40] = {};
    const std::size_t ts_len = woke::platform::get_time_str(ts, sizeof ts);
    check(ts_len == 25 && ts[0] == '[' && ts[24] == ']' && ts[11] == ' ' && ts[20] == '.',
          "platform::get_time_str -> [YYYY-MM-DD HH:MM:SS.mmm]");
    check(woke::platform::get_tid() > 0, "platform::get_tid() > 0");

    // ---- 2) all non-fatal levels -> stderr (default sink) ------------------
    WOKE_TRACE("render", "trace: draw-call suppression active=%d", 1);
    WOKE_DEBUG("jni",    "debug: cached method id for setScreen");
    WOKE_INFO("core",    "info: logger engine online");
    WOKE_WARN("config",  "warn: missing optional setting %s", "accent");
    WOKE_ERROR("hook",   "error: simulated failure code=%d", -7);

    // ---- 3) timestamped file + redirect ------------------------------------
    char path[512] = {};
    const bool got_path = woke::fs::get_timestamp_path(path, sizeof path, "logs", ".log");
    check(got_path && is_timestamped_log_path(path),
          "filesystem::get_timestamp_path -> logs/YYYY-MM-DD_HH-MM-SS.log");
    std::printf("    log path: %s\n", path);

    check(woke::fs::file_logger_attach(path),
          "filesystem::file_logger_attach redirects logging to the file");
    check(!woke::log::stderr_enabled(), "stderr muted while attached (redirect active)");

    WOKE_INFO("test",  "FILE-ONLY marker info line");
    WOKE_WARN("test",  "FILE-ONLY marker warn line");
    WOKE_ERROR("test", "FILE-ONLY marker error line");
    WOKE_DEBUG("test", "FILE-ONLY marker debug line");

    woke::fs::file_logger_detach();
    check(woke::log::stderr_enabled(), "stderr restored after file_logger_detach");

    WOKE_INFO("core", "post-detach line: back on stderr");

    // ---- 4) fatal in a forked child -> SIGABRT -----------------------------
    std::fflush(stdout);
    const pid_t pid = ::fork();
    if (pid == 0) {
        WOKE_FATAL("test", "fatal demonstration — child must abort here");
        ::_exit(99);  // unreachable: WOKE_FATAL aborts
    }
    int status = 0;
    ::waitpid(pid, &status, 0);
    check(WIFSIGNALED(status) && WTERMSIG(status) == SIGABRT,
          "fatal terminates via std::abort() (SIGABRT in child)");

    // ---- 5) inspect captured stderr + the generated log file ---------------
    const std::string err = read_all(kStderrCapture);
    check(err.find("[TRACE]") != std::string::npos &&
              err.find("[DEBUG]") != std::string::npos &&
              err.find("[ INFO]") != std::string::npos &&
              err.find("[ WARN]") != std::string::npos &&
              err.find("[ERROR]") != std::string::npos &&
              err.find("[FATAL]") != std::string::npos,
          "stderr received every level (TRACE..FATAL)");
    check(err.find("[core]") != std::string::npos &&
              err.find("[render]") != std::string::npos,
          "stderr lines carry categories");
    check(err.find("FILE-ONLY") == std::string::npos,
          "stderr did NOT receive file-redirected lines");
    check(err.find("post-detach line") != std::string::npos,
          "stderr resumed after detach");

    const std::string file = read_all(path);
    check(!file.empty(), "generated log file is readable");
    check(file.find("FILE-ONLY marker info line") != std::string::npos &&
              file.find("FILE-ONLY marker warn line") != std::string::npos &&
              file.find("FILE-ONLY marker error line") != std::string::npos &&
              file.find("FILE-ONLY marker debug line") != std::string::npos,
          "file received the redirected lines (all levels)");
    check(file.find("[tid:") != std::string::npos,
          "file lines carry [tid:...] from platform::get_tid()");
    check(file.find('\033') == std::string::npos,
          "file lines are plain text (no ANSI escapes)");
    check(file.find("post-detach line") == std::string::npos,
          "lines after detach did NOT land in the file");

    // ---- summary -----------------------------------------------------------
    std::printf("=== %s (%d failure%s) ===\n",
                g_failures == 0 ? "ALL TESTS PASSED" : "TESTS FAILED",
                g_failures, g_failures == 1 ? "" : "s");
    return g_failures == 0 ? 0 : 1;
}
