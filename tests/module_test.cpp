// ============================================================================
//  woke.wtf — tests/module_test.cpp
//  End-to-end verification of Phases 2-4 (GUI + modules + config):
//
//    1. compile the intermediary fixture classes (incl. GameOptions,
//       SimpleOption, Entity) onto a test classpath
//    2. dlopen libwoke.so FIRST (constructor defers init), then create a JVM
//       and call JNI_OnLoad -> full startup incl. module registry + config
//    3. module registry: 11 built-ins across the Visual/Movement/Combat
//       categories, names/categories, category counts, unknown-name probes
//    4. client-state layer against fixture objects wired through direct JNI:
//       gamma read/write round-trip, sprint set/query, fps read
//    5. Fullbright = read-modify-restore of gamma; Sprint = per-tick assert
//    6. config persistence: toggles persist immediately (JSON), keybind
//       round-trip, reload reapplies saved states
//    7. click-gui: open-state toggling + headless frame draw (module rows),
//       category pages, search filter, grid view
//    8. BaseSetting persistence: values round-trip through the config file
//    9. UI subsystems: notifications, animation channels, event bus, settings
//       reset and per-module keybinds
//   10. game-thread task queue: post from this thread, run on drain
//   11. JNI_OnUnload tears modules + cache down; destructor ends the session
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
        "tests/fixtures/src/net/minecraft/class_1309.java",
        "tests/fixtures/src/net/minecraft/class_1657.java",
        "tests/fixtures/src/net/minecraft/class_1268.java",
        "tests/fixtures/src/net/minecraft/class_1299.java",
        "tests/fixtures/src/net/minecraft/class_239.java",
        "tests/fixtures/src/net/minecraft/class_3966.java",
        "tests/fixtures/src/net/minecraft/class_636.java",
        "tests/fixtures/src/net/minecraft/class_638.java",
        "tests/fixtures/src/net/minecraft/class_1661.java",
        "tests/fixtures/src/net/minecraft/class_1263.java",
        "tests/fixtures/src/net/minecraft/class_1792.java",
        "tests/fixtures/src/net/minecraft/class_1799.java",
        "tests/fixtures/src/net/minecraft/class_1802.java",
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
    using void_cstr_fn = void (*)(const char*);
    using cstr_cstr_fn = int (*)(const char*, const char*);
    using cstr_dbl_fn = double (*)(const char*, const char*);
    using cstr_cstr_dbl_fn = int (*)(const char*, const char*, double);

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
    auto f_fov = reinterpret_cast<int_fn>(::dlsym(woke, "woke_game_fov"));
    auto f_set_fov = reinterpret_cast<int_int_fn>(::dlsym(woke, "woke_game_set_fov"));
    auto f_sneak = reinterpret_cast<int_fn>(::dlsym(woke, "woke_game_is_sneaking"));
    auto f_set_sneak = reinterpret_cast<int_int_fn>(::dlsym(woke, "woke_game_set_sneaking"));
    auto f_page = reinterpret_cast<str_fn>(::dlsym(woke, "woke_gui_page"));
    auto f_select_page = reinterpret_cast<cstr_fn>(::dlsym(woke, "woke_gui_select_page"));
    auto f_set_search = reinterpret_cast<void_cstr_fn>(::dlsym(woke, "woke_gui_set_search"));
    auto f_set_grid = reinterpret_cast<set_int_fn>(::dlsym(woke, "woke_gui_set_grid"));
    auto f_grid = reinterpret_cast<int_fn>(::dlsym(woke, "woke_gui_grid"));
    auto f_expand = reinterpret_cast<void_cstr_fn>(::dlsym(woke, "woke_gui_expand"));
    auto f_gui_expanded = reinterpret_cast<str_fn>(::dlsym(woke, "woke_gui_expanded"));
    auto f_wants = reinterpret_cast<int_fn>(::dlsym(woke, "woke_gui_wants_frames"));
    auto f_notify = reinterpret_cast<int (*)(const char*, const char*, int)>(
        ::dlsym(woke, "woke_notify"));
    auto f_notif_active = reinterpret_cast<int_fn>(::dlsym(woke, "woke_notifications_active"));
    auto f_notif_pushed = reinterpret_cast<long_fn>(::dlsym(woke, "woke_notifications_pushed"));
    auto f_anim_channels = reinterpret_cast<int_fn>(::dlsym(woke, "woke_animations_channels"));
    auto f_anim_ticks = reinterpret_cast<long_fn>(::dlsym(woke, "woke_animations_ticks"));
    auto f_task_probe = reinterpret_cast<int_fn>(::dlsym(woke, "woke_tasks_post_probe"));
    auto f_task_pending = reinterpret_cast<long_fn>(::dlsym(woke, "woke_tasks_pending"));
    auto f_task_executed = reinterpret_cast<long_fn>(::dlsym(woke, "woke_tasks_executed"));
    auto f_task_drain = reinterpret_cast<int_fn>(::dlsym(woke, "woke_tasks_drain"));
    auto f_task_on_game = reinterpret_cast<int_fn>(::dlsym(woke, "woke_tasks_on_game_thread"));
    auto f_listeners = reinterpret_cast<cstr_fn>(::dlsym(woke, "woke_event_listeners"));
    auto f_cat_total = reinterpret_cast<int_fn>(::dlsym(woke, "woke_module_category_count_total"));
    auto f_cat_at = reinterpret_cast<str_arg_fn>(::dlsym(woke, "woke_module_category_at"));
    auto f_cat_count = reinterpret_cast<cstr_fn>(::dlsym(woke, "woke_module_count_in_category"));
    auto f_cat_enabled = reinterpret_cast<cstr_fn>(::dlsym(woke, "woke_module_enabled_in_category"));
    auto f_set_count = reinterpret_cast<cstr_fn>(::dlsym(woke, "woke_module_setting_count"));
    auto f_set_name = reinterpret_cast<const char* (*)(const char*, int)>(
        ::dlsym(woke, "woke_module_setting_name"));
    auto f_set_kind = reinterpret_cast<cstr_cstr_fn>(::dlsym(woke, "woke_module_setting_kind"));
    auto f_set_bool = reinterpret_cast<cstr_cstr_fn>(::dlsym(woke, "woke_module_setting_bool"));
    auto f_set_dbl = reinterpret_cast<cstr_dbl_fn>(::dlsym(woke, "woke_module_setting_double"));
    auto f_put_dbl = reinterpret_cast<cstr_cstr_dbl_fn>(
        ::dlsym(woke, "woke_module_set_setting_double"));
    auto f_mod_bind = reinterpret_cast<cstr_fn>(::dlsym(woke, "woke_module_keybind"));
    auto f_mod_set_bind = reinterpret_cast<cstr_int_fn>(::dlsym(woke, "woke_module_set_keybind"));
    auto f_reset_settings = reinterpret_cast<cstr_fn>(::dlsym(woke, "woke_config_reset_settings"));
    auto f_choices = reinterpret_cast<int (*)(const char*, const char*)>(
        ::dlsym(woke, "woke_module_setting_choice_count"));
    auto f_cool = reinterpret_cast<float (*)()>(::dlsym(woke, "woke_game_attack_cooldown"));
    auto f_tgt_hp = reinterpret_cast<float (*)()>(::dlsym(woke, "woke_game_target_health"));
    auto f_offhand_totem_probe = reinterpret_cast<int (*)()>(
        ::dlsym(woke, "woke_game_offhand_totem"));

    check(f_cool != nullptr && f_tgt_hp != nullptr && f_offhand_totem_probe != nullptr,
          "combat game-state exports resolve via dlsym");
    auto f_choice_label = reinterpret_cast<const char* (*)(const char*, const char*, int)>(
        ::dlsym(woke, "woke_module_setting_choice_label"));
    auto f_choice = reinterpret_cast<int (*)(const char*, const char*)>(
        ::dlsym(woke, "woke_module_setting_choice"));
    auto f_set_choice = reinterpret_cast<int (*)(const char*, const char*, int)>(
        ::dlsym(woke, "woke_module_set_setting_choice"));

    check(f_fov && f_set_fov && f_sneak && f_set_sneak && f_page && f_select_page && f_notify &&
              f_task_probe && f_listeners && f_set_count && f_set_dbl && f_put_dbl &&
              f_mod_set_bind && f_reset_settings && f_expand && f_gui_expanded,
          "all new subsystem exports resolve via dlsym");
    check(f_choices && f_choice_label && f_choice && f_set_choice,
          "mode-dropdown setting exports resolve via dlsym");

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
    check(f_mod_count() == 11, "module registry holds 11 built-in modules");
    const char* n0 = f_mod_name(0);
    const char* n1 = f_mod_name(1);
    const char* n2 = f_mod_name(2);
    const char* n3 = f_mod_name(3);
    const char* n4 = f_mod_name(4);
    const char* n5 = f_mod_name(5);
    const char* n6 = f_mod_name(6);
    const char* n7 = f_mod_name(7);
    const char* n8 = f_mod_name(8);
    const char* n9 = f_mod_name(9);
    check(n0 && n1 && n2 && n3 && n4 && n5 && n6 && n7 && n8 && n9 &&
              std::strcmp(n0, "HUD") == 0 && std::strcmp(n1, "Fullbright") == 0 &&
              std::strcmp(n2, "Zoom") == 0 && std::strcmp(n3, "Sprint") == 0 &&
              std::strcmp(n4, "Sneak") == 0 && std::strcmp(n5, "Target HUD") == 0 &&
              std::strcmp(n6, "Attack Cooldown") == 0 &&
              std::strcmp(n7, "Auto Clicker") == 0 &&
              std::strcmp(n8, "KillAura") == 0 && std::strcmp(n9, "W-Tap") == 0 &&
              f_mod_name(10) != nullptr && std::strcmp(f_mod_name(10), "Auto Totem") == 0 &&
              f_mod_name(11) == nullptr,
          "module names: HUD, Fullbright, Zoom, Sprint, Sneak, Target HUD, "
          "Attack Cooldown, Auto Clicker, KillAura, W-Tap");
    check(f_mod_cat(0) && f_mod_cat(3) && f_mod_cat(5) &&
              std::strcmp(f_mod_cat(0), "Visual") == 0 &&
              std::strcmp(f_mod_cat(3), "Movement") == 0 &&
              std::strcmp(f_mod_cat(5), "Combat") == 0,
          "module categories: Combat (Target HUD/...), Visual (HUD/...), Movement (Sprint/...)");
    check(f_cat_total() == 6 && f_cat_at(0) && std::strcmp(f_cat_at(0), "Combat") == 0 &&
              std::strcmp(f_cat_at(5), "Visual") == 0,
          "the six spec categories are exposed in display order");
    check(f_cat_count("Visual") == 3 && f_cat_count("Movement") == 2 &&
              f_cat_count("Combat") == 6 && f_cat_count("Mace") == 0 &&
              f_cat_count("Misc") == 0 && f_cat_count("Spear") == 0,
          "per-category module counts (Combat 6, Visual 3, Movement 2, rest 0)");
    check(f_cat_enabled("Visual") == 0 && f_cat_enabled("Movement") == 0,
          "no module is enabled before any toggle (fresh config)");
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

    // fov round-trip through GameOptions.fov (Integer-boxed SimpleOption)
    check(f_fov() == 70, "fov reads the fixture default (70)");
    check(f_set_fov(30) == 1 && f_fov() == 30, "fov write/read round-trip (30)");

    // sneaking round-trip through Entity#setSneaking/isSneaking
    check(f_set_sneak(1) == 1 && f_sneak() == 1, "set_sneaking(1) -> is_sneaking() == 1");
    check(f_set_sneak(0) == 1 && f_sneak() == 0, "set_sneaking(0) -> is_sneaking() == 0");

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

    // Zoom = read-modify-restore of the fov video setting
    check(f_set_fov(70) == 1, "fov parked at 70 before Zoom");
    check(f_mod_set("Zoom", 1) == 1, "Zoom enabled");
    check(f_fov() == 30, "Zoom narrowed fov to its default setting (30)");
    check(f_mod_set("Zoom", 0) == 1, "Zoom disabled");
    check(f_fov() == 70, "Zoom restored fov to 70 (read-modify-restore)");

    // Sneak = per-tick assert on Entity#setSneaking
    check(f_mod_set("Sneak", 1) == 1, "Sneak enabled");
    f_tick();
    check(f_sneak() == 1, "Sneak on_tick asserts sneaking on the player");
    check(f_mod_set("Sneak", 0) == 1, "Sneak disabled");

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
    check(std::strcmp(f_page(), "Visual") == 0, "dashboard opens on the Visual category page");
    check(modules_shown == 3, "Visual page drew its 3 module cards");
    check(f_gui_draws() >= 1, "gui draw counter advanced");

    check(f_select_page("Movement") == 1 && std::strcmp(f_page(), "Movement") == 0,
          "sidebar page switch to Movement");
    check(f_gui_draw(&modules_shown, &toggles) == 1 && modules_shown == 2,
          "Movement page drew its 2 module cards");
    check(f_select_page("Combat") == 1, "sidebar page switch to the Combat category");
    check(f_gui_draw(&modules_shown, &toggles) == 1 && modules_shown == 6,
          "Combat page draws its 6 module cards");
    check(f_select_page("Mace") == 1, "sidebar page switch to the empty Mace category");
    check(f_gui_draw(&modules_shown, &toggles) == 1 && modules_shown == 0,
          "empty category draws no cards (0-badge section)");
    check(f_select_page("NoSuchPage") == 0, "unknown page name is rejected");

    check(f_select_page("Visual") == 1, "back on the Visual page");
    f_set_grid(1);
    check(f_grid() == 1 && f_gui_draw(&modules_shown, &toggles) == 1,
          "grid view renders the same cards");
    f_set_grid(0);
    f_set_search("zoom");
    check(f_gui_draw(&modules_shown, &toggles) == 1 && modules_shown == 1,
          "search filter narrows the list to the matching module");
    f_set_search("");
    f_expand("Fullbright");
    check(std::strcmp(f_gui_expanded(), "Fullbright") == 0,
          "card expansion follows the chevron state");
    check(f_gui_draw(&modules_shown, &toggles) == 1 && modules_shown == 3,
          "expanded card keeps all Visual cards visible");
    f_gui_set(0);

    // ---- 7b) BaseSetting persistence ---------------------------------------
    check(f_set_count("Fullbright") == 2, "Fullbright declares 2 typed settings");
    check(f_set_name("Fullbright", 0) && std::strcmp(f_set_name("Fullbright", 0), "Gamma") == 0,
          "first Fullbright setting is Gamma");
    check(f_set_kind("Fullbright", "Gamma") == 2, "Gamma is a decimal setting (kind 2)");
    check(f_set_kind("Fullbright", "Restore") == 0, "Restore is a boolean setting (kind 0)");
    check(f_set_dbl("Fullbright", "Gamma") == 16.0, "Gamma defaults to 16.0");
    check(f_set_bool("Fullbright", "Restore") == 1, "Restore defaults to true");
    check(f_put_dbl("Fullbright", "Gamma", 8.0) == 1, "Gamma set to 8.0");
    check(f_set_dbl("Fullbright", "Gamma") == 8.0, "Gamma read back as 8.0");
    check(f_cfg_save() == 1, "config saved with the new setting value");
    check(contains(read_all(config_path), "\"Gamma\":8.0"),
          "config JSON persists the BaseSetting value");
    check(f_put_dbl("Fullbright", "Gamma", 20.0) == 1, "Gamma changed again (20.0)");
    check(f_cfg_load() == 1, "config reloaded");
    check(f_set_dbl("Fullbright", "Gamma") == 8.0, "reload reapplies the persisted setting");
    check(f_reset_settings("Fullbright") == 1, "setting reset restores exactly one value");
    check(f_set_dbl("Fullbright", "Gamma") == 16.0, "reset restored the Gamma default");

    // ---- 7b2) mode dropdown setting (HUD Corner) ----------------------------
    // Corner is the spec's Mode Dropdown kind: integer-backed with named
    // choices, edited through a combo box instead of a numeric slider.
    check(f_set_count("HUD") == 3, "HUD declares 3 typed settings");
    check(f_set_kind("HUD", "Corner") == 1, "Corner is an integer-backed setting (kind 1)");
    check(f_choices("HUD", "Corner") == 4, "Corner exposes its 4 named dropdown choices");
    check(f_choice_label("HUD", "Corner", 0) != nullptr &&
              std::strcmp(f_choice_label("HUD", "Corner", 0), "Top Left") == 0 &&
              f_choice_label("HUD", "Corner", 1) != nullptr &&
              std::strcmp(f_choice_label("HUD", "Corner", 1), "Top Right") == 0,
          "Corner choice labels: Top Left / Top Right at 0/1");
    check(f_choice_label("HUD", "Corner", 4) == nullptr,
          "out-of-range choice index returns null");
    check(f_choice("HUD", "Corner") == 0, "Corner defaults to index 0 (Top Left)");
    check(f_set_choice("HUD", "Corner", 3) == 1, "Corner selectable by index");
    check(f_choice("HUD", "Corner") == 3, "Corner selection read back as 3");
    check(f_cfg_save() == 1, "config saved with the dropdown selection");
    check(contains(read_all(config_path), "\"Corner\":3"),
          "config JSON persists the dropdown selection");
    check(f_set_choice("HUD", "Corner", 9) == 0, "out-of-range selection is rejected");
    check(f_choice("HUD", "Corner") == 3, "rejected selection leaves the value unchanged");
    check(f_choice("Fullbright", "Gamma") == -1, "non-dropdown setting reports -1");
    check(f_choices("Fullbright", "Gamma") == 0, "non-dropdown exposes 0 choices");
    check(f_reset_settings("HUD") == 1, "HUD setting reset works");
    check(f_choice("HUD", "Corner") == 0, "reset restores the dropdown default (Top Left)");

    // ---- 7c) per-module keybinds -------------------------------------------
    check(f_mod_bind("Sprint") == 0, "Sprint starts without a keybind");
    check(f_mod_set_bind("Sprint", 33) == 1 && f_mod_bind("Sprint") == 33,
          "Sprint keybind set to keycode 33");
    check(contains(read_all(config_path), "\"Sprint\":33"),
          "keybind persisted into the config JSON");
    check(f_mod_set_bind("Sprint", 0) == 1 && f_mod_bind("Sprint") == 0,
          "keybind cleared with 0");

    // ---- 7d) notifications, animation engine, event bus --------------------
    const long long pushed_before = f_notif_pushed();
    check(f_notify("Test", "toast body", 1) == 1, "toast pushed through the exported API");
    check(f_notify("Second", "toast body", 0) == 1, "second toast pushed");
    check(f_notif_pushed() == pushed_before + 2, "notification queue counted both toasts");
    check(f_notif_active() >= 2, "two toasts are active");
    f_gui_set(0);
    check(f_wants == nullptr || f_wants() == 1,
          "dirty GUI state keeps requesting frames (toasts/open animation)");
    check(f_anim_channels() > 0, "animation controller has registered channels");
    check(f_gui_draw(&modules_shown, &toggles) == 1, "toast-only frame still renders");
    check(f_anim_ticks() >= 1, "animation controller ticked at least once");
    check(f_listeners("module_toggled") >= 1,
          "module_toggled listener installed by the dashboard");
    check(f_listeners("frame_tick") == 0, "frame_tick has no listeners yet (decoupled)");
    check(f_listeners("nope") == -1, "unknown event name reports -1");

    // ---- 7d2) combat client-state (Target HUD / Auto Clicker paths) ---------
    // Wire the combat fixture graph: interactionManager + crosshairTarget.
    jclass cls636 = env->FindClass("net/minecraft/class_636");
    jclass cls3966 = env->FindClass("net/minecraft/class_3966");
    jclass cls1309 = env->FindClass("net/minecraft/class_1309");
    check(cls636 != nullptr && cls3966 != nullptr && cls1309 != nullptr,
          "combat fixture classes found");
    jmethodID ctor636 =
        (cls636 != nullptr) ? env->GetMethodID(cls636, "<init>", "()V") : nullptr;
    jmethodID ctor3966 = (cls3966 != nullptr)
                             ? env->GetMethodID(cls3966, "<init>", "(Lnet/minecraft/class_1297;)V")
                             : nullptr;
    jmethodID ctor1309 =
        (cls1309 != nullptr) ? env->GetMethodID(cls1309, "<init>", "()V") : nullptr;
    jfieldID fid_mgr = env->GetStaticFieldID(cls310, "field_instance", "Lnet/minecraft/class_310;");
    check(ctor636 != nullptr && ctor3966 != nullptr && ctor1309 != nullptr &&
              fid_mgr != nullptr,
          "combat fixture constructors + client holder found");
    jobject client_ref = env->GetStaticObjectField(cls310, fid_mgr);
    check(client_ref != nullptr, "fixture client instance reachable for combat wiring");
    jmethodID get_attacks = nullptr;   // hoisted: section 7d3 also reads attacks
    int attacks_before = 0;
    if (client_ref != nullptr && ctor636 != nullptr && ctor3966 != nullptr) {
        jobject manager = env->NewObject(cls636, ctor636);
        jfieldID fid_1761 = env->GetFieldID(cls310, "field_1761", "Lnet/minecraft/class_636;");
        jfieldID fid_1765 = env->GetFieldID(cls310, "field_1765", "Lnet/minecraft/class_239;");
        check(manager != nullptr && fid_1761 != nullptr && fid_1765 != nullptr,
              "interactionManager fixture + crosshair fields resolved");
        if (manager != nullptr && fid_1761 != nullptr && fid_1765 != nullptr) {
            env->SetObjectField(client_ref, fid_1761, manager);

            get_attacks = env->GetStaticMethodID(cls636, "attackCount", "()I");
            check(get_attacks != nullptr, "fixture attack counter reachable");
            attacks_before = (get_attacks != nullptr)
                                 ? env->CallStaticIntMethod(cls636, get_attacks)
                                 : 0;

            // Attack cooldown export: fixture player returns 1.0 by default.
            check(near(f_cool(), 1.0), "attack cooldown reads the fixture value (1.0)");

            // No target -> client_attack refuses; target_health is -1.
            check(f_tgt_hp() == -1.0f, "no crosshair target -> target health is -1");

            // Crosshair on a living entity -> Auto Clicker attacks exactly once
            // per charge + rate window (CPS parked at 1 to keep timing stable).
            jobject victim = env->NewObject(cls1309, ctor1309);
            jobject hit = (victim != nullptr) ? env->NewObject(cls3966, ctor3966, victim)
                                              : nullptr;
            check(victim != nullptr && hit != nullptr, "victim entity + hit result created");
            if (hit != nullptr) {
                env->SetObjectField(client_ref, fid_1765, hit);
                check(near(f_tgt_hp(), 20.0),
                      "target health reads the fixture value (20.0) through the crosshair raycast");
                f_put_dbl("Auto Clicker", "CPS", 1.0);
                check(f_mod_set("Auto Clicker", 1) == 1, "Auto Clicker enabled");
                f_tick();
                const int after_first = (get_attacks != nullptr)
                                            ? env->CallStaticIntMethod(cls636, get_attacks)
                                            : 0;
                check(after_first == attacks_before + 1,
                      "Auto Clicker performed exactly one vanilla attack");
                f_tick();
                f_tick();
                const int after_more = (get_attacks != nullptr)
                                           ? env->CallStaticIntMethod(cls636, get_attacks)
                                           : 0;
                check(after_more == attacks_before + 1,
                      "CPS rate limit holds (no attack within the 1 s window)");

                // Disable/enable resets the rate window (on_disable clears it),
                // so each guard below is checked with a fresh timer.
                auto reset_timer = [&] {
                    f_mod_set("Auto Clicker", 0);
                    f_mod_set("Auto Clicker", 1);
                };

                // Crosshair on the local player -> self-attack refused.
                jobject self_hit =
                    env->NewObject(cls3966, ctor3966,
                                   env->GetObjectField(client_ref,
                                                       env->GetFieldID(cls310, "field_1724",
                                                                       "Lnet/minecraft/class_746;")));
                if (self_hit != nullptr) {
                    env->SetObjectField(client_ref, fid_1765, self_hit);
                    reset_timer();
                    f_tick();
                    check((get_attacks != nullptr)
                              ? env->CallStaticIntMethod(cls636, get_attacks) == after_more
                              : 1,
                          "Auto Clicker never attacks the local player");
                    env->DeleteLocalRef(self_hit);
                }

                // Crosshair on a block/miss (plain HitResult) -> no attack.
                jclass cls239 = env->FindClass("net/minecraft/class_239");
                jmethodID ctor239 =
                    (cls239 != nullptr) ? env->GetMethodID(cls239, "<init>", "()V") : nullptr;
                if (cls239 != nullptr && ctor239 != nullptr) {
                    jobject miss = env->NewObject(cls239, ctor239);
                    env->SetObjectField(client_ref, fid_1765, miss);
                    reset_timer();
                    f_tick();
                    check((get_attacks != nullptr)
                              ? env->CallStaticIntMethod(cls636, get_attacks) == after_more
                              : 1,
                          "non-entity crosshair target is ignored");
                    env->DeleteLocalRef(miss);
                }

                check(f_mod_set("Auto Clicker", 0) == 1, "Auto Clicker disabled again");
                env->SetObjectField(client_ref, fid_1765, nullptr);
                env->DeleteLocalRef(hit);
            }
            if (victim != nullptr) {
                env->DeleteLocalRef(victim);
            }
            env->DeleteLocalRef(manager);
        }
    }

    // ---- 7d3) KillAura / W-Tap / Auto Totem ----------------------------------
    // Wire the world fixture with a victim + a far entity, then drive KillAura.
    jclass cls638 = env->FindClass("net/minecraft/class_638");
    jclass cls1657 = env->FindClass("net/minecraft/class_1657");
    jclass cls1661 = env->FindClass("net/minecraft/class_1661");
    jclass cls1799 = env->FindClass("net/minecraft/class_1799");
    jclass cls1802 = env->FindClass("net/minecraft/class_1802");
    jmethodID ctor638 =
        (cls638 != nullptr) ? env->GetMethodID(cls638, "<init>", "()V") : nullptr;
    jmethodID add_entity =
        (cls638 != nullptr)
            ? env->GetMethodID(cls638, "addFixtureEntity", "(Lnet/minecraft/class_1297;)V")
            : nullptr;
    jfieldID fid_1687 = env->GetFieldID(cls310, "field_1687", "Lnet/minecraft/class_638;");
    jfieldID fid_1724 = env->GetFieldID(cls310, "field_1724", "Lnet/minecraft/class_746;");
    jfieldID fid_inventory =
        (cls1657 != nullptr)
            ? env->GetFieldID(cls1657, "field_inventory", "Lnet/minecraft/class_1661;")
            : nullptr;
    jfieldID fid_offhand =
        (cls1661 != nullptr)
            ? env->GetFieldID(cls1661, "field_offhand", "Lnet/minecraft/class_1799;")
            : nullptr;
    jfieldID fid_totem =
        (cls1802 != nullptr) ? env->GetStaticFieldID(cls1802, "field_8288", "Lnet/minecraft/class_1792;")
                             : nullptr;
    jmethodID ctor1661 =
        (cls1661 != nullptr) ? env->GetMethodID(cls1661, "<init>", "()V") : nullptr;
    jmethodID ctor1799 =
        (cls1799 != nullptr) ? env->GetMethodID(cls1799, "<init>", "(Lnet/minecraft/class_1792;)V")
                             : nullptr;
    check(ctor638 != nullptr && add_entity != nullptr && fid_1687 != nullptr &&
              fid_1724 != nullptr && fid_inventory != nullptr && fid_offhand != nullptr &&
              fid_totem != nullptr && ctor1661 != nullptr && ctor1799 != nullptr,
          "world + inventory fixture plumbing resolved");
    if (ctor638 != nullptr && add_entity != nullptr && fid_1687 != nullptr &&
        fid_inventory != nullptr && fid_offhand != nullptr && fid_totem != nullptr) {
        jobject world = env->NewObject(cls638, ctor638);
        jobject player_obj = env->GetObjectField(client_ref, fid_1724);
        check(world != nullptr && player_obj != nullptr, "fixture world + player reachable");
        if (world != nullptr && player_obj != nullptr) {
            env->SetObjectField(client_ref, fid_1687, world);

            // A victim next to the player; KillAura attacks it once per window.
            jobject victim2 = env->NewObject(cls1309, ctor1309);
            check(victim2 != nullptr, "KillAura victim created");
            if (victim2 != nullptr) {
                env->CallVoidMethod(world, add_entity, victim2);
                f_put_dbl("KillAura", "Reach", 4.0);
                f_put_dbl("KillAura", "CPS", 1.0);
                // W-Tap is armed before KillAura so the attack event reaches a
                // listener; the 20 ms minimum tap window + a short sleep make
                // the drop/re-assert cycle deterministic.
                f_put_dbl("W-Tap", "Tap Duration", 20.0);
                check(f_set_dbl("W-Tap", "Tap Duration") == 20.0,
                      "Tap Duration parked at its 20 ms minimum");
                check(f_mod_set("W-Tap", 1) == 1, "W-Tap enabled");
                check(f_set_sprinting(1) == 1 && f_sprinting() == 1,
                      "sprint parked on before the W-Tap cycle");
                check(f_mod_set("KillAura", 1) == 1, "KillAura enabled");
                f_tick();
                check((get_attacks != nullptr)
                          ? env->CallStaticIntMethod(cls636, get_attacks) >= attacks_before + 2
                          : 1,
                      "KillAura attacked the nearest entity in reach");
                check(f_sprinting() == 0, "W-Tap dropped sprint on the automated attack");
                ::usleep(120 * 1000);   // outlast the tap window (20 ms parked)
                f_tick();
                check(f_sprinting() == 1, "W-Tap re-asserts sprint after the tap window");

                check(f_mod_set("KillAura", 0) == 1, "KillAura disabled again");
                f_mod_set("W-Tap", 0);
                env->DeleteLocalRef(victim2);
            }

            // ---- Auto Totem: offhand totem detection ---------------------------
            jobject inventory = env->GetObjectField(player_obj, fid_inventory);
            if (inventory != nullptr) {
                // Empty offhand -> not a totem.
                env->SetObjectField(inventory, fid_offhand, nullptr);
                check(f_offhand_totem_probe() == 0, "empty offhand reports no totem");

                // A totem in the offhand -> detected.
                jobject totem_item = env->GetStaticObjectField(cls1802, fid_totem);
                jobject totem_stack = (totem_item != nullptr && ctor1799 != nullptr)
                                          ? env->NewObject(cls1799, ctor1799, totem_item)
                                          : nullptr;
                check(totem_stack != nullptr, "totem stack fixture created");
                if (totem_stack != nullptr) {
                    env->SetObjectField(inventory, fid_offhand, totem_stack);
                    check(f_offhand_totem_probe() == 1,
                          "offhand totem detected via OFF_HAND_SLOT");
                    env->DeleteLocalRef(totem_stack);
                }
                env->DeleteLocalRef(totem_item);
                env->SetObjectField(inventory, fid_offhand, nullptr);
                env->DeleteLocalRef(inventory);
            }

            // Registered + one tick against the fixture: the offhand is empty
            // and the screen handler is absent, so the swap path fails softly.
            check(f_mod_set("Auto Totem", 1) == 1, "Auto Totem registered and toggleable");
            f_tick();
            check(f_mod_set("Auto Totem", 0) == 1, "Auto Totem disabled again");
            env->DeleteLocalRef(world);
        }
        env->DeleteLocalRef(player_obj);
    }

    // ---- 7e) game-thread task queue ----------------------------------------
    check(f_task_on_game() == 0, "this test thread is not the game thread");
    const long long executed_before = f_task_executed();
    check(f_task_probe() == 1, "probe job accepted from a foreign thread");
    check(f_task_pending() == 1, "job waits until the game thread drains");
    check(f_task_drain() == 1, "drain runs the queued job");
    check(f_task_executed() == executed_before + 1, "executed counter advanced");
    check(f_notify(nullptr, nullptr, 9) == 1, "out-of-range toast kind falls back to info");

    // ---- 8) teardown --------------------------------------------------------
    f_onunload(vm, nullptr);
    check(f_status() == 0, "JNI_OnUnload -> state == 0 (kDetached)");
    check(f_mod_count() == 0, "module registry cleared on unload");
    check(::dlclose(woke) == 0, "dlclose(libwoke.so) — destructor executed");

    const std::string log = read_all("logs/latest.log");
    check(contains(log, "no JVM present in this process — deferred to JNI_OnLoad"),
          "log: constructor deferred init");
    check(contains(log, "registered 11 built-in modules"), "log: built-in modules registered");
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
