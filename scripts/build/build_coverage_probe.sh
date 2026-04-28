#!/usr/bin/env bash
# Build the standalone coverage_probe binary.
#
# Same sancov-instrumented LLVM libraries as opt_fuzz_target, but no Centipede
# runner — coverage_probe provides its own __sanitizer_cov_*_init hooks.

source "$(dirname "$0")/env.sh"
check_prereqs

echo "=== Building coverage_probe ==="

if [[ ! -f "$LLVM_BUILD/build.ninja" ]]; then
    echo "ERROR: LLVM sancov build not found at $LLVM_BUILD" >&2
    exit 1
fi

mkdir -p "$BUILD_OUT"

echo "[probe] Extracting LLVM sancov link libraries..."
mapfile -t LINK_LIBS < <(
    cd "$LLVM_BUILD" &&
    ninja -t commands bin/opt 2>/dev/null |
    grep -E '(clang\+\+|ld\.lld).*bin/opt' |
    grep -oP 'lib/\S+\.a' |
    sort -u |
    while read -r lib; do echo "$LLVM_BUILD/$lib"; done
)
echo "[probe] Found ${#LINK_LIBS[@]} sancov LLVM libraries"

PROBE_INCLUDES=(
    -I"$PROJECT_ROOT"
    -I"$LLVM_BUILD/include"
    -I"$LLVM_SRC/llvm/include"
)

PROBE_SRC="$PROJECT_ROOT/src/tools/coverage_probe.cc"
PROBE_BIN="$BUILD_OUT/coverage_probe"

echo "[probe] Compiling + linking coverage_probe (sancov, no Centipede)..."
$CXX "${FUZZ_CFLAGS[@]}" -std=c++17 -fno-rtti \
    "${PROBE_INCLUDES[@]}" \
    "$PROBE_SRC" \
    -fuse-ld=lld \
    -Wl,--start-group \
    "${LINK_LIBS[@]}" \
    -Wl,--end-group \
    -ldl -lrt -lpthread -lm -lz -ltinfo \
    -o "$PROBE_BIN"

echo "[probe] ✓ $PROBE_BIN ($(du -h "$PROBE_BIN" | cut -f1))"
