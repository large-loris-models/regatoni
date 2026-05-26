#!/usr/bin/env bash
# Build the offline corpus-analysis CLI binary.
#
# Links against llvm-build-plain (no sancov, no asan) — this is an analysis
# tool, not an instrumented harness. Mirrors build_litmus_gen.sh; uses
# LLVM's IR reader/IR API to compute per-function structural metrics.

source "$(dirname "$0")/env.sh"
check_prereqs

echo "=== Building corpus-analysis ==="

if [[ ! -f "$LLVM_BUILD_PLAIN/build.ninja" ]]; then
    echo "ERROR: LLVM plain build not found at $LLVM_BUILD_PLAIN" >&2
    exit 1
fi

if [[ ! -f "$PROJECT_ROOT/deps/nlohmann-json/json.hpp" ]]; then
    echo "ERROR: nlohmann/json header not found at deps/nlohmann-json/json.hpp" >&2
    exit 1
fi

mkdir -p "$BUILD_OUT"

echo "[corpus-analysis] Extracting LLVM plain link libraries..."
mapfile -t LINK_LIBS < <(
    cd "$LLVM_BUILD_PLAIN" &&
    ninja -t commands bin/opt 2>/dev/null |
    grep -E '(clang\+\+|ld\.lld).*bin/opt' |
    grep -oP 'lib/\S+\.a' |
    sort -u |
    while read -r lib; do echo "$LLVM_BUILD_PLAIN/$lib"; done
)
echo "[corpus-analysis] Found ${#LINK_LIBS[@]} plain LLVM libraries"

CA_INCLUDES=(
    -I"$PROJECT_ROOT"
    -I"$LLVM_BUILD_PLAIN/include"
    -I"$LLVM_SRC/llvm/include"
)

CA_SRCS=(
    "$PROJECT_ROOT/src/corpus-analysis/main.cc"
)

CA_BIN="$BUILD_OUT/corpus-analysis"

CA_CFLAGS=(
    "-g"
    "-O2"
    "-fno-omit-frame-pointer"
)

echo "[corpus-analysis] Compiling + linking corpus-analysis (plain LLVM)..."
$CXX "${CA_CFLAGS[@]}" -std=c++17 -fno-rtti \
    "${CA_INCLUDES[@]}" \
    "${CA_SRCS[@]}" \
    -fuse-ld=lld \
    -Wl,--start-group \
    "${LINK_LIBS[@]}" \
    -Wl,--end-group \
    -ldl -lrt -lpthread -lm -lz -ltinfo \
    -o "$CA_BIN"

echo "[corpus-analysis] ✓ $CA_BIN ($(du -h "$CA_BIN" | cut -f1))"
