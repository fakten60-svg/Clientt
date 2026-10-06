// ============================================================================
//  woke.wtf — tests/defer_test.cpp
//  Pure-injection path (the client is dlopen'd into an ALREADY RUNNING JVM, so
//  JNI_OnLoad never fires):
//
//    1. compile intermediary fixture classes (javac) onto a test classpath
//    2. create a real JVM (JNI_CreateJavaVM via dlopen of libjvm.so)
//    3. dlopen libwoke.so -> the constructor sees the JVM, must NOT do any JVM
//       work, and arms the deferred-init worker
//    4. never call JNI_OnLoad. Within the grace period the worker must finish
//       the full startup: thread attach, mappings.json parse, reflection cache,
//       built-in modules, config, hook engine
//    5. assert the log proves JNI_OnLoad was never involved
//    6. dlclose -> the destructor joins the worker and shuts everything down
//
//  Build:  g++ -std=c++20 -Wall -Wextra -Wpedantic tests/defer_test.cpp
//          -I src -I .cache/tools/jdk/include -I .cache/tools/jdk/include/linux
//          -pthread -o .cache/defer_test -ldl
//  Run:    from the repo root —  ./.cache/defer_test [path/to/libwoke.so]
//
//  WOKE_DEFER_GRACE_MS is set to 100 before dlopen so the test does not wait
//  out the production 1500 ms grace period.
// ============================================================================
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>

#include <dlfcn.h>
#include <sys/stat.h>

#include <jni.h>

