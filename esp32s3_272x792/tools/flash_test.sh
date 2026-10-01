#!/usr/bin/env bash
# Build + flash the desk-buddy 5.79" e-paper net-status firmware (COM4, CH340).
# NOTE: run with native C:/ paths — see docs/bringup.md (flash_test.sh MSYS gotcha).
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
REPO="$(cd "$ROOT/.." && pwd)"
export PATH="$PATH:/c/Users/m/bin"
B="esp32:esp32:esp32s3:PSRAM=opi,FlashMode=qio,FlashSize=8M"
LIB="$ROOT/lib/EDP"
# shared/net_config.h (generated from shared/net.env) — WiFi creds + LAN endpoint
INC="--build-property compiler.c.extra_flags=-I$REPO/shared --build-property compiler.SS.extra_flags=-I$REPO/shared --build-property compiler.cpp.extra_flags=-I$REPO/shared"

arduino-cli compile -b "$B" --library "$LIB" $INC "$ROOT/fw/test_sketch"
arduino-cli upload  -p COM4 -b "$B" "$ROOT/fw/test_sketch"
