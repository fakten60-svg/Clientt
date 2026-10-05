// ============================================================================
//  woke.wtf — tests/module_test.cpp
//  End-to-end verification of Phases 2-4 (GUI + modules + config):
//
//    1. compile the intermediary fixture classes (incl. GameOptions,
//       SimpleOption, Entity) onto a test classpath
//    2. dlopen libwoke.so FIRST (constructor defers init), then create a JVM
//       and call JNI_OnLoad -> full startup incl. module registry + config
//    3. module registry: 3 built-ins, names/categories, unknown-name probes
//    4. client-state layer against fixture objects wired through direct JNI:
//       gamma read/write round-trip, sprint set/query, fps read
//    5. Fullbright = read-modify-restore of gamma; Sprint = per-tick assert
//    6. config persistence: toggles persist immediately (JSON), keybind
//       round-trip, reload reapplies saved states
//    7. click-gui: open-state toggling + headless frame draw (module rows)
//    8. JNI_OnUnload tears modules + cache down; destructor ends the session
//
//  Build:  g++ -std=c++20 -Wall -Wextra -Wpedantic tests/module_test.cpp
//          -I src -I .cache/tools/jdk/include -I .cache/tools/jdk/include/linux
//          -o .cache/module_test -ldl
//  Run:    from the repo root —  ./.cache/module_test [path/to/libwoke.so]
// ============================================================================
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include <dlfcn.h>
#include <sys/stat.h>
#include <unistd.h>

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

bool near(double a, double b) {
    return std::fabs(a - b) < 1e-9;
}

} // namespace

