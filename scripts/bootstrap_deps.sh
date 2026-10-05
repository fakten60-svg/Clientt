#!/usr/bin/env bash
# ============================================================================
# woke.wtf — dependency bootstrap (pinned + idempotent)
#
# Vendors into third_party/:
#   minhook   Linux/POSIX-ported MinHook (lindbergh-loader/linuxloader, pinned SHA)
#   imgui     Dear ImGui docking tag + GLFW/OpenGL3 backends (pinned tag)
#   glfw      GLFW headers only — fallback when no system libglfw (pinned tag)
#   nlohmann  nlohmann/json single header (pinned release)
# Then runs scripts/fetch_mappings.sh -> ./mappings.json
#
# Safe to re-run: completed steps are skipped via .woke-pin stamp files, and
# network clones are cached under .cache/repos/.
#
# Usage:  scripts/bootstrap_deps.sh
#         SKIP_MAPPINGS=1 scripts/bootstrap_deps.sh    (skip mappings.json)
# ============================================================================
set -euo pipefail
IFS=$'\n\t'
export GIT_TERMINAL_PROMPT=0 GIT_TERMINAL_PAGING=0

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
TP="${ROOT}/third_party"
CACHE="${ROOT}/.cache/repos"

# ------------------------------- pinned refs --------------------------------
# Linux/POSIX MinHook port (upstream TsudaKageyu/minhook is Windows-only):
# mmap buffers, POSIX thread model, original BSD-2-Clause headers retained.
MINHOOK_REPO="https://github.com/lindbergh-loader/linuxloader.git"
MINHOOK_SHA="9aa6e3e45ccfbbc60eb0f975aa9d3d158a13706c"
MINHOOK_SUBDIR="src/minhook"

IMGUI_REPO="https://github.com/ocornut/imgui.git"
IMGUI_TAG="v1.92.9b-docking"
IMGUI_SUBDIR="backends"

GLFW_REPO="https://github.com/glfw/glfw.git"
GLFW_TAG="3.4"
GLFW_SUBDIR="include"

JSON_VERSION="v3.11.3"
JSON_URL="https://raw.githubusercontent.com/nlohmann/json/${JSON_VERSION}/single_include/nlohmann/json.hpp"

log()  { printf '[bootstrap] %s\n' "$*"; }
fail() { printf '[bootstrap] ERROR: %s\n' "$*" >&2; exit 1; }
need() { command -v "$1" >/dev/null 2>&1 || fail "required command not found: $1"; }

need git
need curl
need python3
need tar

# Atomic download; skipped when the destination already exists.
dl() { # url dest
  local url="$1" dest="$2" tmp="${2}.part.$$"
  if [ -f "$dest" ]; then
    return 0
  fi
  log "downloading $(basename "$dest")"
  curl -fsSL --retry 3 --retry-delay 2 --connect-timeout 20 --max-time 600 \
    -o "$tmp" "$url" || { rm -f "$tmp"; fail "download failed: $url"; }
  mv -f "$tmp" "$dest"
}

# Clone a pinned ref (SHA or tag): partial blobless clone + cone sparse
# checkout so only the needed subtree is materialized. Fully skips the network
# when the cache stamp already matches the pin.
clone_pinned() { # cache_name repo ref subdir
  local name="$1" repo="$2" ref="$3" subdir="$4"
  local dest="${CACHE}/${name}" stamp="${CACHE}/${name}.stamp"
  if [ -f "$stamp" ] && [ "$(cat "$stamp")" = "$ref" ] && [ -d "${dest}/.git" ]; then
    log "${name}: pinned ${ref} already cloned (skip)"
    return 0
  fi
  rm -rf "$dest" # safe: only our own .cache/repos/ directory
  mkdir -p "$CACHE"
  log "${name}: cloning ${repo} @ ${ref}"
  git init -q "$dest"
  git -C "$dest" remote add origin "$repo"
  git -C "$dest" sparse-checkout init --cone
  git -C "$dest" sparse-checkout set "$subdir"
  git -C "$dest" fetch --quiet --depth 1 --filter=blob:none origin "$ref" \
    || fail "fetch failed: ${repo} @ ${ref}"
  git -C "$dest" checkout -q --detach FETCH_HEAD \
    || fail "checkout failed: ${repo} @ ${ref}"
  printf '%s' "$ref" > "$stamp"
}

# Copy a directory tree into a vendor dir, never carrying .git metadata.
copy_tree() { # src dst
  mkdir -p "$2"
  ( cd "$1" && tar --exclude='./.git' -cf - . ) | ( cd "$2" && tar -xf - )
}

already_vendored() { # dir marker_file ref
  [ -f "$1/$2" ] && [ -f "$1/.woke-pin" ] && [ "$(cat "$1/.woke-pin")" = "$3" ]
}

