#!/usr/bin/env bash
# Build the offline litmus-gen CLI binary.
#
# Links against llvm-build-plain (no sancov, no asan) — this is an analysis
# tool, not an instrumented harness. Mirrors build_coverage_probe.sh but
# pulls libs from $LLVM_BUILD_PLAIN.

source "$(dirname "$0")/env.sh"
check_prereqs

echo "=== Building litmus-gen ==="

if [[ ! -f "$LLVM_BUILD_PLAIN/build.ninja" ]]; then
    echo "ERROR: LLVM plain build not found at $LLVM_BUILD_PLAIN" >&2
    exit 1
fi

if [[ ! -f "$PROJECT_ROOT/deps/nlohmann-json/json.hpp" ]]; then
    echo "ERROR: nlohmann/json header not found at deps/nlohmann-json/json.hpp" >&2
    exit 1
fi

mkdir -p "$BUILD_OUT"

echo "[litmus-gen] Extracting LLVM plain link libraries..."
mapfile -t LINK_LIBS < <(
    cd "$LLVM_BUILD_PLAIN" &&
    ninja -t commands bin/opt 2>/dev/null |
    grep -E '(clang\+\+|ld\.lld).*bin/opt' |
    grep -oP 'lib/\S+\.a' |
    sort -u |
    while read -r lib; do echo "$LLVM_BUILD_PLAIN/$lib"; done
)
echo "[litmus-gen] Found ${#LINK_LIBS[@]} plain LLVM libraries"

LITMUS_INCLUDES=(
    -I"$PROJECT_ROOT"
    -I"$LLVM_BUILD_PLAIN/include"
    -I"$LLVM_SRC/llvm/include"
)

LITMUS_SRCS=(
    "$PROJECT_ROOT/src/litmus-gen/main.cc"
    "$PROJECT_ROOT/src/litmus-gen/rule_parser.cc"
    "$PROJECT_ROOT/src/litmus-gen/type_instantiation.cc"
    "$PROJECT_ROOT/src/litmus-gen/generators.cc"
)

LITMUS_BIN="$BUILD_OUT/litmus-gen"

# nlohmann/json requires C++17. No sanitizers, no sancov: this is an
# offline generator, not part of the instrumented fuzz pipeline.
LITMUS_CFLAGS=(
    "-g"
    "-O2"
    "-fno-omit-frame-pointer"
)

echo "[litmus-gen] Compiling + linking litmus-gen (plain LLVM)..."
$CXX "${LITMUS_CFLAGS[@]}" -std=c++17 -fno-rtti \
    "${LITMUS_INCLUDES[@]}" \
    "${LITMUS_SRCS[@]}" \
    -fuse-ld=lld \
    -Wl,--start-group \
    "${LINK_LIBS[@]}" \
    -Wl,--end-group \
    -ldl -lrt -lpthread -lm -lz -ltinfo \
    -o "$LITMUS_BIN"

echo "[litmus-gen] ✓ $LITMUS_BIN ($(du -h "$LITMUS_BIN" | cut -f1))"
