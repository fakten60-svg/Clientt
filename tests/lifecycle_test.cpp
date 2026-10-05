// ============================================================================
//  woke.wtf — tests/lifecycle_test.cpp
//  Dynamically loads libwoke.so (release preset — no ASan interposition) and
//  verifies the load-time bootstrap against the real file system:
//
//    1. constructor created a fresh  logs/YYYY-MM-DD_HH-MM-SS.log
//    2. logs/latest.log is a SYMBOLIC LINK resolving to that session file
//    3. session file permissions are sane (readable, not executable)
//    4. init confirmation messages are present in the file
//    5. dlclose() runs the destructor: session-end line is appended
//
//  Build:  g++ -std=c++20 -Wall -Wextra -Wpedantic tests/lifecycle_test.cpp
//          -o .cache/lifecycle_test -ldl
//  Run:    from the repo root —  ./.cache/lifecycle_test [path/to/libwoke.so]
// ============================================================================
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include <dirent.h>
#include <dlfcn.h>
#include <sys/stat.h>
#include <unistd.h>

namespace {

int g_failures = 0;

void check(bool ok, const char* what) {
    std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", what);
    if (!ok) {
        ++g_failures;
    }
}

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

bool contains(const std::string& hay, const std::string& needle) {
    return hay.find(needle) != std::string::npos;
}

// Session logs in logs/ (everything except latest.log), sorted for stable
// comparison across snapshots.
std::vector<std::string> list_session_logs() {
    std::vector<std::string> out;
    DIR* d = ::opendir("logs");
    if (d == nullptr) {
        return out;
    }
    while (dirent* e = ::readdir(d)) {
        const std::string name = e->d_name;
        if (name == "." || name == ".." || name == "latest.log") {
            continue;
        }
        if (name.size() > 4 && name.compare(name.size() - 4, 4, ".log") == 0) {
            out.push_back(name);
        }
    }
    ::closedir(d);
    return out;
}

// logs/YYYY-MM-DD_HH-MM-SS.log — exact structural validation (28 chars).
bool is_timestamped_log_name(const std::string& p) {
    if (p.size() != 23) {   // YYYY-MM-DD_HH-MM-SS.log
        return false;
    }
    const auto dig = [](char c) { return c >= '0' && c <= '9'; };
    return dig(p[0]) && dig(p[1]) && dig(p[2]) && dig(p[3]) && p[4] == '-' &&
           dig(p[5]) && dig(p[6]) && p[7] == '-' && dig(p[8]) && dig(p[9]) &&
           p[10] == '_' &&
           dig(p[11]) && dig(p[12]) && p[13] == '-' && dig(p[14]) && dig(p[15]) &&
           p[16] == '-' && dig(p[17]) && dig(p[18]) &&
           p.compare(19, 4, ".log") == 0;
}

// Wipe previous run artifacts so "a NEW file appeared" is deterministic.
void clean_logs_dir() {
    std::vector<std::string> stale = list_session_logs();
    for (const std::string& s : stale) {
        ::unlink(("logs/" + s).c_str());
    }
    ::unlink("logs/latest.log");
}

} // namespace

int main(int argc, char** argv) {
    const char* lib = (argc > 1) ? argv[1] : "build/linux-gcc-release/libwoke.so";
    std::printf("=== woke.wtf lifecycle test ===\n");
    std::printf("library: %s\n", lib);

    struct stat st {};
    if (::stat(lib, &st) != 0) {
        std::printf("[FAIL] library not found — build the release preset first\n");
        return 2;
    }
    if (::stat("logs", &st) != 0 && ::mkdir("logs", 0755) != 0) {
        std::perror("mkdir logs");
        return 2;
    }
    clean_logs_dir();

    // ---- 1) load: constructor must run --------------------------------------
    void* handle = ::dlopen(lib, RTLD_NOW | RTLD_LOCAL);
    if (handle == nullptr) {
        std::printf("[FAIL] dlopen: %s\n", ::dlerror());
        return 2;
    }
    check(true, "dlopen(libwoke.so) succeeded — constructor executed");

    const std::vector<std::string> created = list_session_logs();
    check(created.size() == 1 && is_timestamped_log_name(created[0]),
          "constructor created exactly one logs/YYYY-MM-DD_HH-MM-SS.log");
    if (created.empty()) {
        std::printf("=== TESTS FAILED (no session log) ===\n");
        return 1;
    }
    const std::string session_name = created[0];
    const std::string session_path = "logs/" + session_name;
    std::printf("    session log: %s\n", session_path.c_str());

    // ---- 2) latest.log must be a symlink resolving to the session file ------
    struct stat lstst {};
    const bool lstat_ok = ::lstat("logs/latest.log", &lstst) == 0;
    check(lstat_ok && S_ISLNK(lstst.st_mode), "logs/latest.log exists and is a symlink");

    char linkbuf[512] = {};
    const ssize_t nread = lstat_ok ? ::readlink("logs/latest.log", linkbuf, sizeof linkbuf - 1) : -1;
    const std::string target = (nread > 0) ? std::string(linkbuf, static_cast<std::size_t>(nread)) : "";
    check(target == session_name,
          "symlink target is the session file name (relative link)");

    struct stat resolved {};
    check(::stat("logs/latest.log", &resolved) == 0 && S_ISREG(resolved.st_mode),
          "latest.log resolves to a regular file");

    // ---- 3) permissions: readable, non-executable ---------------------------
    struct stat sess_st {};
    check(::stat(session_path.c_str(), &sess_st) == 0 && S_ISREG(sess_st.st_mode) &&
              ::access(session_path.c_str(), R_OK) == 0 &&
              (sess_st.st_mode & 0111) == 0,
          "session file readable and not executable");

    // ---- 4) initialization messages ----------------------------------------
    const std::string on_load = read_all(session_path.c_str());
    check(contains(on_load, "woke.wtf v") && contains(on_load, "native client loaded"),
          "init message: 'woke.wtf v... native client loaded'");
    check(contains(on_load, "session log attached: " + session_path),
          "init message: 'session log attached: <timestamped path>'");
    check(contains(on_load, "logger initialized"), "init message: 'logger initialized'");
    check(contains(on_load, "latest.log symlink:"), "init message: latest.log symlink line");
    check(contains(on_load, "[tid:"), "log lines carry platform::get_tid() [tid:...]");

    // ---- 5) unload: destructor must detach + append session-end -------------
    const bool closed = ::dlclose(handle) == 0;
    check(closed, "dlclose(libwoke.so) succeeded — destructor executed");

    const std::string on_unload = read_all(session_path.c_str());
    check(contains(on_unload, "libwoke unloading — detaching session logger"),
          "destructor message: 'libwoke unloading...'");
    check(contains(on_unload, "=== session log ended ==="),
          "destructor detached the logger (session-end marker flushed to file)");

    // File must still exist and the symlink must still resolve after unload.
    check(::stat("logs/latest.log", &resolved) == 0 && S_ISREG(resolved.st_mode),
          "latest.log still resolves after unload");

    std::printf("=== %s (%d failure%s) ===\n",
                g_failures == 0 ? "ALL TESTS PASSED" : "TESTS FAILED",
                g_failures, g_failures == 1 ? "" : "s");
    return g_failures == 0 ? 0 : 1;
}