int main(int argc, char** argv) {
    const char* lib = (argc > 1) ? argv[1] : "build/linux-gcc-release/libwoke.so";
    std::printf("=== woke.wtf module test ===\n");
    std::printf("library: %s\n", lib);

    // Redirect config I/O into .cache so the repo root stays clean.
    const char* config_path = ".cache/woke_config_test.json";
    ::setenv("WOKE_CONFIG_PATH", config_path, 1);
    ::unlink(config_path);

    // ---- 1) JDK + fixtures --------------------------------------------------
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

    const char* fixtures[] = {
        "tests/fixtures/src/net/minecraft/class_310.java",
        "tests/fixtures/src/net/minecraft/class_437.java",
        "tests/fixtures/src/net/minecraft/class_746.java",
        "tests/fixtures/src/net/minecraft/class_1297.java",
        "tests/fixtures/src/net/minecraft/class_315.java",
        "tests/fixtures/src/net/minecraft/class_7172.java",
    };
    std::string jc = "mkdir -p .cache/javac-out && " + javac + " -d .cache/javac-out";
    for (const char* f : fixtures) {
        jc += " ";
        jc += f;
    }
    jc += " >/dev/null 2>&1";
    check(::system(jc.c_str()) == 0, "javac compiled intermediary fixture classes");

    // ---- 2) dlopen first (deferred init), then JVM, then JNI_OnLoad ---------
    void* woke = ::dlopen(lib, RTLD_NOW | RTLD_LOCAL);
    if (woke == nullptr) {
        std::printf("[FAIL] dlopen(libwoke.so): %s\n", ::dlerror());
        return 2;
    }

    using status_fn = int (*)();
    using long_fn = long long (*)();
    using int_fn = int (*)();
    using str_fn = const char* (*)();
    using cstr_int_fn = int (*)(const char*, int);
    using cstr_fn = int (*)(const char*);
    using int_int_fn = int (*)(int);
    using str_arg_fn = const char* (*)(int);
    using tick_fn = void (*)();
    using dbl_fn = double (*)();
    using dbl_set_fn = int (*)(double);
    using set_int_fn = void (*)(int);
    using draw_fn = int (*)(int*, int*);
    using onload_fn = jint (*)(JavaVM*, void*);
    using onunload_fn = void (*)(JavaVM*, void*);

    auto f_status = reinterpret_cast<status_fn>(::dlsym(woke, "woke_jni_status"));
    auto f_onload = reinterpret_cast<onload_fn>(::dlsym(woke, "JNI_OnLoad"));
    auto f_onunload = reinterpret_cast<onunload_fn>(::dlsym(woke, "JNI_OnUnload"));
    auto f_mod_count = reinterpret_cast<int_fn>(::dlsym(woke, "woke_module_count"));
    auto f_mod_name = reinterpret_cast<str_arg_fn>(::dlsym(woke, "woke_module_name"));
    auto f_mod_cat = reinterpret_cast<str_arg_fn>(::dlsym(woke, "woke_module_category"));
    auto f_mod_enabled = reinterpret_cast<cstr_fn>(::dlsym(woke, "woke_module_enabled"));
    auto f_mod_set = reinterpret_cast<cstr_int_fn>(::dlsym(woke, "woke_module_set_enabled"));
    auto f_tick = reinterpret_cast<tick_fn>(::dlsym(woke, "woke_modules_tick"));
    auto f_ready = reinterpret_cast<int_fn>(::dlsym(woke, "woke_game_client_ready"));
    auto f_fps = reinterpret_cast<int_fn>(::dlsym(woke, "woke_game_current_fps"));
    auto f_gamma = reinterpret_cast<dbl_fn>(::dlsym(woke, "woke_game_gamma"));
    auto f_set_gamma = reinterpret_cast<dbl_set_fn>(::dlsym(woke, "woke_game_set_gamma"));
    auto f_sprinting = reinterpret_cast<int_fn>(::dlsym(woke, "woke_game_is_sprinting"));
    auto f_set_sprinting = reinterpret_cast<int_int_fn>(::dlsym(woke, "woke_game_set_sprinting"));
    auto f_gui_open = reinterpret_cast<int_fn>(::dlsym(woke, "woke_gui_is_open"));
    auto f_gui_set = reinterpret_cast<set_int_fn>(::dlsym(woke, "woke_gui_set_open"));
    auto f_gui_toggle = reinterpret_cast<tick_fn>(::dlsym(woke, "woke_gui_toggle"));
    auto f_gui_draw = reinterpret_cast<draw_fn>(::dlsym(woke, "woke_gui_draw_frame"));
    auto f_gui_draws = reinterpret_cast<long_fn>(::dlsym(woke, "woke_gui_draw_count"));
    auto f_cfg_load = reinterpret_cast<int_fn>(::dlsym(woke, "woke_config_load"));
    auto f_cfg_save = reinterpret_cast<int_fn>(::dlsym(woke, "woke_config_save"));
    auto f_cfg_path = reinterpret_cast<str_fn>(::dlsym(woke, "woke_config_path"));
    auto f_cfg_keybind = reinterpret_cast<int_fn>(::dlsym(woke, "woke_config_keybind"));
    auto f_cfg_set_keybind = reinterpret_cast<set_int_fn>(::dlsym(woke, "woke_config_set_keybind"));
    auto f_imgui = reinterpret_cast<int_fn>(::dlsym(woke, "woke_hook_imgui_initialized"));

    check(f_status && f_onload && f_onunload && f_mod_count && f_mod_name && f_mod_cat &&
              f_mod_enabled && f_mod_set && f_tick && f_ready && f_fps && f_gamma &&
              f_set_gamma && f_sprinting && f_set_sprinting && f_gui_open && f_gui_set &&
              f_gui_toggle && f_gui_draw && f_gui_draws && f_cfg_load && f_cfg_save &&
              f_cfg_path && f_cfg_keybind && f_cfg_set_keybind && f_imgui,
          "all exported module/game/gui/config symbols resolve via dlsym");
    if (!f_status || !f_onload || !f_mod_count || !f_mod_set || !f_gamma || !f_gui_draw) {
        return 2;
    }

    check(f_status() == 0, "state == 0 after dlopen: constructor deferred");

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
    if (create_vm(&vm, reinterpret_cast<void**>(&env), &vm_args) != JNI_OK ||
        vm == nullptr || env == nullptr) {
        std::printf("[FAIL] JNI_CreateJavaVM failed\n");
        return 2;
    }
    check(f_onload(vm, nullptr) == JNI_VERSION_1_8, "JNI_OnLoad returned JNI_VERSION_1_8");
    check(f_status() == 2, "state == 2 (kReady): modules + config + hooks initialized");
    check(f_imgui() == 1, "ImGui context created (ready for the click-gui)");

    // ---- 3) module registry -------------------------------------------------
    check(f_mod_count() == 3, "module registry holds 3 built-in modules");
    const char* n0 = f_mod_name(0);
    const char* n1 = f_mod_name(1);
    const char* n2 = f_mod_name(2);
    check(n0 && n1 && n2 && std::strcmp(n0, "HUD") == 0 &&
              std::strcmp(n1, "Fullbright") == 0 && std::strcmp(n2, "Sprint") == 0,
          "module names in registration order: HUD, Fullbright, Sprint");
    check(f_mod_cat(0) && f_mod_cat(2) && std::strcmp(f_mod_cat(0), "Render") == 0 &&
              std::strcmp(f_mod_cat(2), "Movement") == 0,
          "module categories: Render / Render / Movement");
    check(f_mod_enabled("DoesNotExist") == -1, "unknown module probes as -1");
    check(f_mod_enabled("Sprint") == 0, "Sprint starts disabled (no config file)");
    check(f_mod_name(99) == nullptr && f_mod_name(-1) == nullptr,
          "out-of-range module index returns null");

    // ---- 4) client-state layer against the fixture object graph -------------
    // Wire the fixture MinecraftClient singleton through direct JNI.
    jclass cls310 = env->FindClass("net/minecraft/class_310");
    check(cls310 != nullptr, "fixture class_310 (MinecraftClient) found");
    jmethodID ctor = (cls310 != nullptr) ? env->GetMethodID(cls310, "<init>", "()V") : nullptr;
    jfieldID inst = (cls310 != nullptr)
                        ? env->GetStaticFieldID(cls310, "field_instance", "Lnet/minecraft/class_310;")
                        : nullptr;
    check(ctor != nullptr && inst != nullptr, "fixture constructor + instance holder found");
    jobject client = (ctor != nullptr) ? env->NewObject(cls310, ctor) : nullptr;
    if (inst != nullptr && client != nullptr) {
        env->SetStaticObjectField(cls310, inst, client);
    }
    check(client != nullptr, "fixture MinecraftClient instance created + installed");

    check(f_ready() == 1, "game_state: client object graph reachable");
    check(f_fps() == 240, "game_state: getCurrentFps() reads the fixture value (240)");

    // gamma round-trip through GameOptions.gamma (SimpleOption)
    check(near(f_gamma(), 0.0), "gamma reads fixture default (0.0)");
    check(f_set_gamma(0.5) == 1 && near(f_gamma(), 0.5), "gamma write/read round-trip (0.5)");
    check(f_set_gamma(16.0) == 1 && near(f_gamma(), 16.0), "gamma write/read round-trip (16.0)");

    // sprint round-trip through Entity#setSprinting/isSprinting on the player
    check(f_set_sprinting(1) == 1 && f_sprinting() == 1, "set_sprinting(1) -> is_sprinting() == 1");
    check(f_set_sprinting(0) == 1 && f_sprinting() == 0, "set_sprinting(0) -> is_sprinting() == 0");

    // ---- 5) module behavior -------------------------------------------------
    check(f_set_gamma(0.5) == 1, "gamma parked at 0.5 before Fullbright");
    check(f_mod_set("Fullbright", 1) == 1, "Fullbright enabled");
    check(f_mod_enabled("Fullbright") == 1, "Fullbright reports enabled");
    check(near(f_gamma(), 16.0), "Fullbright raised gamma to 16.0");
    check(f_mod_set("Fullbright", 0) == 1, "Fullbright disabled");
    check(near(f_gamma(), 0.5), "Fullbright restored gamma to 0.5 (read-modify-restore)");

    check(f_mod_set("Sprint", 1) == 1, "Sprint enabled");
    f_tick();
    check(f_sprinting() == 1, "Sprint on_tick asserts sprinting on the player");
    check(f_mod_set("DoesNotExist", 1) == 0, "set_enabled on unknown module returns 0");

    // ---- 6) config persistence ---------------------------------------------
    check(std::strcmp(f_cfg_path(), config_path) == 0, "config path honors WOKE_CONFIG_PATH");
    const std::string cfg1 = read_all(config_path);
    check(!cfg1.empty(), "config file written on toggle");
    check(contains(cfg1, "\"Sprint\":true") && contains(cfg1, "\"Fullbright\":false"),
          "config JSON records module states");
    check(f_cfg_set_keybind != nullptr && f_cfg_keybind() == 62,
          "default keybind is keycode 62 (Right Shift)");
    f_cfg_set_keybind(21);
    check(f_cfg_keybind() == 21, "keybind set to 21");
    check(contains(read_all(config_path), "\"keybind\":21"), "keybind persisted to config JSON");

    check(f_mod_set("Sprint", 0) == 1, "Sprint disabled (state change to persist)");
    check(f_cfg_load() == 1, "config reloaded from disk");
    check(f_mod_enabled("Sprint") == 0, "reload reapplies saved module state (Sprint off)");
    check(f_cfg_save() == 1, "explicit config save succeeds");

    // ---- 7) click-gui -------------------------------------------------------
    check(f_gui_open() == 0, "click-gui starts closed");
    f_gui_toggle();
    check(f_gui_open() == 1, "click-gui toggled open");
    f_gui_toggle();
    check(f_gui_open() == 0, "click-gui toggled closed");
    f_gui_set(1);
    check(f_gui_open() == 1, "click-gui open via set");

    int modules_shown = 0;
    int toggles = 0;
    check(f_gui_draw(&modules_shown, &toggles) == 1, "headless click-gui frame rendered");
    check(modules_shown == 3, "click-gui drew all 3 module rows");
    check(f_gui_draws() >= 1, "gui draw counter advanced");
    f_gui_set(0);

    // ---- 8) teardown --------------------------------------------------------
    f_onunload(vm, nullptr);
    check(f_status() == 0, "JNI_OnUnload -> state == 0 (kDetached)");
    check(f_mod_count() == 0, "module registry cleared on unload");
    check(::dlclose(woke) == 0, "dlclose(libwoke.so) — destructor executed");

    const std::string log = read_all("logs/latest.log");
    check(contains(log, "no JVM present in this process — deferred to JNI_OnLoad"),
          "log: constructor deferred init");
    check(contains(log, "registered 3 built-in modules"), "log: built-in modules registered");
    check(contains(log, "Fullbright enabled"), "log: Fullbright enable recorded");
    check(contains(log, "Fullbright disabled"), "log: Fullbright disable recorded");
    check(contains(log, "click-gui opened"), "log: click-gui open recorded");
    check(contains(log, "config saved to"), "log: config persisted at least once");
    check(contains(log, "=== session log ended ==="), "log: destructor ended the session");

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
