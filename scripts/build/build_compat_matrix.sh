#!/usr/bin/env bash
# Build the offline compat-matrix CLI binary.
#
# Mirrors build_litmus_gen.sh: plain LLVM build, no sanitizers. The tool
# itself is JSON-only and doesn't link any LLVM symbols, but we keep the
# library group for consistency with the rest of the analysis tools so
# adding LLVM-using helpers later is a one-line change.

source "$(dirname "$0")/env.sh"
check_prereqs

echo "=== Building compat-matrix ==="

if [[ ! -f "$LLVM_BUILD_PLAIN/build.ninja" ]]; then
    echo "ERROR: LLVM plain build not found at $LLVM_BUILD_PLAIN" >&2
    exit 1
fi

if [[ ! -f "$PROJECT_ROOT/deps/nlohmann-json/json.hpp" ]]; then
    echo "ERROR: nlohmann/json header not found at deps/nlohmann-json/json.hpp" >&2
    exit 1
fi

mkdir -p "$BUILD_OUT"

echo "[compat-matrix] Extracting LLVM plain link libraries..."
mapfile -t LINK_LIBS < <(
    cd "$LLVM_BUILD_PLAIN" &&
    ninja -t commands bin/opt 2>/dev/null |
    grep -E '(clang\+\+|ld\.lld).*bin/opt' |
    grep -oP 'lib/\S+\.a' |
    sort -u |
    while read -r lib; do echo "$LLVM_BUILD_PLAIN/$lib"; done
)
echo "[compat-matrix] Found ${#LINK_LIBS[@]} plain LLVM libraries"

COMPAT_INCLUDES=(
    -I"$PROJECT_ROOT"
    -I"$LLVM_BUILD_PLAIN/include"
    -I"$LLVM_SRC/llvm/include"
)

COMPAT_SRCS=(
    "$PROJECT_ROOT/src/compat-matrix/main.cc"
)

COMPAT_BIN="$BUILD_OUT/compat-matrix"

COMPAT_CFLAGS=(
    "-g"
    "-O2"
    "-fno-omit-frame-pointer"
)

echo "[compat-matrix] Compiling + linking compat-matrix (plain LLVM)..."
$CXX "${COMPAT_CFLAGS[@]}" -std=c++17 -fno-rtti \
    "${COMPAT_INCLUDES[@]}" \
    "${COMPAT_SRCS[@]}" \
    -fuse-ld=lld \
    -Wl,--start-group \
    "${LINK_LIBS[@]}" \
    -Wl,--end-group \
    -ldl -lrt -lpthread -lm -lz -ltinfo \
    -o "$COMPAT_BIN"

echo "[compat-matrix] ✓ $COMPAT_BIN ($(du -h "$COMPAT_BIN" | cut -f1))"
