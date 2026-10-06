# woke.wtf

Native C++ injection utility client for **Minecraft 1.21.11 (Fabric)** — x86_64 Linux.
Produces a single shared object: **`libwoke.so`**.

> Scope: private-server utility testing, QoL automation, and local singleplayer
> development only. Zero public multiplayer use. Modules never generate or send
> network packets; all behavior runs through standard client-side game state
> read/write APIs with explicit, visible UI toggles.

## What it does

| Area | Behavior |
|---|---|
| Lifecycle | Session log under `logs/` (+ `latest.log` symlink), JNI attach, `mappings.json` parse, reflection cache, built-in module registration, config load, hook engine — all idempotent and re-attachable |
| Present hook | MinHook detour on `glXSwapBuffers`, always chaining to the original; timestamps its own work to keep per-frame overhead measurable |
| Overlay | Dear ImGui 1.92 click-gui (module list by category, toggles, tooltips, live metrics footer). When the GUI is closed the detour performs **zero** ImGui work (draw-call suppression) |
| Input | Keybind edge-detection via `XQueryKeymap` on the game's X11 `Display`; pointer state via `XQueryPointer` |
| Modules | HUD (fps/client state), Fullbright (gamma read-modify-restore), Sprint (client-state toggle) |
| Config | `woke.wtf/config.v1` JSON — module states + keybind, written on every toggle |

Everything client-side is read/written through the JNI reflection cache; no
packet generation, no spoofing, no server interaction.

## Injection modes

Both paths converge on the same `jni_startup()` and are idempotent, so whichever
fires first wins.

| Mode | Trigger | Startup |
|---|---|---|
| `System.loadLibrary` | The JVM calls `JNI_OnLoad` | `JNI_OnLoad` runs the full startup directly |
| Pure injection (`dlopen` into a running JVM) | `JNI_GetCreatedJavaVMs` reports a live VM in the load constructor | The constructor only logs and arms a **deferred-init worker**, which waits a grace period for a `JNI_OnLoad` that will never come, then runs the same startup itself |

The constructor never touches the JVM: it runs under glibc's loader lock, and
classloading (`FindClass` in the reflection cache) could deadlock against a
concurrent `dlopen`. All JVM work happens after the grace period, on a thread
that is not holding the loader lock.

### Renderer modes

The ImGui renderer is attached lazily on the **first frame the overlay draws**,
and the choice is sticky for the process lifetime:

