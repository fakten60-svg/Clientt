#!/usr/bin/env bash
# ============================================================================
# woke.wtf — mappings.json generator
#
# Downloads Yarn + Intermediary (tiny v2) for a Minecraft version from Fabric
# Maven, merges them, and emits a single runtime-oriented ./mappings.json:
#
#   "net/minecraft/client/MinecraftClient": {
#     "intermediary": "net/minecraft/class_310",
#     "descriptor":   "Lnet/minecraft/class_310;",
#     "fields":  { "currentScreen": [ { "intermediary": "field_...", "descriptor": "L...;" } ] },
#     "methods": { "setScreen":     [ { "intermediary": "method_...", "descriptor": "(L...;)V" } ] }
#   }
#
# All names/descriptors are normalized to the INTERMEDIARY namespace — the
# namespace the Fabric-remapped game runs under, i.e. exactly what JNI
# FindClass/GetStaticMethodID need. Member keys are Yarn names (readable),
# values are runtime-safe.
#
# Idempotent: jars are cached under .cache/mappings/ keyed by version.
# Usage: scripts/fetch_mappings.sh [minecraft-version]   (default: 1.21.11)
#        FORCE=1 scripts/fetch_mappings.sh               (force re-download)
# ============================================================================
set -euo pipefail
IFS=$'\n\t'

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"

MC_VERSION="${1:-${MC_VERSION:-1.21.11}}"
CACHE="${ROOT}/.cache/mappings"
OUT="${ROOT}/mappings.json"
MAVEN="https://maven.fabricmc.net/net/fabricmc"
FORCE="${FORCE:-0}"

log()  { printf '[mappings] %s\n' "$*"; }
fail() { printf '[mappings] ERROR: %s\n' "$*" >&2; exit 1; }
need() { command -v "$1" >/dev/null 2>&1 || fail "required command not found: $1"; }

need curl
need python3
mkdir -p "$CACHE"

# Atomic download; skipped when the destination already exists.
dl() { # url dest
  local url="$1" dest="$2" tmp="${2}.part.$$"
  if [ -f "$dest" ] && [ "$FORCE" != "1" ]; then
    return 0
  fi
  log "downloading $(basename "$dest")"
  curl -fsSL --retry 3 --retry-delay 2 --connect-timeout 20 --max-time 300 \
    -o "$tmp" "$url" || { rm -f "$tmp"; fail "download failed: $url"; }
  mv -f "$tmp" "$dest"
}

# ----------------------------------------------------------------------------
# 1) Resolve the newest Yarn build for the target Minecraft version
# ----------------------------------------------------------------------------
log "resolving latest Yarn build for Minecraft ${MC_VERSION}"
META="$(curl -fsSL --retry 3 --connect-timeout 20 \
  "${MAVEN}/yarn/maven-metadata.xml")" \
  || fail "cannot fetch yarn maven-metadata.xml from Fabric Maven"

ESCAPED_MC="${MC_VERSION//./\\.}"
# Stable releases only: exact "<mc>+build.N" — never "<mc>-pre1…" / "<mc>-rc3…".
YARN_VERSION="$(
  { printf '%s\n' "$META" | grep -o "<version>${ESCAPED_MC}+build\.[0-9]*</version>" || true; } \
    | sed -e 's#</\?version>##g' | sort -V | tail -n 1
)"
[ -n "$YARN_VERSION" ] || fail "no Yarn builds published for Minecraft ${MC_VERSION}"
log "yarn build: ${YARN_VERSION}"

# ----------------------------------------------------------------------------
# 2) Download both mapping jars (cached per version) and extract mappings.tiny
# ----------------------------------------------------------------------------
YARN_JAR="${CACHE}/yarn-${YARN_VERSION}-v2.jar"
INTER_JAR="${CACHE}/intermediary-${MC_VERSION}-v2.jar"
dl "${MAVEN}/yarn/${YARN_VERSION}/yarn-${YARN_VERSION}-v2.jar" "$YARN_JAR"
dl "${MAVEN}/intermediary/${MC_VERSION}/intermediary-${MC_VERSION}-v2.jar" "$INTER_JAR"

