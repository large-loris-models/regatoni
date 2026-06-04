#!/usr/bin/env bash
# Build the spec-inject CLI binary.
#
# Mirrors build_spec_mutate.sh: plain LLVM, no sanitizers. The tool
# loads a litmus library, walks host .ll files, links litmus modules
# into host clones, splices CallInst, and writes injected modules.
#
# Adds the LLVMLinker static library to the link line — it is not in
# `opt`'s default dependency set but is required by
# `llvm::Linker::linkModules`.

source "$(dirname "$0")/env.sh"
check_prereqs

echo "=== Building spec-inject ==="

if [[ ! -f "$LLVM_BUILD_PLAIN/build.ninja" ]]; then
    echo "ERROR: LLVM plain build not found at $LLVM_BUILD_PLAIN" >&2
    exit 1
fi

if [[ ! -f "$PROJECT_ROOT/deps/nlohmann-json/json.hpp" ]]; then
    echo "ERROR: nlohmann/json header not found at deps/nlohmann-json/json.hpp" >&2
    exit 1
fi

LINKER_LIB="$LLVM_BUILD_PLAIN/lib/libLLVMLinker.a"
if [[ ! -f "$LINKER_LIB" ]]; then
    echo "ERROR: libLLVMLinker.a not found at $LINKER_LIB" >&2
    exit 1
fi

mkdir -p "$BUILD_OUT"

echo "[spec-inject] Extracting LLVM plain link libraries..."
mapfile -t LINK_LIBS < <(
    cd "$LLVM_BUILD_PLAIN" &&
    ninja -t commands bin/opt 2>/dev/null |
    grep -E '(clang\+\+|ld\.lld).*bin/opt' |
    grep -oP 'lib/\S+\.a' |
    sort -u |
    while read -r lib; do echo "$LLVM_BUILD_PLAIN/$lib"; done
)
# Ensure LLVMLinker is in the set (it isn't pulled in by opt's link line).
LINK_LIBS+=("$LINKER_LIB")
echo "[spec-inject] Found ${#LINK_LIBS[@]} plain LLVM libraries (incl. LLVMLinker)"

SI_INCLUDES=(
    -I"$PROJECT_ROOT"
    -I"$LLVM_BUILD_PLAIN/include"
    -I"$LLVM_SRC/llvm/include"
)

SI_SRCS=(
    "$PROJECT_ROOT/src/spec-inject/main.cc"
    "$PROJECT_ROOT/src/spec-inject/litmus_library.cc"
    "$PROJECT_ROOT/src/spec-inject/injector.cc"
)

SI_BIN="$BUILD_OUT/spec-inject"

SI_CFLAGS=(
    "-g"
    "-O2"
    "-fno-omit-frame-pointer"
)

echo "[spec-inject] Compiling + linking spec-inject (plain LLVM)..."
$CXX "${SI_CFLAGS[@]}" -std=c++17 -fno-rtti \
    "${SI_INCLUDES[@]}" \
    "${SI_SRCS[@]}" \
    -fuse-ld=lld \
    -Wl,--start-group \
    "${LINK_LIBS[@]}" \
    -Wl,--end-group \
    -ldl -lrt -lpthread -lm -lz -ltinfo \
    -o "$SI_BIN"

echo "[spec-inject] ✓ $SI_BIN ($(du -h "$SI_BIN" | cut -f1))"