namespace {

int g_failures = 0;

void check(bool ok, const char* what) {
    std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", what);
    if (!ok) {
        ++g_failures;
    }
}

bool file_exists(const std::string& p) {
    struct stat st {};
    return ::stat(p.c_str(), &st) == 0;
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

} // namespace

int main(int argc, char** argv) {
    setvbuf(stdout, nullptr, _IONBF, 0);
    const char* lib = (argc > 1) ? argv[1] : "build/linux-gcc-release/libwoke.so";
    std::printf("=== woke.wtf deferred-init (pure injection) test ===\n");
    std::printf("library: %s\n", lib);

    // ---- 1) locate the provisioned JDK -------------------------------------
    const char* jdk = std::getenv("WOKE_JAVA_HOME");
    if (jdk == nullptr || jdk[0] == '\0') {
        jdk = std::getenv("JAVA_HOME");
    }
    if (jdk == nullptr || jdk[0] == '\0') {
        jdk = ".cache/tools/jdk";
    }
    const std::string javac = std::string(jdk) + "/bin/javac";
    std::string jvm_lib;
    for (const char* rel : {"/lib/server/libjvm.so", "/lib/amd64/server/libjvm.so"}) {
        if (file_exists(std::string(jdk) + rel)) {
            jvm_lib = std::string(jdk) + rel;
            break;
        }
    }
    if (!file_exists(javac) || jvm_lib.empty()) {
        std::printf("[FAIL] JDK not found at '%s' (need bin/javac + libjvm.so)\n", jdk);
        return 2;
    }
    if (!file_exists(lib)) {
        std::printf("[FAIL] %s not found — build the release preset first\n", lib);
        return 2;
    }
    std::printf("jdk:    %s\n", jdk);

    // ---- 2) compile intermediary fixtures ----------------------------------
    const char* fixtures[] = {
        "tests/fixtures/src/net/minecraft/class_310.java",
        "tests/fixtures/src/net/minecraft/class_437.java",
        "tests/fixtures/src/net/minecraft/class_746.java",
        "tests/fixtures/src/net/minecraft/class_1297.java",
        "tests/fixtures/src/net/minecraft/class_315.java",
        "tests/fixtures/src/net/minecraft/class_7172.java",
        "tests/fixtures/src/net/minecraft/class_1309.java",
        "tests/fixtures/src/net/minecraft/class_1511.java",
        "tests/fixtures/src/net/minecraft/class_1657.java",
        "tests/fixtures/src/net/minecraft/class_1268.java",
        "tests/fixtures/src/net/minecraft/class_1299.java",
        "tests/fixtures/src/net/minecraft/class_239.java",
        "tests/fixtures/src/net/minecraft/class_3966.java",
        "tests/fixtures/src/net/minecraft/class_3965.java",
        "tests/fixtures/src/net/minecraft/class_1269.java",
        "tests/fixtures/src/net/minecraft/class_636.java",
        "tests/fixtures/src/net/minecraft/class_638.java",
        "tests/fixtures/src/net/minecraft/class_1661.java",
        "tests/fixtures/src/net/minecraft/class_1263.java",
        "tests/fixtures/src/net/minecraft/class_1792.java",
        "tests/fixtures/src/net/minecraft/class_1799.java",
        "tests/fixtures/src/net/minecraft/class_1802.java",
        "tests/fixtures/src/net/minecraft/class_2382.java",
        "tests/fixtures/src/net/minecraft/class_2338.java",
        "tests/fixtures/src/net/minecraft/class_2586.java",
        "tests/fixtures/src/net/minecraft/class_1684.java",
        "tests/fixtures/src/net/minecraft/class_2595.java",
        "tests/fixtures/src/net/minecraft/class_3719.java",
        "tests/fixtures/src/net/minecraft/class_2627.java",
        "tests/fixtures/src/net/minecraft/class_2614.java",
        "tests/fixtures/src/net/minecraft/class_2609.java",
        "tests/fixtures/src/net/minecraft/class_3722.java",
        "tests/fixtures/src/com/mojang/authlib/GameProfile.java",
    };
    std::string jc = "mkdir -p .cache/javac-out && " + javac + " -d .cache/javac-out";
    for (const char* f : fixtures) {
        jc += " ";
        jc += f;
    }
    jc += " >/dev/null 2>&1";
    check(::system(jc.c_str()) == 0, "javac compiled intermediary fixture classes");

    // ---- 3) create the JVM FIRST (this is the injection scenario) ----------
    void* jvm_handle = ::dlopen(jvm_lib.c_str(), RTLD_NOW | RTLD_GLOBAL);
    if (jvm_handle == nullptr) {
        std::printf("[FAIL] dlopen(libjvm.so): %s\n", ::dlerror());
        return 2;
    }
    using create_vm_fn = jint (*)(JavaVM**, void**, void*);
    auto create_vm = reinterpret_cast<create_vm_fn>(::dlsym(jvm_handle, "JNI_CreateJavaVM"));
    if (create_vm == nullptr) {
        std::printf("[FAIL] JNI_CreateJavaVM not found\n");
        return 2;
    }

    std::string cp_opt = "-Djava.class.path=.cache/javac-out";
    std::string xms = "-Xms32m";
    std::string xmx = "-Xmx128m";
    JavaVMOption opts[3] = {};
    opts[0].optionString = cp_opt.data();
    opts[1].optionString = xms.data();
    opts[2].optionString = xmx.data();
    JavaVMInitArgs vm_args = {};
    vm_args.version = JNI_VERSION_1_8;
    vm_args.nOptions = 3;
    vm_args.options = opts;
    vm_args.ignoreUnrecognized = JNI_TRUE;

    JavaVM* vm = nullptr;
    JNIEnv* env = nullptr;
    if (create_vm(&vm, reinterpret_cast<void**>(&env), &vm_args) != JNI_OK || vm == nullptr) {
        std::printf("[FAIL] JNI_CreateJavaVM failed\n");
        return 2;
    }
    std::printf("[INFO] JVM created — the client is about to be injected into it\n");

    // Short grace period so the test does not wait out the production 1500 ms.
    ::setenv("WOKE_DEFER_GRACE_MS", "100", 1);

    // ---- 4) dlopen libwoke.so with the JVM already running ------------------
    void* woke = ::dlopen(lib, RTLD_NOW | RTLD_LOCAL);
    if (woke == nullptr) {
        std::printf("[FAIL] dlopen(libwoke.so): %s\n", ::dlerror());
        return 2;
    }

    using status_fn = int (*)();
    using count_fn = long long (*)();
    using onload_fn = jint (*)(JavaVM*, void*);

    auto f_status = reinterpret_cast<status_fn>(::dlsym(woke, "woke_jni_status"));
    auto f_armed = reinterpret_cast<status_fn>(::dlsym(woke, "woke_jni_deferred_armed"));
    auto f_modules = reinterpret_cast<status_fn>(::dlsym(woke, "woke_module_count"));
    auto f_mappings = reinterpret_cast<count_fn>(::dlsym(woke, "woke_mappings_class_count"));
    auto f_cached = reinterpret_cast<count_fn>(::dlsym(woke, "woke_jni_cached_class_count"));
    auto f_onload = reinterpret_cast<onload_fn>(::dlsym(woke, "JNI_OnLoad"));

    check(f_status && f_armed && f_modules && f_mappings && f_cached && f_onload != nullptr,
          "all exported symbols resolve (JNI_OnLoad exists but stays uncalled)");
    if (!f_status || !f_armed || !f_modules || !f_mappings || !f_cached) {
        return 2;
    }

    check(f_status() == 0, "state == 0 after dlopen: no JVM work in the load constructor");
    check(f_armed() == 1, "constructor armed the deferred-init worker (pure injection)");

    // ---- 5) the worker must complete startup without JNI_OnLoad -------------
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
    while (f_status() != 2 && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    check(f_status() == 2, "deferred worker reached kReady (state == 2) with no JNI_OnLoad");
    check(f_modules() == 25, "built-in modules registered by the deferred worker");
    check(f_mappings() >= 9000, "mappings.json parsed by the deferred worker");
    check(f_cached() >= 1, "reflection cache populated by the deferred worker");
    std::printf("       cached: %lld classes\n", f_cached());

    // ---- 6) dlclose -> destructor joins the worker + shuts down -------------
    check(::dlclose(woke) == 0, "dlclose(libwoke.so) — destructor executed, worker joined");

    const std::string log = read_all("logs/latest.log");
    check(contains(log, "JVM present — deferring initialization to JNI_OnLoad"),
          "log: constructor saw the running JVM");
    check(contains(log, "deferred-init worker armed (grace"),
          "log: worker armed line");
    check(contains(log, "pure injection detected, running deferred initialization"),
          "log: worker ran the deferred startup");
    check(contains(log, "deferred initialization complete"),
          "log: deferred startup finished");
    check(!contains(log, "JNI_OnLoad complete"),
          "log: JNI_OnLoad was never invoked (pure-injection path)");
    check(contains(log, "ImGui context created (bare frame"),
          "log: hook engine initialized from the worker");
    check(contains(log, "JNI shutdown complete"), "log: destructor shut the client down");
    check(contains(log, "=== session log ended ==="), "log: session ended");

    // ---- 7) shut the fixture JVM down --------------------------------------
    using destroy_fn = jint (*)(JavaVM*);
    if (auto destroy_vm = reinterpret_cast<destroy_fn>(::dlsym(jvm_handle, "DestroyJavaVM"))) {
        destroy_vm(vm);
        std::printf("[INFO] DestroyJavaVM returned\n");
    }
    // libjvm.so intentionally not dlclosed (unsupported by HotSpot).

    std::printf("=== %s (%d failure%s) ===\n",
                g_failures == 0 ? "ALL TESTS PASSED" : "TESTS FAILED",
                g_failures, g_failures == 1 ? "" : "s");
    return g_failures == 0 ? 0 : 1;
}