extract_tiny() { # jar dest
  python3 - "$1" "$2" <<'PYEOF'
import sys, zipfile
jar, dest = sys.argv[1], sys.argv[2]
try:
    with zipfile.ZipFile(jar) as z:
        data = z.read("mappings/mappings.tiny")
except (zipfile.BadZipFile, KeyError, OSError) as e:
    sys.exit(f"[mappings] cannot read mappings/mappings.tiny from {jar}: {e}")
with open(dest, "wb") as fh:
    fh.write(data)
PYEOF
}
extract_tiny "$YARN_JAR" "${CACHE}/yarn.tiny"
extract_tiny "$INTER_JAR" "${CACHE}/intermediary.tiny"

# ----------------------------------------------------------------------------
# 3) Merge tiny v2 -> mappings.json (names + descriptors in intermediary NS)
# ----------------------------------------------------------------------------
log "merging tiny v2 records"
python3 - "${CACHE}/yarn.tiny" "${CACHE}/intermediary.tiny" \
          "$MC_VERSION" "$YARN_VERSION" "$OUT" <<'PYEOF'
import json, re, sys, time

yarn_path, inter_path, mc_ver, yarn_ver, out_path = sys.argv[1:6]

def parse_tiny(path):
    """tiny v2 -> (namespaces, class records). Only c/f/m lines matter for JNI;
    p/v local-variable records are irrelevant for reflection lookups."""
    namespaces, classes = [], []
    with open(path, "r", encoding="utf-8-sig") as fh:
        for raw in fh:
            line = raw.rstrip("\n")
            if not line:
                continue
            if line.startswith("tiny\t"):
                parts = line.split("\t")
                if len(parts) < 4 or parts[1] != "2":
                    sys.exit(f"[mappings] {path}: unsupported tiny format: {line[:60]!r}")
                namespaces = parts[3:]
                continue
            # tiny v2 indentation grammar (verified against yarn 1.21.11+build.6):
            #   indent 0 "c"  -> class record      indent 1 "f"/"m" -> members
            #   indent 1+ "c" -> javadoc comment    indent 1+ "p"/"v" -> locals
            # Comments share the "c" tag, so indentation is the only safe discriminator.
            indent = len(line) - len(line.lstrip("\t"))
            tok = line.lstrip("\t").split("\t")
            if tok[0] == "c" and indent == 0:
                classes.append([tok[1:], []])
            elif tok[0] in ("f", "m") and indent == 1 and classes and len(tok) >= 3:
                classes[-1][1].append((tok[0], tok[1], tok[2:]))
    if not namespaces:
        sys.exit(f"[mappings] {path}: no tiny v2 header found")
    return namespaces, classes

def ns_index(ns, name, path):
    try:
        return ns.index(name)
    except ValueError:
        sys.exit(f"[mappings] {path}: namespace {name!r} missing (have {ns})")

y_ns, y_classes = parse_tiny(yarn_path)
i_ns, i_classes = parse_tiny(inter_path)
yi = ns_index(y_ns, "intermediary", yarn_path)
yn = ns_index(y_ns, "named", yarn_path)
ii = ns_index(i_ns, "intermediary", inter_path)

# --- class-name maps: any namespace -> intermediary (for descriptor rewrite) --
maps = [dict() for _ in y_ns]
for names, _ in y_classes:
    if len(names) != len(y_ns):
        continue
    inter = names[yi]
    if not inter:
        continue
    for idx, nm in enumerate(names):
        if nm:
            maps[idx][nm] = inter
maps[yi] = {}  # intermediary is the identity target

