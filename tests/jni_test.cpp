// ============================================================================
//  woke.wtf — tests/jni_test.cpp
//  End-to-end verification of the JVM/JNI layer:
//
//    1. compile intermediary fixture classes (javac) onto a test classpath
//    2. dlopen libwoke.so FIRST — constructor logs "deferred to JNI_OnLoad"
//       (no JVM exists yet) and creates the session log + latest.log symlink
//    3. create a real JVM (JNI_CreateJavaVM via dlopen of libjvm.so)
//    4. call JNI_OnLoad from libwoke.so -> full init: thread attach,
//       mappings.json parse, reflection cache population
//    5. query the exported cache API: non-null jclass/jmethodID/jfieldID,
//       cross-verified against direct JNI calls (pointer identity)
//    6. call JNI_OnUnload -> cache released, state reset, mappings retained
//    7. dlclose -> destructor detaches the session logger
//
//  Build:  g++ -std=c++20 -Wall -Wextra -Wpedantic tests/jni_test.cpp
//          -I src -I .cache/tools/jdk/include -I .cache/tools/jdk/include/linux
//          -o .cache/jni_test -ldl
//  Run:    from the repo root —  ./.cache/jni_test [path/to/libwoke.so]
// ============================================================================
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

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
    const char* lib = (argc > 1) ? argv[1] : "build/linux-gcc-release/libwoke.so";
    std::printf("=== woke.wtf JNI test ===\n");
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
        std::printf("       provision one, e.g. see tests/jni_test.cpp header\n");
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
        "tests/fixtures/src/net/minecraft/class_1657.java",
        "tests/fixtures/src/net/minecraft/class_1268.java",
        "tests/fixtures/src/net/minecraft/class_1299.java",
        "tests/fixtures/src/net/minecraft/class_239.java",
        "tests/fixtures/src/net/minecraft/class_3966.java",
        "tests/fixtures/src/net/minecraft/class_636.java",
    };
    std::string jc = "mkdir -p .cache/javac-out && " + javac + " -d .cache/javac-out";
    for (const char* f : fixtures) {
        jc += " ";
        jc += f;
    }
    jc += " >/dev/null 2>&1";
    check(::system(jc.c_str()) == 0, "javac compiled intermediary fixture classes");

    // ---- 3) dlopen libwoke.so BEFORE any JVM exists ------------------------
    void* woke = ::dlopen(lib, RTLD_NOW | RTLD_LOCAL);
    if (woke == nullptr) {
        std::printf("[FAIL] dlopen(libwoke.so): %s\n", ::dlerror());
        return 2;
    }
    check(true, "dlopen(libwoke.so) — constructor ran (session log + deferred JNI init)");

    using status_fn = int (*)();
    using onload_fn = jint (*)(JavaVM*, void*);
    using onunload_fn = void (*)(JavaVM*, void*);
    using long_fn = long (*)();
    using str_fn = const char* (*)(const char*);
    using ptr_fn = void* (*)(const char*, const char*, const char*);
    using ptr1_fn = void* (*)(const char*);

    auto sym_status = reinterpret_cast<status_fn>(::dlsym(woke, "woke_jni_status"));
    auto sym_load = reinterpret_cast<onload_fn>(::dlsym(woke, "JNI_OnLoad"));
    auto sym_unload = reinterpret_cast<onunload_fn>(::dlsym(woke, "JNI_OnUnload"));
    auto sym_mc_classes = reinterpret_cast<long_fn>(::dlsym(woke, "woke_mappings_class_count"));
    auto sym_mc_methods = reinterpret_cast<long_fn>(::dlsym(woke, "woke_mappings_method_count"));
    auto sym_mc_fields = reinterpret_cast<long_fn>(::dlsym(woke, "woke_mappings_field_count"));
    auto sym_c_classes = reinterpret_cast<long_fn>(::dlsym(woke, "woke_jni_cached_class_count"));
    auto sym_c_methods = reinterpret_cast<long_fn>(::dlsym(woke, "woke_jni_cached_method_count"));
    auto sym_c_fields = reinterpret_cast<long_fn>(::dlsym(woke, "woke_jni_cached_field_count"));
    auto sym_inter = reinterpret_cast<str_fn>(::dlsym(woke, "woke_reflection_intermediary_class"));
    auto sym_cls = reinterpret_cast<ptr1_fn>(::dlsym(woke, "woke_reflection_class"));
    auto sym_meth = reinterpret_cast<ptr_fn>(::dlsym(woke, "woke_reflection_method"));
    auto sym_fld = reinterpret_cast<ptr_fn>(::dlsym(woke, "woke_reflection_field"));

    check(sym_status && sym_load && sym_unload && sym_mc_classes && sym_mc_methods &&
              sym_mc_fields && sym_c_classes && sym_c_methods && sym_c_fields &&
              sym_inter && sym_cls && sym_meth && sym_fld,
          "all exported JNI/cache symbols resolve via dlsym");
    if (!sym_status || !sym_load || !sym_cls || !sym_meth || !sym_fld) {
        return 2;
    }
    check(sym_status() == 0, "state == 0 (deferred): no JVM existed at dlopen time");

    // ---- 4) create a real JVM with the fixture classpath -------------------
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
    const jint create_rs = create_vm(&vm, reinterpret_cast<void**>(&env), &vm_args);
    if (create_rs != JNI_OK || vm == nullptr || env == nullptr) {
        std::printf("[FAIL] JNI_CreateJavaVM failed (%d)\n", static_cast<int>(create_rs));
        return 2;
    }
    std::printf("[INFO] JVM created (version 0x%04x)\n",
                static_cast<unsigned>(env->GetVersion()));

    // ---- 5) JNI_OnLoad performs the full initialization --------------------
    const jint load_rs = sym_load(vm, nullptr);
    check(load_rs == JNI_VERSION_1_8, "JNI_OnLoad returned JNI_VERSION_1_8 (not JNI_ERR)");
    check(sym_status() == 2, "state == 2 (kReady): attached + mappings + cache");

    // idempotent second call must not fail or double-initialize
    check(sym_load(vm, nullptr) == JNI_VERSION_1_8, "JNI_OnLoad is idempotent on re-call");

    // ---- 6) mappings.json parsed -------------------------------------------
    check(sym_mc_classes() >= 9000, "mappings.json parsed: >= 9000 classes");
    check(sym_mc_methods() >= 50000, "mappings.json parsed: >= 50000 methods");
    check(sym_mc_fields() >= 40000, "mappings.json parsed: >= 40000 fields");

    const char* inter = sym_inter("net/minecraft/client/MinecraftClient");
    check(inter != nullptr && std::strcmp(inter, "net/minecraft/class_310") == 0,
          "yarn MinecraftClient -> intermediary net/minecraft/class_310");

    // ---- 7) reflection cache: valid (non-null) handles ---------------------
    check(sym_c_classes() >= 1, "reflection cache populated (>= 1 class resolved)");
    std::printf("       cached: %ld classes, %ld methods, %ld fields\n",
                sym_c_classes(), sym_c_methods(), sym_c_fields());

    void* jclass_void = sym_cls("net/minecraft/client/MinecraftClient");
    check(jclass_void != nullptr, "cached jclass (MinecraftClient) is non-null");

    void* method_void = sym_meth("net/minecraft/client/MinecraftClient", "setScreen", nullptr);
    check(method_void != nullptr, "cached jmethodID (setScreen) is non-null");

    void* field_void = sym_fld("net/minecraft/client/MinecraftClient", "player", nullptr);
    check(field_void != nullptr, "cached jfieldID (player) is non-null");

    const char* desc_m = "(Lnet/minecraft/class_437;)V";
    check(sym_meth("net/minecraft/client/MinecraftClient", "setScreen", desc_m) == method_void,
          "descriptor-qualified method lookup returns the same jmethodID");

    // Cross-verify the cached handles against direct JNI calls (pointer identity).
    auto jclass_cached = reinterpret_cast<jclass>(jclass_void);
    jclass direct = env->FindClass("net/minecraft/class_310");
    check(direct != nullptr && env->IsSameObject(direct, jclass_cached) == JNI_TRUE,
          "cached jclass is the same class as env->FindClass");
    if (direct != nullptr) {
        jmethodID direct_mid =
            env->GetMethodID(direct, "method_1507", "(Lnet/minecraft/class_437;)V");
        check(direct_mid != nullptr && direct_mid == reinterpret_cast<jmethodID>(method_void),
              "cached jmethodID == direct GetMethodID result");
        jfieldID direct_fid =
            env->GetFieldID(direct, "field_1724", "Lnet/minecraft/class_746;");
        check(direct_fid != nullptr && direct_fid == reinterpret_cast<jfieldID>(field_void),
              "cached jfieldID == direct GetFieldID result");
        env->DeleteLocalRef(direct);
    }

    check(sym_cls("com/example/DoesNotExist") == nullptr,
          "unknown yarn class resolves to null (negative control)");

    // ---- 8) JNI_OnUnload releases the cache --------------------------------
    sym_unload(vm, nullptr);
    check(sym_status() == 0, "JNI_OnUnload -> state == 0 (kDetached)");
    check(sym_c_classes() == 0 && sym_c_methods() == 0 && sym_c_fields() == 0,
          "reflection cache fully released (all counts 0)");
    check(sym_cls("net/minecraft/client/MinecraftClient") == nullptr,
          "lookups return null after JNI_OnUnload");
    check(sym_mc_classes() >= 9000, "mappings retained across unload (re-attach ready)");

    // ---- 9) dlclose: destructor detaches the session logger ----------------
    check(::dlclose(woke) == 0, "dlclose(libwoke.so) — destructor executed");

    const std::string log = read_all("logs/latest.log");
    check(contains(log, "no JVM present in this process — deferred to JNI_OnLoad"),
          "log: constructor deferred init (no JVM at dlopen)");
    check(contains(log, "mappings.json parsed:"), "log: mappings.json parsed line");
    check(contains(log, "reflection cache ready:"), "log: reflection cache summary line");
    check(contains(log, "JNI_OnLoad complete"), "log: JNI_OnLoad confirmation");
    check(contains(log, "JNI_OnUnload called"), "log: JNI_OnUnload confirmation");
    check(contains(log, "=== session log ended ==="), "log: destructor ended the session");

    // ---- 10) shut the fixture JVM down -------------------------------------
    using destroy_fn = jint (*)(JavaVM*);
    auto destroy_vm = reinterpret_cast<destroy_fn>(::dlsym(jvm_handle, "DestroyJavaVM"));
    if (destroy_vm != nullptr) {
        destroy_vm(vm);
        std::printf("[INFO] DestroyJavaVM returned\n");
    }
    // NOTE: libjvm.so is intentionally not dlclosed (unsupported by HotSpot);
    // the process exits immediately after this test.

    std::printf("=== %s (%d failure%s) ===\n",
                g_failures == 0 ? "ALL TESTS PASSED" : "TESTS FAILED",
                g_failures, g_failures == 1 ? "" : "s");
    return g_failures == 0 ? 0 : 1;
}
