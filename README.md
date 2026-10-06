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
| Overlay | Dear ImGui 1.92 **macOS dashboard** click-gui (traffic-light title bar, category sidebar, searchable module cards with Apple-style pill toggles, grid/list view, toasts, live metrics footer). When the GUI is closed and no animation/toast is settling the detour performs **zero** ImGui work (draw-call suppression) |
| Input | Keybind edge-detection via `XQueryKeymap` on the game's X11 `Display`; pointer state via `XQueryPointer` |
| Modules | 18 built-ins across 6 spec categories — Target HUD, Attack Cooldown, Auto Clicker, KillAura, W-Tap, Auto Totem, Triggerbot, AimAssist, Auto Hit Crystal, Anchor Macro, SafeAnchor (Combat); Auto Mace (Mace); Spear Lunge (Spear); HUD, Fullbright, Zoom (Visual); Sprint, Sneak (Movement). Each has typed `BaseSetting`s; combat automations only reuse the vanilla attack/use/swap paths |
| Config | `woke.wtf/config.v1` JSON — module states, per-module `settings`, per-module `keybinds`, and the GUI keybind; written on every toggle/edit |
| Subsystems | Decoupled type-safe `core::event_bus`, main-thread `core::task_queue`, `ui::notification_queue` toasts, `ui::animation_controller` (spring/easing), `ui::theme` palette, `utils::render`/`utils::math` stateless helpers |

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

## Modules & categories

Six categories are always present in the sidebar (`Combat`, `Mace`, `Misc`,
`Movement`, `Spear`, `Visual`); a category with no modules simply shows a
`0 modules` badge. Built-ins:

| Module | Category | Behavior |
|---|---|---|
| Target HUD | Combat | Render-only panel for the entity under the crosshair (type + health bar) from the client's own raycast |
| Attack Cooldown | Combat | Render-only vanilla attack-charge indicator near the crosshair |
| Auto Clicker | Combat | Repeats the vanilla attack (`interactionManager.attackEntity` + `swingHand`) on the crosshair target at a set CPS; optional "Require Full Charge" respects the vanilla cooldown |
| KillAura | Combat | Per-tick scan of the client world for the nearest living entity in reach, attacked through the same vanilla call pair (no aim assist — aiming stays with the player); Reach + CPS settings |
| W-Tap | Combat | Listens for automated attacks on the event bus and taps sprint off/on around them for the vanilla sprint-knockback bonus (client movement state only) |
| Auto Totem | Combat | Watches the offhand and swaps a totem of undying in from the main inventory through the vanilla `clickSlot(SWAP)` inventory click |
| Triggerbot | Combat | Attacks the moment the crosshair rests on a valid target and the vanilla charge passes the configured threshold |
| AimAssist | Combat | Steers the LOCAL view rotation toward the nearest target in reach — FOV-cone gated, bounded degrees-per-tick step, never a snap; plain client rotation state |
| Auto Hit Crystal | Combat | Scans the world for the nearest End Crystal in reach and attacks it through the vanilla attack call pair |
| Anchor Macro | Combat | Repeats the vanilla `interactBlock` use-click on the crosshair block while a respawn anchor is held (place/charge/detonate is the game's own decision) |
| SafeAnchor | Combat | Watchdog: disables Anchor Macro once the player's health drops below the configured threshold |
| Auto Mace | Mace | Attacks the crosshair target while falling at least the configured distance — timed for the mace smite window; optionally requires the mace |
| Spear Lunge | Spear | Attacks the crosshair target and boosts the player's own velocity along the look vector (`Entity.setVelocity`, the vanilla movement-state write) |
| HUD | Visual | Draws a watermark (optionally with live FPS) in a configurable corner (mode dropdown) |
| Fullbright | Visual | Read-modify-restore of the `gamma` video setting |
| Zoom | Visual | Read-modify-restore of the `fov` video setting (`Integer`-boxed `SimpleOption`) |
| Sprint | Movement | Per-tick `Entity#setSprinting(true)` assert while enabled |
| Sneak | Movement | Per-tick `Entity#setSneaking(true)` assert while enabled |

Every module derives from `BaseModule` with `on_enable` / `on_disable` / `on_tick`
/ `on_render` hooks and declares typed `BaseSetting<T>` values (boolean, integer,
decimal, color, text) plus `mode_setting` dropdowns — an integer-backed setting
with named choices (e.g. the HUD watermark corner) that serializes as its index,
edits through a combo box in the dashboard and exposes its choices via the
`woke_module_setting_choice_count/_label` API. Settings serialize automatically
into the config file; a toggle emits `core::event_bus::emit(module_toggled{...})`
so the dashboard toast and any other listener react without a direct dependency.

## The macOS dashboard

The ClickGUI replicates a modern macOS window: a charcoal glass frame with
`window_rounding = 14`, a centered `woke.wtf — Utility Client` title bar and the
three traffic-light controls (close `#FF5F56`, minimize `#FFBD2E`, zoom
`#27C93F`). Inside: a category sidebar with active-module counters, a header with
the page title, `N modules · M enabled` sub-badge, a search field and list/grid
toggle, and rounded module cards (`frame_rounding = 8`) with description, keybind
badge, an expandable chevron and an animated Apple-style pill toggle.

Motion is centralized in `ui::animation`: the window uses a soft spring + opacity
fade for open/close, pill nubs cross-fade and slide, hover states ease brightness,
and toasts slide in from the top-right with a fading lifetime bar. All motion is
time-delta based, so it looks identical at 60 Hz or 240 Hz. `woke_gui_wants_frames`
reports whether anything is unsettled; only then does the present detour run
ImGui work.

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
src/libwoke.cpp              lifecycle core: constructor/destructor, deferred-init worker, startup/shutdown
src/export/                  woke_* C API by topic: lifecycle.cpp, modules.cpp, game_gui.cpp
src/core/logger.hpp         multi-session colorized logger (console + file + latest.log)
src/core/config.*            JSON config engine (module states, settings, keybinds)
src/core/setting.hpp         BaseSetting<T> templated setting primitives + mode_setting dropdowns + setting_group
src/core/event_bus.hpp       decoupled per-type event channels (module_toggled, frame_tick, ...)
src/core/task_queue.*        bounded ring of callables drained on the game thread
src/jni/                     mappings.json parser + jclass/jmethodID/jfieldID cache
src/game/game_state.*        client-state layer (client/player/options, fps, gamma, fov, sprint/sneak)
src/game/game_combat.cpp     combat accessors: crosshair target, attack cooldown, nearest-target scan, vanilla attack path, offhand totem
src/modules/module.*         BaseModule lifecycle + registry + categories
src/modules/builtin.*        built-ins: Target HUD, Attack Cooldown, Auto Clicker, KillAura, W-Tap, Auto Totem, Triggerbot, AimAssist, Auto Hit Crystal, Anchor Macro, SafeAnchor, Auto Mace, Spear Lunge, HUD, Fullbright, Zoom, Sprint, Sneak
src/ui/theme.*               macOS palette + geometry + ImGui style
src/ui/animation.*           AnimationController (spring_value, animated_value, easing curves)
src/ui/component.*           reusable ImGui widgets (traffic light, toggle, search field, ...)
src/ui/notifications.*       toast queue (slide-in, lifetime bar)
src/utils/render.*           RenderUtils: rounded rects, borders, shadows, gradients, text clip
src/utils/math.hpp           MathUtils: clamp/lerp/exp_approach/spring/easing/color
src/gui/gui.*                the macOS dashboard orchestrator (window chrome, pages, public API)
src/gui/internal.*           dashboard shared state + one-time widget configuration
src/gui/sidebar.cpp          navigation sidebar + GENERAL pages (Settings/Theme/Configs/...)
src/gui/card.cpp             module card widget + list/grid layout + settings panel
src/gui/setting_row.cpp      generic BaseSetting<T> editor row (checkbox/slider/combo/color/text)
src/gui/keybind.cpp          X11 keybind polling + zero-alloc search matcher
src/hook/hook_engine.*       MinHook wrapper + engine shutdown
src/hook/present_hook.*      glXSwapBuffers detour, task drain, frame_tick, suppression metrics
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
so the result does not depend on the host's GL stack. `module_test` additionally
exercises the new API surface: category counts, FOV/sneak round-trips, `BaseSetting`
persistence and reset, per-module keybinds, dashboard pages/search/grid/expand,
toasts, the animation controller, event-bus listener counts and the task queue.
It also drives the combat set against fixture entities: crosshair-target health
reads, the one-attack-per-rate-window Auto Clicker behavior, the KillAura
nearest-entity attack, the W-Tap sprint tap/re-assert cycle, the Auto Totem
offhand-totem probe, the Triggerbot charge-gated crosshair attack, the
AimAssist cone gate + bounded rotation step, the Auto Hit Crystal
crystal-only scan, the Anchor Macro vanilla use-click (with and without the
anchor held), the SafeAnchor low-health watchdog, the Auto Mace fall gate and
the Spear Lunge velocity boost + cooldown, plus the self-attack guard and the
ignore path for non-entity crosshair targets.

The GL renderer branch (`gl` mode) is only exercised against a real GLX context
and is not covered by the headless suites.