| Mode | When | Behavior |
|---|---|---|
| `gl` | A GLX context is current on that frame (the game's render thread inside `glXSwapBuffers`) | `imgui_impl_opengl3` attaches and draws straight into the game's back buffer; display size from `XGetGeometry`, pointer from `XQueryPointer` |
| `bare` | No GLX entry point reachable, or `WOKE_FORCE_CPU_OVERLAY` set | Legacy CPU-only font atlas, no GL calls at all (headless runs and tests) |

The two paths cannot be mixed: ImGui 1.92 asserts when a legacy-built atlas is
later handed to a texture-aware backend, so the first frame decides and the
other path is never entered.

If GLX is reachable but never becomes current (≈300 presents), the client warns
and settles for the CPU-only overlay instead of suppressing every frame.

## Configuration

| Env var | Default | Purpose |
|---|---|---|
| `WOKE_MAPPINGS_PATH` | `mappings.json` | Runtime mappings file location |
| `WOKE_CONFIG_PATH` | `woke.wtf/config.json` | Config file location |
| `WOKE_DEFER_GRACE_MS` | `1500` | How long the deferred-init worker waits for a `JNI_OnLoad` before taking over (0–60000) |
| `WOKE_FORCE_CPU_OVERLAY` | unset | Any non-empty value other than `0` disables the GL renderer (headless CI, overlay debugging) |

Default GUI keybind is keycode **62** (Right Shift); the real key comes from the
config file, so no rebuild is needed to change it.

## Toolchain requirements

| Component | Requirement |
|---|---|
| OS | Linux x86_64, glibc, kernel 6.x+ |
| Compiler | GCC 13+ **or** Clang 17+ (`-std=c++20`) |
| CMake | 3.27+ |
| Generator | Ninja (`ninja-build`) |
| Tools | `git`, `curl`, `python3`, `tar` |
| System packages (recommended) | `libglfw3-dev`, `libgl1-mesa-dev` (optional — vendored GLFW headers are used as fallback) |

No system packages are strictly required: dependencies are vendored under
`third_party/` by the bootstrap script. If a pinned toolchain is preferred,
place `cmake`/`ninja` executables in `.cache/tools/bin/` — every preset
prepends that directory to `PATH` automatically.

## Quick start

```bash
# 1) Vendor dependencies (MinHook Linux port, Dear ImGui, GLFW headers,
#    nlohmann/json) and generate mappings.json. Idempotent — re-run anytime.
scripts/bootstrap_deps.sh

# 2) Configure + build (Ninja presets)
cmake --preset linux-gcc-debug      # GCC, -Og -g3, AddressSanitizer
cmake --build --preset linux-gcc-debug

cmake --preset linux-gcc-release    # GCC, -O3 -march=x86-64-v2, LTO
cmake --build --preset linux-gcc-release

cmake --preset linux-clang-release  # Clang, -O3, LTO
cmake --build --preset linux-clang-release
```

Outputs: `build/<preset>/libwoke.so`.

## Vendored pins (`scripts/bootstrap_deps.sh`)

| Dependency | Source | Pin |
|---|---|---|
| MinHook (Linux/POSIX port) | `lindbergh-loader/linuxloader` → `src/minhook` | SHA `9aa6e3e45ccf` |
| Dear ImGui (docking + GLFW/OpenGL3 backends) | `ocornut/imgui` | tag `v1.92.9b-docking` |
| GLFW headers (fallback) | `glfw/glfw` | tag `3.4` |
| nlohmann/json | single header | `v3.11.3` |

Each dependency carries a `.woke-pin` stamp; re-runs skip completed steps and
clones are cached under `.cache/repos/`. Use `SKIP_MAPPINGS=1` to skip
mappings generation.

## `mappings.json` (`scripts/fetch_mappings.sh`)

Downloads Yarn + Intermediary (tiny v2) for Minecraft **1.21.11** from Fabric
Maven, merges them, and emits a single runtime reference file with all
class/field/method names and descriptors normalized to the **intermediary**
namespace — exactly what JNI `FindClass` / `GetMethodID` need inside the
Fabric-remapped game:

```jsonc
"net/minecraft/client/MinecraftClient": {
  "intermediary": "net/minecraft/class_310",
  "descriptor":   "Lnet/minecraft/class_310;",
  "methods": { "setScreen": [ { "intermediary": "method_1507",
                                "descriptor": "(Lnet/minecraft/class_437;)V" } ] }
}
```

```bash
scripts/fetch_mappings.sh                 # default: 1.21.11 (latest stable Yarn build)
scripts/fetch_mappings.sh 1.21.11         # explicit version
FORCE=1 scripts/fetch_mappings.sh         # force re-download of cached jars
```

## Project layout

```
src/libwoke.cpp              lifecycle, JNI entry points, deferred-init worker, woke_* exports
src/core/                    logger, filesystem helpers, platform utilities, config
src/jni/                     mappings.json parser + jclass/jmethodID/jfieldID cache
src/game/                    client-state layer (client/player/options, fps, gamma, sprinting)
src/modules/                 module base + registry, built-ins (HUD, Fullbright, Sprint)
src/gui/                     click-gui window, keybind polling, headless frame cycle
src/hook/hook_engine.*       MinHook wrapper + engine shutdown
src/hook/present_hook.*      glXSwapBuffers detour, frame timing, suppression metrics
src/hook/imgui_backend.*     renderer mode decision, GL attach, X11 display/pointer input
```

## Tests

Tests are standalone binaries (no test framework) that `dlopen` the built
`libwoke.so` and assert behavior plus log output. Build them against the
release preset:

```bash
JDK="-I .cache/tools/jdk/include -I .cache/tools/jdk/include/linux"
g++ -std=c++20 -Wall -Wextra -Wpedantic -Isrc tests/logger_test.cpp -o .cache/logger_test
g++ -std=c++20 -Wall -Wextra -Wpedantic tests/lifecycle_test.cpp -o .cache/lifecycle_test -ldl
g++ -std=c++20 -Wall -Wextra -Wpedantic -I src $JDK tests/jni_test.cpp -o .cache/jni_test -ldl
g++ -std=c++20 -Wall -Wextra -Wpedantic -I src $JDK tests/module_test.cpp -o .cache/module_test -ldl
g++ -std=c++20 -Wall -Wextra -Wpedantic -I src $JDK -rdynamic tests/hook_test.cpp -o .cache/hook_test -ldl
g++ -std=c++20 -Wall -Wextra -Wpedantic -I src $JDK -pthread tests/defer_test.cpp -o .cache/defer_test -ldl
```

Run them **from the repo root**, in this order (later tests read
`logs/latest.log`, which each run rewrites):

```bash
./.cache/lifecycle_test   # constructor/destructor, session log + symlink
./.cache/logger_test      # logger levels, file redirection, tid/time formatting
./.cache/jni_test         # JNI_OnLoad path: attach, mappings, reflection cache
./.cache/module_test      # game-state, modules, config persistence, headless gui frames
./.cache/defer_test       # pure injection: deferred-init worker, no JNI_OnLoad
./.cache/hook_test        # present detour, suppression, steady-state frame budget
```

`hook_test` and `defer_test` need a JDK (`WOKE_JAVA_HOME`, `JAVA_HOME`, or
`.cache/tools/jdk`). `hook_test` exports its own `glXSwapBuffers` (hence
`-rdynamic`) so MinHook patches a real function, and forces the CPU-only overlay
so the result does not depend on the host's GL stack.
