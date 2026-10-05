// ============================================================================
//  woke.wtf — tests/hook_test.cpp
//  Verifies the hook engine end to end:
//
//    1. this test EXECUTABLE exports its own glXSwapBuffers (noinline) — a
//       stand-in for libGL's function, so MinHook patches a real function
//    2. create a JVM, dlopen libwoke.so -> JNI startup installs the detour
//       and creates the bare ImGui context
//    3. calling glXSwapBuffers must run OUR detour AND chain to the original
//       implementation body via the MinHook trampoline (both counters move)
//    4. GUI closed -> draw-call suppression (zero ImGui frames);
//       GUI open   -> bare ImGui frame runs; per-frame overhead < 0.5 ms
//    5. JNI_OnUnload tears hooks + context down; after dlclose the original
//       function is fully restored
//
//  Build:  g++ -std=c++20 -Wall -Wextra -Wpedantic tests/hook_test.cpp
//          -I src -I .cache/tools/jdk/include -I .cache/tools/jdk/include/linux
//          -rdynamic -o .cache/hook_test -ldl
//          (-rdynamic puts glXSwapBuffers into the exe's .dynsym so libwoke's
//           dlsym(RTLD_DEFAULT) finds it, exactly like libGL's exported symbol)
//  Run:    from the repo root —  ./.cache/hook_test [path/to/libwoke.so]
// ============================================================================
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include <dlfcn.h>

#include <jni.h>

namespace {

long long g_impl_calls = 0;   // the "real" swap implementation body
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

} // namespace

// The function under test — exported so libwoke's dlsym(RTLD_DEFAULT) finds
// it exactly like it would find libGL's glXSwapBuffers. noinline so calls
// always reach the (patched) entry point.
extern "C" __attribute__((visibility("default"))) void glXSwapBuffers(void* dpy,
                                                                     unsigned long drawable) {
    (void)dpy;
    (void)drawable;
    ++g_impl_calls;
}
__attribute__((noinline)) static void call_swap() {
    glXSwapBuffers(nullptr, 0UL);
}

