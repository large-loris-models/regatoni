#!/usr/bin/env bash

source "$(dirname "$0")/env.sh"
check_prereqs

echo "=== Linking fuzz target ==="

for f in "$CENTIPEDE_RUNNER"; do
    if [[ ! -f "$f" ]]; then
        echo "ERROR: Not found: $f" >&2
        exit 1
    fi
done

if [[ ! -f "$LLVM_BUILD/build.ninja" ]]; then
    echo "ERROR: LLVM sancov build not found at $LLVM_BUILD" >&2
    exit 1
fi

mkdir -p "$BUILD_OUT"

# Extract LLVM link libraries from the opt build
echo "[link] Extracting LLVM link libraries..."
mapfile -t LINK_LIBS < <(
    cd "$LLVM_BUILD" &&
    ninja -t commands bin/opt 2>/dev/null |
    grep -E '(clang\+\+|ld\.lld).*bin/opt' |
    grep -oP 'lib/\S+\.a' |
    sort -u |
    while read -r lib; do echo "$LLVM_BUILD/$lib"; done
)
echo "[link] Found ${#LINK_LIBS[@]} LLVM libraries"

# Include flags
INCLUDE_FLAGS=(
    -I"$LLVM_BUILD/include"
    -I"$LLVM_SRC/llvm/include"
)

# Compile harness with sancov
HARNESS_SRC="$PROJECT_ROOT/src/harness/opt_fuzz_target.cc"
HARNESS_OBJ="$BUILD_OUT/opt_fuzz_target.o"
FUZZ_TARGET="$BUILD_OUT/opt_fuzz_target"

echo "[link] Compiling harness..."
$CXX ${FUZZ_CFLAGS[@]} -std=c++17 \
    "${INCLUDE_FLAGS[@]}" \
    -c "$HARNESS_SRC" \
    -o "$HARNESS_OBJ"

echo "[link] Linking..."
$CXX -std=c++17 \
    -fno-sanitize=all \
    -fuse-ld=lld \
    "$HARNESS_OBJ" \
    -Wl,--start-group \
    "${LINK_LIBS[@]}" \
    "$CENTIPEDE_RUNNER" \
    -Wl,--end-group \
    -ldl -lrt -lpthread -lm -lz -ltinfo \
    -o "$FUZZ_TARGET"

echo "[link] ✓ $FUZZ_TARGET ($(du -h "$FUZZ_TARGET" | cut -f1))"