desc_re = re.compile(r"L([^;]+);")
def to_intermediary(desc):
    """Rewrite every L...; token of a descriptor to its intermediary name.
    JDK/other-library types are absent from the maps and stay verbatim
    (identical in every namespace). Works regardless of whether the source
    file writes descriptors in official, intermediary or named namespace."""
    def repl(m):
        cn = m.group(1)
        for mp in maps:
            if cn in mp:
                return "L" + mp[cn] + ";"
        return m.group(0)
    return desc_re.sub(repl, desc)

# --- cross-validate yarn against the official intermediary file --------------
i_set = {names[ii] for names, _ in i_classes
         if len(names) == len(i_ns) and names[ii]}
y_inter = {names[yi] for names, _ in y_classes
           if len(names) == len(y_ns) and names[yi]}
if not y_inter:
    sys.exit("[mappings] parsed zero classes from yarn mappings")
overlap = len(y_inter & i_set)
if overlap < len(y_inter) * 0.99:
    sys.exit(f"[mappings] yarn/intermediary mismatch: only {overlap}/{len(y_inter)} classes matched")

# --- emit -------------------------------------------------------------------
classes_out = {}
counts = {"classes": 0, "fields": 0, "methods": 0, "skipped_records": 0}
for names, members in y_classes:
    if len(names) != len(y_ns) or not names[yi] or not names[yn]:
        counts["skipped_records"] += 1
        continue
    inter_cls, named_cls = names[yi], names[yn]
    rec = {"intermediary": inter_cls,
           "descriptor": "L" + inter_cls + ";",
           "fields": {}, "methods": {}}
    for kind, desc, mnames in members:
        if len(mnames) != len(y_ns) or not mnames[yi] or not mnames[yn]:
            counts["skipped_records"] += 1
            continue
        bucket = "fields" if kind == "f" else "methods"
        rec[bucket].setdefault(mnames[yn], []).append(
            {"intermediary": mnames[yi], "descriptor": to_intermediary(desc)})
        counts[bucket] += 1
    classes_out[named_cls] = rec
    counts["classes"] += 1

doc = {
    "schema": "woke.wtf/mappings.v1",
    "minecraft": mc_ver,
    "yarn": yarn_ver,
    "generated": time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime()),
    "namespace": "intermediary",
    "counts": counts,
    "classes": classes_out,
}
with open(out_path, "w", encoding="utf-8") as fh:
    json.dump(doc, fh, separators=(",", ":"), ensure_ascii=True)
print(f"[mappings] merged: classes={counts['classes']} methods={counts['methods']} "
      f"fields={counts['fields']} skipped={counts['skipped_records']} "
      f"intermediary-match={overlap}/{len(y_inter)}")
PYEOF

# ----------------------------------------------------------------------------
# 4) Validate the generated file + spot-check known 1.21.x anchors
# ----------------------------------------------------------------------------
python3 - "$OUT" <<'PYEOF'
import json, os, sys
path = sys.argv[1]
try:
    with open(path, "r", encoding="utf-8") as fh:
        doc = json.load(fh)
except (json.JSONDecodeError, OSError) as e:
    sys.exit(f"[mappings] generated file is not valid JSON: {e}")

missing = {"schema", "minecraft", "yarn", "namespace", "counts", "classes"} - doc.keys()
if missing:
    sys.exit(f"[mappings] generated file missing keys: {sorted(missing)}")
mc_cls = doc["classes"].get("net/minecraft/client/MinecraftClient")
if not mc_cls or mc_cls.get("intermediary") != "net/minecraft/class_310":
    sys.exit("[mappings] spot-check failed: MinecraftClient != net/minecraft/class_310")
if "setScreen" not in mc_cls.get("methods", {}):
    sys.exit("[mappings] spot-check failed: MinecraftClient#setScreen missing")
c = doc["counts"]
print(f"[mappings] OK: {path}")
print(f"[mappings]   classes={c['classes']} methods={c['methods']} fields={c['fields']}")
print(f"[mappings]   minecraft={doc['minecraft']} yarn={doc['yarn']} "
      f"size={os.path.getsize(path) / 1e6:.1f} MB")
PYEOF

log "done -> mappings.json"
