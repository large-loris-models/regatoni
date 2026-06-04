#!/usr/bin/env bash
# Build the spec-mutate CLI binary.
#
# Mirrors other build scripts in scripts/build/: plain LLVM, no sanitizers. The tool
# parses a JSON rewrite spec, walks IR, clones modules, applies the
# spec's transforms, and verifies each variant.

source "$(dirname "$0")/env.sh"
check_prereqs

echo "=== Building spec-mutate ==="

if [[ ! -f "$LLVM_BUILD_PLAIN/build.ninja" ]]; then
    echo "ERROR: LLVM plain build not found at $LLVM_BUILD_PLAIN" >&2
    exit 1
fi

if [[ ! -f "$PROJECT_ROOT/deps/nlohmann-json/json.hpp" ]]; then
    echo "ERROR: nlohmann/json header not found at deps/nlohmann-json/json.hpp" >&2
    exit 1
fi

mkdir -p "$BUILD_OUT"

echo "[spec-mutate] Extracting LLVM plain link libraries..."
mapfile -t LINK_LIBS < <(
    cd "$LLVM_BUILD_PLAIN" &&
    ninja -t commands bin/opt 2>/dev/null |
    grep -E '(clang\+\+|ld\.lld).*bin/opt' |
    grep -oP 'lib/\S+\.a' |
    sort -u |
    while read -r lib; do echo "$LLVM_BUILD_PLAIN/$lib"; done
)
echo "[spec-mutate] Found ${#LINK_LIBS[@]} plain LLVM libraries"

SM_INCLUDES=(
    -I"$PROJECT_ROOT"
    -I"$LLVM_BUILD_PLAIN/include"
    -I"$LLVM_SRC/llvm/include"
)

SM_SRCS=(
    "$PROJECT_ROOT/src/spec-mutate/main.cc"
    "$PROJECT_ROOT/src/spec-mutate/spec.cc"
    "$PROJECT_ROOT/src/spec-mutate/match_engine.cc"
    "$PROJECT_ROOT/src/spec-mutate/transform_engine.cc"
)

SM_BIN="$BUILD_OUT/spec-mutate"

SM_CFLAGS=(
    "-g"
    "-O2"
    "-fno-omit-frame-pointer"
)

echo "[spec-mutate] Compiling + linking spec-mutate (plain LLVM)..."
$CXX "${SM_CFLAGS[@]}" -std=c++17 -fno-rtti \
    "${SM_INCLUDES[@]}" \
    "${SM_SRCS[@]}" \
    -fuse-ld=lld \
    -Wl,--start-group \
    "${LINK_LIBS[@]}" \
    -Wl,--end-group \
    -ldl -lrt -lpthread -lm -lz -ltinfo \
    -o "$SM_BIN"

echo "[spec-mutate] ✓ $SM_BIN ($(du -h "$SM_BIN" | cut -f1))"
