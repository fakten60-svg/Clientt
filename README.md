# woke.wtf

Native C++ injection utility client for **Minecraft 1.21.11 (Fabric)** — x86_64 Linux.
Produces a single shared object: **`libwoke.so`**.

> Scope: private-server utility testing, QoL automation, and local singleplayer
> development only. Zero public multiplayer use. Modules never generate or send
> network packets; all behavior runs through standard client-side game state
> read/write APIs with explicit, visible UI toggles.

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