int main(int argc, char** argv) {
    setvbuf(stdout, nullptr, _IONBF, 0);   // unbuffered: progress visible when hung
    const char* lib = (argc > 1) ? argv[1] : "build/linux-gcc-release/libwoke.so";
    std::printf("=== woke.wtf hook test ===\n");
    std::printf("library: %s\n", lib);

    // sanity: our stand-in must be dynamically visible for dlsym(RTLD_DEFAULT)
    if (::dlsym(RTLD_DEFAULT, "glXSwapBuffers") == nullptr) {
        std::printf("[FAIL] glXSwapBuffers not in dynamic symbol table — "
                    "rebuild the test with -rdynamic\n");
        return 2;
    }

    // ---- 1) fixture JVM (same provisioning as jni_test) ---------------------
    const char* jdk = std::getenv("WOKE_JAVA_HOME");
    if (jdk == nullptr || jdk[0] == '\0') {
        jdk = std::getenv("JAVA_HOME");
    }
    if (jdk == nullptr || jdk[0] == '\0') {
        jdk = ".cache/tools/jdk";
    }
    std::string jvm_lib;
    for (const char* rel : {"/lib/server/libjvm.so", "/lib/amd64/server/libjvm.so"}) {
        const std::string candidate = std::string(jdk) + rel;
        if (std::FILE* f = std::fopen(candidate.c_str(), "rb")) {
            std::fclose(f);
            jvm_lib = candidate;
            break;
        }
    }
    if (jvm_lib.empty()) {
        std::printf("[FAIL] libjvm.so not found under '%s'\n", jdk);
        return 2;
    }

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
    std::string xms = "-Xms32m";
    std::string xmx = "-Xmx128m";
    JavaVMOption opts[2] = {};
    opts[0].optionString = xms.data();
    opts[1].optionString = xmx.data();
    JavaVMInitArgs vm_args = {};
    vm_args.version = JNI_VERSION_1_8;
    vm_args.nOptions = 2;
    vm_args.options = opts;
    vm_args.ignoreUnrecognized = JNI_TRUE;
    JavaVM* vm = nullptr;
    JNIEnv* env = nullptr;
    if (create_vm(&vm, reinterpret_cast<void**>(&env), &vm_args) != JNI_OK) {
        std::printf("[FAIL] JNI_CreateJavaVM failed\n");
        return 2;
    }
    std::printf("[INFO] JVM created\n");

    // ---- 2) dlopen libwoke: constructor attaches + installs hooks ----------
    void* woke = ::dlopen(lib, RTLD_NOW | RTLD_LOCAL);
    if (woke == nullptr) {
        std::printf("[FAIL] dlopen(libwoke.so): %s\n", ::dlerror());
        return 2;
    }
    using status_fn = int (*)();
    using count_fn = long long (*)();
    using str_fn = const char* (*)();
    using set_fn = void (*)(int);
    using onload_fn = jint (*)(JavaVM*, void*);
    using onunload_fn = void (*)(JavaVM*, void*);

    auto f_status = reinterpret_cast<status_fn>(::dlsym(woke, "woke_hook_status"));
    auto f_imgui = reinterpret_cast<status_fn>(::dlsym(woke, "woke_hook_imgui_initialized"));
    auto f_target = reinterpret_cast<str_fn>(::dlsym(woke, "woke_hook_target"));
    auto f_hits = reinterpret_cast<count_fn>(::dlsym(woke, "woke_hook_present_count"));
    auto f_supp = reinterpret_cast<count_fn>(::dlsym(woke, "woke_hook_suppressed_count"));
    auto f_frames = reinterpret_cast<count_fn>(::dlsym(woke, "woke_hook_imgui_frame_count"));
    auto f_last = reinterpret_cast<count_fn>(::dlsym(woke, "woke_hook_last_frame_ns"));
    auto f_total = reinterpret_cast<count_fn>(::dlsym(woke, "woke_hook_total_frame_ns"));
    auto f_set = reinterpret_cast<set_fn>(::dlsym(woke, "woke_hook_set_gui_open"));
    auto f_onload = reinterpret_cast<onload_fn>(::dlsym(woke, "JNI_OnLoad"));
    auto f_onunload = reinterpret_cast<onunload_fn>(::dlsym(woke, "JNI_OnUnload"));

    check(f_status && f_imgui && f_target && f_hits && f_supp && f_frames && f_last &&
              f_total && f_set && f_onload && f_onunload,
          "all exported JNI/hook symbols resolve via dlsym");
    if (!f_status || !f_hits || !f_set || !f_onload) {
        return 2;
    }

    // ---- 3) constructor DEFERRED (no JVM work under the loader lock), -----
    //         JNI_OnLoad performs the real initialization
    check(f_status() == 0, "state == 0 after dlopen: constructor deferred (loader-lock safe)");
    check(f_onload(vm, nullptr) == JNI_VERSION_1_8, "JNI_OnLoad returned JNI_VERSION_1_8");

    // ---- 3) hook active + ImGui context ready ------------------------------
    check(f_status() == 1, "glXSwapBuffers hook is ACTIVE after load");
    check(f_imgui() == 1, "bare ImGui context initialized (ready for rendering)");
    check(std::strcmp(f_target(), "glXSwapBuffers") == 0, "hook target is glXSwapBuffers");

    // ---- 4) detour fires and chains to the original body -------------------
    call_swap();   // GUI closed by default
    check(f_hits() == 1, "detour executed (present count == 1)");
    check(g_impl_calls == 1, "original implementation called via trampoline (chain works)");
    check(f_supp() == 1 && f_frames() == 0,
          "GUI closed -> draw-call suppression (0 ImGui frames)");

    // ---- 5) GUI open -> bare ImGui frame -----------------------------------
    f_set(1);
    call_swap();
    check(f_hits() == 2 && g_impl_calls == 2, "second present: detour + original");
    check(f_frames() == 1, "GUI open -> bare ImGui frame executed (NewFrame+Render)");
    check(f_supp() == 1, "suppression counter did not advance while open");

    // ---- 6) draw suppression again + frame budget --------------------------
    f_set(0);
    call_swap();
    check(f_frames() == 1 && f_supp() == 2,
          "GUI closed again -> suppressed (ImGui frame count frozen)");
    const long long hits = f_hits();
    const long long avg_ns = (hits > 0) ? (f_total() / hits) : 0;
    check(avg_ns >= 0 && avg_ns < 500000,
          "per-frame overhead under 0.5 ms budget (avg < 500000 ns)");
    std::printf("       overhead: last=%lld ns avg=%lld ns over %lld presents\n",
                f_last(), avg_ns, hits);

    // ---- 7) JNI_OnUnload tears the hook engine down ------------------------
    f_onunload(vm, nullptr);
    check(f_status() == 0, "JNI_OnUnload removed the present hook");
    check(f_imgui() == 0, "JNI_OnUnload destroyed the ImGui context");

    // ---- 8) dlclose + original fully restored ------------------------------
    check(::dlclose(woke) == 0, "dlclose(libwoke.so) — destructor executed");
    const long long before = g_impl_calls;
    call_swap();
    check(g_impl_calls == before + 1,
          "after unload glXSwapBuffers runs the pristine original (no crash)");

    const std::string log = read_all("logs/latest.log");
    check(contains(log, "JVM present — deferring initialization to JNI_OnLoad"),
          "log: constructor deferred init despite JVM present");
    check(contains(log, "ImGui context created (bare frame"), "log: ImGui context created line");
    check(contains(log, "present hook installed: glXSwapBuffers"), "log: present hook installed line");
    check(contains(log, "hook engine initialized: glXSwapBuffers active"),
          "log: hook engine initialized line");
    check(contains(log, "MinHook engine shut down"), "log: MinHook engine shut down line");
    check(contains(log, "=== session log ended ==="), "log: session ended (destructor)");

    using destroy_fn = jint (*)(JavaVM*);
    if (auto destroy_vm = reinterpret_cast<destroy_fn>(::dlsym(jvm_handle, "DestroyJavaVM"))) {
        destroy_vm(vm);
    }
    // libjvm.so intentionally not dlclosed (unsupported); process exits now.

    std::printf("=== %s (%d failure%s) ===\n",
                g_failures == 0 ? "ALL TESTS PASSED" : "TESTS FAILED",
                g_failures, g_failures == 1 ? "" : "s");
    return g_failures == 0 ? 0 : 1;
}
