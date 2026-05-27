#!/usr/bin/env bash
# Build the rewrite-seeds CLI binary.
#
# Mirrors build_enrich_seeds.sh: plain LLVM, no sanitizers. The tool
# parses a JSON rewrite spec, walks IR, clones modules, applies the
# spec's transforms, and verifies each variant.

source "$(dirname "$0")/env.sh"
check_prereqs

echo "=== Building rewrite-seeds ==="

if [[ ! -f "$LLVM_BUILD_PLAIN/build.ninja" ]]; then
    echo "ERROR: LLVM plain build not found at $LLVM_BUILD_PLAIN" >&2
    exit 1
fi

if [[ ! -f "$PROJECT_ROOT/deps/nlohmann-json/json.hpp" ]]; then
    echo "ERROR: nlohmann/json header not found at deps/nlohmann-json/json.hpp" >&2
    exit 1
fi

mkdir -p "$BUILD_OUT"

echo "[rewrite-seeds] Extracting LLVM plain link libraries..."
mapfile -t LINK_LIBS < <(
    cd "$LLVM_BUILD_PLAIN" &&
    ninja -t commands bin/opt 2>/dev/null |
    grep -E '(clang\+\+|ld\.lld).*bin/opt' |
    grep -oP 'lib/\S+\.a' |
    sort -u |
    while read -r lib; do echo "$LLVM_BUILD_PLAIN/$lib"; done
)
echo "[rewrite-seeds] Found ${#LINK_LIBS[@]} plain LLVM libraries"

RS_INCLUDES=(
    -I"$PROJECT_ROOT"
    -I"$LLVM_BUILD_PLAIN/include"
    -I"$LLVM_SRC/llvm/include"
)

RS_SRCS=(
    "$PROJECT_ROOT/src/rewrite-seeds/main.cc"
    "$PROJECT_ROOT/src/rewrite-seeds/spec.cc"
    "$PROJECT_ROOT/src/rewrite-seeds/match_engine.cc"
    "$PROJECT_ROOT/src/rewrite-seeds/transform_engine.cc"
)

RS_BIN="$BUILD_OUT/rewrite-seeds"

RS_CFLAGS=(
    "-g"
    "-O2"
    "-fno-omit-frame-pointer"
)

echo "[rewrite-seeds] Compiling + linking rewrite-seeds (plain LLVM)..."
$CXX "${RS_CFLAGS[@]}" -std=c++17 -fno-rtti \
    "${RS_INCLUDES[@]}" \
    "${RS_SRCS[@]}" \
    -fuse-ld=lld \
    -Wl,--start-group \
    "${LINK_LIBS[@]}" \
    -Wl,--end-group \
    -ldl -lrt -lpthread -lm -lz -ltinfo \
    -o "$RS_BIN"

echo "[rewrite-seeds] ✓ $RS_BIN ($(du -h "$RS_BIN" | cut -f1))"