# ------------------------- 1) MinHook (Linux/POSIX port) --------------------
vend_minhook() {
  local dest="${TP}/minhook"
  if already_vendored "$dest" "include/MinHook.h" "$MINHOOK_SHA"; then
    log "minhook: already vendored @ ${MINHOOK_SHA:0:12} (skip)"
    return 0
  fi
  clone_pinned minhook "$MINHOOK_REPO" "$MINHOOK_SHA" "$MINHOOK_SUBDIR"
  rm -rf "$dest"
  copy_tree "${CACHE}/minhook/${MINHOOK_SUBDIR}" "$dest"
  [ -f "${dest}/include/MinHook.h" ] && [ -f "${dest}/src/hook.c" ] \
    && [ -f "${dest}/src/hde/hde64.c" ] \
    || fail "minhook vendoring incomplete (expected include/ + src/ + hde/)"
  printf '%s' "$MINHOOK_SHA" > "${dest}/.woke-pin"
  log "minhook: vendored Linux/POSIX port @ ${MINHOOK_SHA:0:12}"
}

# ------------------------- 2) Dear ImGui (docking) --------------------------
vend_imgui() {
  local dest="${TP}/imgui"
  if already_vendored "$dest" "imgui.cpp" "$IMGUI_TAG"; then
    log "imgui: already vendored @ ${IMGUI_TAG} (skip)"
    return 0
  fi
  clone_pinned imgui "$IMGUI_REPO" "$IMGUI_TAG" "$IMGUI_SUBDIR"
  rm -rf "$dest"
  copy_tree "${CACHE}/imgui" "$dest"
  [ -f "${dest}/backends/imgui_impl_glfw.cpp" ] \
    && [ -f "${dest}/backends/imgui_impl_opengl3_loader.h" ] \
    && [ -f "${dest}/imgui.h" ] \
    || fail "imgui vendoring incomplete (expected core + backends/)"
  printf '%s' "$IMGUI_TAG" > "${dest}/.woke-pin"
  log "imgui: vendored @ ${IMGUI_TAG}"
}

# ------------------------- 3) GLFW headers (fallback) -----------------------
vend_glfw() {
  local dest="${TP}/glfw"
  if already_vendored "$dest" "include/GLFW/glfw3.h" "$GLFW_TAG"; then
    log "glfw: already vendored @ ${GLFW_TAG} (skip)"
    return 0
  fi
  clone_pinned glfw "$GLFW_REPO" "$GLFW_TAG" "$GLFW_SUBDIR"
  rm -rf "$dest"
  copy_tree "${CACHE}/glfw/include" "${dest}/include"
  cp "${CACHE}/glfw/LICENSE.md" "${dest}/LICENSE.md"
  [ -f "${dest}/include/GLFW/glfw3.h" ] || fail "glfw vendoring incomplete"
  printf '%s' "$GLFW_TAG" > "${dest}/.woke-pin"
  log "glfw: vendored headers @ ${GLFW_TAG} (used only when no system GLFW)"
}

# ------------------------- 4) nlohmann/json ---------------------------------
vend_json() {
  local dest="${TP}/nlohmann"
  local hdr="${dest}/json.hpp"
  if already_vendored "$dest" "json.hpp" "$JSON_VERSION"; then
    log "nlohmann: already vendored @ ${JSON_VERSION} (skip)"
    return 0
  fi
  mkdir -p "$dest"
  rm -f "$hdr"
  dl "$JSON_URL" "$hdr"
  grep -q "NLOHMANN_JSON_VERSION_MAJOR" "$hdr" \
    || fail "nlohmann/json.hpp looks corrupt (no version macro)"
  printf '%s' "$JSON_VERSION" > "${dest}/.woke-pin"
  log "nlohmann: vendored @ ${JSON_VERSION}"
}

# ------------------------------- run steps ----------------------------------
log "=== woke.wtf dependency bootstrap ==="
log "root: ${ROOT}"
vend_minhook
vend_imgui
vend_glfw
vend_json

if [ "${SKIP_MAPPINGS:-0}" != "1" ]; then
  log "generating mappings.json (scripts/fetch_mappings.sh)"
  bash "${SCRIPT_DIR}/fetch_mappings.sh"
else
  log "SKIP_MAPPINGS=1 — skipping mappings.json generation"
fi

log "=== summary ==="
for d in minhook imgui glfw nlohmann; do
  if [ -d "${TP}/${d}" ]; then
    printf '[bootstrap]   third_party/%-10s %s\n' "$d" \
      "$(du -sh "${TP}/${d}" 2>/dev/null | cut -f1)"
  fi
done
if [ -f "${ROOT}/mappings.json" ]; then
  printf '[bootstrap]   %-22s %s\n' "mappings.json" \
    "$(du -h "${ROOT}/mappings.json" | cut -f1)"
fi
log "bootstrap complete"
