#!/usr/bin/env bash

set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$REPO_ROOT"

: "${ANDROID_NDK_HOME:?ANDROID_NDK_HOME must point at an installed NDK (e.g. the r27 LTS release)}"
NDK_BUILD="$ANDROID_NDK_HOME/ndk-build"
[ -x "$NDK_BUILD" ] || { echo "error: '$NDK_BUILD' not found or not executable" >&2; exit 1; }

VERSION="$(sed -n 's/^version=//p' module/module.prop)"
VERSION_CODE="$(sed -n 's/^versionCode=//p' module/module.prop)"
[ -n "$VERSION" ] || { echo "error: could not read version= from module/module.prop" >&2; exit 1; }
OUT_ZIP="ih8SecureLock-${VERSION}.zip"

echo "==> Building native library for all ABIs (ndk-build, $(nproc) jobs)"
"$NDK_BUILD" \
  -C zygisk \
  NDK_PROJECT_PATH=. \
  APP_BUILD_SCRIPT=./jni/Android.mk \
  NDK_APPLICATION_MK=./jni/Application.mk \
  -j"$(nproc)"

STAGE="$(mktemp -d)"
trap 'rm -rf "$STAGE"' EXIT

cp -a module/. "$STAGE/"
mkdir -p "$STAGE/zygisk"

mapfile -t ABIS < <(grep -m1 '^APP_ABI' zygisk/jni/Application.mk | sed 's/^APP_ABI[[:space:]]*:=[[:space:]]*//' | tr -s ' ' '\n')
if [ "${#ABIS[@]}" -eq 0 ]; then
  echo "error: couldn't parse APP_ABI out of zygisk/jni/Application.mk" >&2
  exit 1
fi
for abi in "${ABIS[@]}"; do
  src="zygisk/libs/${abi}/libih8securelock.so"
  if [ ! -f "$src" ]; then
    echo "error: missing build output for ABI '$abi' ($src)" >&2
    echo "       did ndk-build actually run for all of: ${ABIS[*]}?" >&2
    exit 1
  fi
  cp "$src" "$STAGE/zygisk/${abi}.so"
done

rm -f "$OUT_ZIP" "${OUT_ZIP}.sha256"
( cd "$STAGE" && zip -r9 -X "$REPO_ROOT/$OUT_ZIP" . -x '.*' )

sha256sum "$OUT_ZIP" > "${OUT_ZIP}.sha256"

echo "==> Built $OUT_ZIP (version $VERSION, versionCode $VERSION_CODE)"
echo "==> $(cat "${OUT_ZIP}.sha256")"

rm -rf debug-symbols
mkdir -p debug-symbols
for abi in "${ABIS[@]}"; do
  obj="zygisk/obj/local/${abi}/libih8securelock.so"
  [ -f "$obj" ] && cp "$obj" "debug-symbols/${abi}.so"
done
echo "==> Unstripped debug symbols copied to ./debug-symbols/"
