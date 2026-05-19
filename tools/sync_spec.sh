#!/usr/bin/env bash
#
# Refreshes spec/ if symlinks aren't usable (e.g. you took a tarball
# of MeshRoute to a machine without the sibling Lua repo). Defaults to
# the development layout (sibling repo named lora-universal-simulator);
# pass an alternate path or tarball URL as $1.
#
# Usage:
#   bash tools/sync_spec.sh                          # symlink to ../lora-universal-simulator
#   bash tools/sync_spec.sh /path/to/lora-universal-simulator
#   bash tools/sync_spec.sh https://github.com/.../archive/main.tar.gz

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
SPEC_DIR="$REPO_ROOT/spec"

src="${1:-$REPO_ROOT/../lora-universal-simulator}"

# Clean spec/ contents (preserve the directory itself)
rm -f "$SPEC_DIR"/dv_dual_sf.lua
rm -rf "$SPEC_DIR"/docs "$SPEC_DIR"/test "$SPEC_DIR"/scenarios

if [[ "$src" =~ ^https?:// ]]; then
  echo "Fetching spec from $src ..."
  tmp="$(mktemp -d)"
  curl -sSL "$src" | tar -xz -C "$tmp"
  src_root="$(find "$tmp" -maxdepth 2 -name 'dv_dual_sf.lua' -printf '%h\n' | head -1)/.."
  cp "$src_root/scenarios/dv_dual_sf.lua" "$SPEC_DIR/dv_dual_sf.lua"
  cp -r "$src_root/docs" "$SPEC_DIR/docs"
  cp -r "$src_root/test" "$SPEC_DIR/test"
  cp -r "$src_root/scenarios" "$SPEC_DIR/scenarios"
  rm -rf "$tmp"
  echo "Hard-copied spec from tarball into $SPEC_DIR"
elif [[ -d "$src" ]]; then
  if [[ "$src" = /* ]]; then
    abs="$src"
  else
    abs="$(cd "$REPO_ROOT" && cd "$src" && pwd)"
  fi
  ln -sf "$abs/scenarios/dv_dual_sf.lua" "$SPEC_DIR/dv_dual_sf.lua"
  ln -sf "$abs/docs"                     "$SPEC_DIR/docs"
  ln -sf "$abs/test"                     "$SPEC_DIR/test"
  ln -sf "$abs/scenarios"                "$SPEC_DIR/scenarios"
  echo "Symlinked spec → $abs"
else
  echo "ERROR: '$src' is not a directory or a URL" >&2
  exit 1
fi
