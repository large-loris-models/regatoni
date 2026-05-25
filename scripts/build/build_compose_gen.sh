#!/usr/bin/env bash
# Build the offline compose-gen CLI binary.
#
# Mirrors build_compat_matrix.sh: plain LLVM build, no sanitizers. The tool
# itself is JSON-only (no LLVM linkage required at the moment) but we keep
# the link group for consistency with the other analysis CLIs, so the
# next person who wants to link an LLVM helper into compose-gen has a
# one-line change instead of a rebuild script rewrite.

source "$(dirname "$0")/env.sh"
check_prereqs

echo "=== Building compose-gen ==="

if [[ ! -f "$LLVM_BUILD_PLAIN/build.ninja" ]]; then
    echo "ERROR: LLVM plain build not found at $LLVM_BUILD_PLAIN" >&2
    exit 1
fi

if [[ ! -f "$PROJECT_ROOT/deps/nlohmann-json/json.hpp" ]]; then
    echo "ERROR: nlohmann/json header not found at deps/nlohmann-json/json.hpp" >&2
    exit 1
fi

mkdir -p "$BUILD_OUT"

echo "[compose-gen] Extracting LLVM plain link libraries..."
mapfile -t LINK_LIBS < <(
    cd "$LLVM_BUILD_PLAIN" &&
    ninja -t commands bin/opt 2>/dev/null |
    grep -E '(clang\+\+|ld\.lld).*bin/opt' |
    grep -oP 'lib/\S+\.a' |
    sort -u |
    while read -r lib; do echo "$LLVM_BUILD_PLAIN/$lib"; done
)
echo "[compose-gen] Found ${#LINK_LIBS[@]} plain LLVM libraries"

COMPOSE_INCLUDES=(
    -I"$PROJECT_ROOT"
    -I"$LLVM_BUILD_PLAIN/include"
    -I"$LLVM_SRC/llvm/include"
)

COMPOSE_SRCS=(
    "$PROJECT_ROOT/src/compose-gen/main.cc"
)

COMPOSE_BIN="$BUILD_OUT/compose-gen"

COMPOSE_CFLAGS=(
    "-g"
    "-O2"
    "-fno-omit-frame-pointer"
)

echo "[compose-gen] Compiling + linking compose-gen (plain LLVM)..."
$CXX "${COMPOSE_CFLAGS[@]}" -std=c++17 -fno-rtti \
    "${COMPOSE_INCLUDES[@]}" \
    "${COMPOSE_SRCS[@]}" \
    -fuse-ld=lld \
    -Wl,--start-group \
    "${LINK_LIBS[@]}" \
    -Wl,--end-group \
    -ldl -lrt -lpthread -lm -lz -ltinfo \
    -o "$COMPOSE_BIN"

echo "[compose-gen] ✓ $COMPOSE_BIN ($(du -h "$COMPOSE_BIN" | cut -f1))"
