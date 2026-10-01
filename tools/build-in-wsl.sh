#!/usr/bin/env bash
#
# Build DVDREGION on a Linux host without Docker.
#
# Pulls the /usr/local/ps3dev tree out of the hldtux/ps3dev Docker image by
# talking to the registry directly, drops it into /usr/local/ps3dev, then runs
# the normal PSL1GHT makefile. Only one path is extracted from each layer, so
# nothing else from the image rootfs leaks into the host.
#
# Usage (inside WSL or any Linux box):
#     sudo ./tools/build-in-wsl.sh [project-dir]
#
# See .github/workflows/build.yml for the equivalent Docker-based path.

set -euo pipefail

REPO="hldtux/ps3dev"
REF="latest"
PS3DEV="/usr/local/ps3dev"
PROJECT="${1:-$(pwd)}"

TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT

say() { printf '\n=== %s ===\n' "$*"; }

# ---------------------------------------------------------------- host deps --
say "installing build dependencies"
export DEBIAN_FRONTEND=noninteractive
apt-get update -qq
# libelf  -> sprxlinker
# libgmp  -> fself / make_self_npdrm
# libz, libssl -> binutils and gcc host libraries
apt-get install -y -qq --no-install-recommends \
    make ca-certificates curl xz-utils zstd \
    libelf-dev libgmp-dev zlib1g-dev libssl-dev python3

# ------------------------------------------------------------- registry auth --
say "authorising against the registry"
TOKEN=$(curl -fsSL \
  "https://auth.docker.io/token?service=registry.docker.io&scope=repository:${REPO}:pull" |
  python3 -c 'import sys,json; print(json.load(sys.stdin)["token"])')

ACCEPT_INDEX="application/vnd.docker.distribution.manifest.list.v2+json"
ACCEPT_IMAGE="application/vnd.oci.image.index.v1+json"
ACCEPT_MANIFEST="application/vnd.docker.distribution.manifest.v2+json"
ACCEPT_OCI="application/vnd.oci.image.manifest.v1+json"

# ---------------------------------------------------------------- manifests --
say "fetching the manifest"
curl -fsSL -H "Authorization: Bearer ${TOKEN}" \
  -H "Accept: ${ACCEPT_INDEX}, ${ACCEPT_IMAGE}" \
  "https://registry-1.docker.io/v2/${REPO}/manifests/${REF}" -o "$TMP/index.json"

DIGEST=$(python3 - "$TMP/index.json" <<'PY'
import json, sys
doc = json.load(open(sys.argv[1]))
if "manifests" not in doc:          # a single-platform manifest already
    print("")
    raise SystemExit(0)
for m in doc["manifests"]:
    p = m.get("platform", {})
    if p.get("os") == "linux" and p.get("architecture") == "amd64":
        print(m["digest"])
        break
else:
    raise SystemExit("no linux/amd64 manifest in the index")
PY
)

if [ -n "$DIGEST" ]; then
  curl -fsSL -H "Authorization: Bearer ${TOKEN}" \
    -H "Accept: ${ACCEPT_MANIFEST}, ${ACCEPT_OCI}" \
    "https://registry-1.docker.io/v2/${REPO}/manifests/${DIGEST}" -o "$TMP/manifest.json"
else
  cp "$TMP/index.json" "$TMP/manifest.json"
fi

# ------------------------------------------------------------- layer extract --
say "extracting ${PS3DEV} from the image layers"
python3 - "$TMP/manifest.json" > "$TMP/layers.tsv" <<'PY'
import json, sys
for l in json.load(open(sys.argv[1]))["layers"]:
    print(l["digest"], l.get("mediaType", ""))
PY

COUNT=0
while read -r LDIGEST LTYPE; do
  [ -n "$LDIGEST" ] || continue
  COUNT=$((COUNT + 1))
  printf '  layer %2d  %s\n' "$COUNT" "${LTYPE##*/}"

  curl -fsSL -H "Authorization: Bearer ${TOKEN}" \
    "https://registry-1.docker.io/v2/${REPO}/blobs/${LDIGEST}" -o "$TMP/layer.bin"

  case "$LTYPE" in
    *zstd*) DECOMP="zstd -dc" ;;
    *gzip*|tar) DECOMP="gzip -dc" ;;
    *) DECOMP="gzip -dc" ;;
  esac

  # Only usr/local/ps3vd members are wanted. tar exits non-zero when a layer
  # does not contain the path at all, which is normal.
  $DECOMP "$TMP/layer.bin" 2>/dev/null | tar -x usr/local/ps3dev 2>/dev/null || true
  rm -f "$TMP/layer.bin"
done < "$TMP/layers.tsv"

# ------------------------------------------------------------------ sanity --
say "verifying the toolchain"
test -f "${PS3DEV}/ppu_rules"  || { echo "MISSING ${PS3DEV}/ppu_rules";  exit 1; }
test -f "${PS3DEV}/base_rules" || { echo "MISSING ${PS3DEV}/base_rules"; exit 1; }
"${PS3DEV}/ppu/bin/ppu-gcc" --version | head -1
if [ ! -f "${PS3DEV}/portlibs/ppu/lib/libSDL.a" ]; then
  echo "MISSING ${PS3DEV}/portlibs/ppu/lib/libSDL.a - this image has no SDL 1.3"
  exit 1
fi
echo "SDL 1.3 present"

# -------------------------------------------------------------------- build --
export PS3DEV
export PSL1GHT="${PS3DEV}"
export PATH="${PS3DEV}/bin:${PS3DEV}/ppu/bin:${PS3DEV}/spu/bin:${PS3DEV}/portlibs/ppu/bin:${PATH}"

cd "$PROJECT"
say "building the self"
make clean || true
make
file build/DVDREGION.elf || true

say "building the pkg"
if make pkg; then
  ls -l ./*.pkg
else
  echo "make pkg failed; the .self is still usable"
fi

say "results"
ls -l DVDREGION.self ./*.pkg 2>/dev/null || true
echo
echo "Install the NPDRM pkg on CFW/HEN, or copy build/pkg/USRDIR/EBOOT.BIN"
echo "plus build/pkg/PARAM.SFO into /dev_hdd0/game/DVDRGN01000/ on the console."