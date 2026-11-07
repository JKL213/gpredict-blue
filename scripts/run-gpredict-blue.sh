#!/bin/sh
# Start an uninstalled Gpredict-Blue build with repo pixmaps/data.
set -eu
ROOT=$(CDPATH= cd "$(dirname "$0")/.." && pwd)
BIN="$ROOT/src/gpredict"
if ! test -x "$BIN"; then
  echo "Binary missing; run: make -C $ROOT/src gpredict" >&2
  exit 1
fi
if ! test -f "$ROOT/pixmaps/maps/earth_800.png"; then
  echo "Missing $ROOT/pixmaps/maps/earth_800.png (incomplete checkout?)" >&2
  exit 1
fi
export GPREDICT_PIXMAPS_DIR="$ROOT/pixmaps"
export GPREDICT_DATA_DIR="$ROOT"
exec "$BIN" "$@"
