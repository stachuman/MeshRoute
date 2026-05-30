#!/usr/bin/env bash
#
# Re-sync the vendored MeshCore PHY glue + the XIAO board def from a pinned
# MeshCore checkout, BYTE-IDENTICAL. We reuse the SX1262 PHY ONLY (not the
# Dispatcher/mesh stack) — see lib/meshcore/NOTICE.
#
# Usage:
#   bash tools/vendor_meshcore.sh [path-to-MeshCore-checkout]
#     (default: ../MeshCore)
#
# After running, `git diff` shows exactly what upstream changed. If the commit
# moved, update MESHCORE_VERSION + the date in lib/meshcore/NOTICE by hand.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
SRC="${1:-$REPO_ROOT/../MeshCore}"

if [[ ! -d "$SRC" ]]; then
  echo "ERROR: MeshCore checkout '$SRC' not found" >&2
  exit 1
fi

# The exact vendored set: 2 PHY headers + the custom board JSON + its ldscript.
# Format: "<src-relative-to-MeshCore>  <dst-relative-to-MeshRoute>"
FILES=(
  "src/helpers/radiolib/CustomSX1262.h            lib/meshcore/src/helpers/radiolib/CustomSX1262.h"
  "src/helpers/radiolib/SX126xReset.h             lib/meshcore/src/helpers/radiolib/SX126xReset.h"
  "boards/seeed-xiao-afruitnrf52-nrf52840.json    boards/seeed-xiao-afruitnrf52-nrf52840.json"
  "boards/nrf52840_s140_v7.ld                     boards/nrf52840_s140_v7.ld"
  "variants/xiao_nrf52/variant.h                  variants/Seeed_XIAO_nRF52840/variant.h"
  "variants/xiao_nrf52/variant.cpp                variants/Seeed_XIAO_nRF52840/variant.cpp"
)

for entry in "${FILES[@]}"; do
  # shellcheck disable=SC2086
  set -- $entry
  s="$SRC/$1"; d="$REPO_ROOT/$2"
  if [[ ! -f "$s" ]]; then
    echo "ERROR: expected upstream file missing: $s" >&2
    exit 1
  fi
  mkdir -p "$(dirname "$d")"
  cp "$s" "$d"
  echo "vendored  $1  ->  $2"
done

# The MIT license, verbatim (covers the vendored files).
if [[ -f "$SRC/LICENSE" ]]; then
  cp "$SRC/LICENSE" "$REPO_ROOT/lib/meshcore/license.txt"
  echo "vendored  LICENSE  ->  lib/meshcore/license.txt"
fi

if commit="$(git -C "$SRC" rev-parse HEAD 2>/dev/null)"; then
  echo
  echo "MeshCore HEAD: $commit"
  echo "If it differs from MESHCORE_VERSION in lib/meshcore/NOTICE, update that file."
fi
