#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
DEPS_DIR="$PROJECT_ROOT/deps"

source "$PROJECT_ROOT/deps.conf"

JOBS="${JOBS:-$(nproc)}"

mkdir -p "$DEPS_DIR"

echo "=== Regatoni: Setting up dependencies ==="
echo "  Parallel jobs: $JOBS"
echo "  Deps dir:      $DEPS_DIR"
echo ""

# --- Submodules ---
echo "[1/5] Initializing submodules..."
cd "$PROJECT_ROOT"
git submodule update --init --recursive

# --- LLVM source ---
echo "[2/5] LLVM source..."
LLVM_SRC="$DEPS_DIR/llvm-project"

if [ ! -d "$LLVM_SRC" ]; then
    echo "  Cloning llvm-project..."
    git clone https://github.com/llvm/llvm-project.git "$LLVM_SRC"
else
    echo "  LLVM source already present, skipping clone."
fi

# --- Shared cmake flags ---
COMMON_CMAKE_FLAGS=(
    -G Ninja
    -DCMAKE_C_COMPILER=clang
    -DCMAKE_CXX_COMPILER=clang++
    -DLLVM_ENABLE_PROJECTS="clang"
    -DLLVM_TARGETS_TO_BUILD="X86"
    -DLLVM_USE_LINKER=lld
    -DLLVM_ENABLE_ASSERTIONS=ON
    -DLLVM_BUILD_EXAMPLES=OFF
    -DLLVM_BUILD_TOOLS=ON
    -DLLVM_BUILD_UTILS=ON
)

# --- LLVM build: sancov (for fuzzing) ---
echo "[3/5] Building LLVM (sancov)..."
LLVM_BUILD_SANCOV="$DEPS_DIR/llvm-build-sancov"

SANCOV_FLAGS="-g -O2 -fno-omit-frame-pointer -gline-tables-only -DFUZZING_BUILD_MODE_UNSAFE_FOR_PRODUCTION -fsanitize-coverage=inline-8bit-counters,pc-table,trace-cmp"

if [ ! -f "$LLVM_BUILD_SANCOV/bin/opt" ]; then
    echo "  Configuring LLVM (sancov)..."
    cmake -S "$LLVM_SRC/llvm" -B "$LLVM_BUILD_SANCOV" \
        "${COMMON_CMAKE_FLAGS[@]}" \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_C_FLAGS="$SANCOV_FLAGS" \
        -DCMAKE_CXX_FLAGS="$SANCOV_FLAGS" \
        -DLLVM_ENABLE_RTTI=ON

    echo "  Building opt target (to get all LLVM libraries)..."
    cmake --build "$LLVM_BUILD_SANCOV" --target opt -j"$JOBS"
else
    echo "  LLVM (sancov) already built, skipping."
fi

echo "  LLVM (sancov) ready: $LLVM_BUILD_SANCOV"
echo "  Libraries: $(find "$LLVM_BUILD_SANCOV/lib" -name 'libLLVM*.a' | wc -l) LLVM .a files"

# --- LLVM build: ASAN+UBSAN (for triage) ---
echo "  Building LLVM (asan)..."
LLVM_BUILD_ASAN="$DEPS_DIR/llvm-build-asan"

ASAN_FLAGS="-fsanitize=address,undefined -fno-omit-frame-pointer -g"

if [ ! -f "$LLVM_BUILD_ASAN/bin/opt" ]; then
    echo "  Configuring LLVM (asan)..."
    cmake -S "$LLVM_SRC/llvm" -B "$LLVM_BUILD_ASAN" \
        "${COMMON_CMAKE_FLAGS[@]}" \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_C_FLAGS="$ASAN_FLAGS" \
        -DCMAKE_CXX_FLAGS="$ASAN_FLAGS" \
        -DLLVM_USE_SANITIZER="Address;Undefined" \
        -DLLVM_ENABLE_RTTI=ON

    echo "  Building opt target (asan, this will take a while)..."
    cmake --build "$LLVM_BUILD_ASAN" --target opt -j"$JOBS"
else
    echo "  LLVM (asan) already built, skipping."
fi

echo "  LLVM (asan) ready: $LLVM_BUILD_ASAN"

# --- LLVM build: plain (for Alive2 + tools) ---
LLVM_BUILD_PLAIN="$DEPS_DIR/llvm-build-plain"

if [ ! -f "$LLVM_BUILD_PLAIN/bin/opt" ] || [ ! -f "$LLVM_BUILD_PLAIN/bin/llc" ] || [ ! -f "$LLVM_BUILD_PLAIN/bin/llvm-reduce" ]; then
    echo "  Configuring LLVM (plain)..."
    cmake -S "$LLVM_SRC/llvm" -B "$LLVM_BUILD_PLAIN" \
        "${COMMON_CMAKE_FLAGS[@]}" \
        -DLLVM_TARGETS_TO_BUILD="X86;AArch64;RISCV" \
        -DCMAKE_BUILD_TYPE=Release \
        -DLLVM_ENABLE_RTTI=ON

    echo "  Building LLVM (plain)..."
    cmake --build "$LLVM_BUILD_PLAIN" --target opt llc llvm-reduce llvm-symbolizer LLVMAArch64Disassembler LLVMRISCVDisassembler LLVMX86Disassembler -j"$JOBS"
else
    echo "  LLVM (plain) already built, skipping."
fi

# --- Alive2 (built against sancov LLVM) ---
echo "[4/5] Building Alive2..."
ALIVE2_SRC="$DEPS_DIR/alive2"
ALIVE2_BUILD="$DEPS_DIR/alive2/build"

if [ ! -d "$ALIVE2_SRC" ]; then
    echo "  Cloning alive2..."
    git clone "$ALIVE2_REPO" "$ALIVE2_SRC"
else
    echo "  Alive2 source already present, skipping clone."
    git -C "$ALIVE2_SRC" remote set-url origin "$ALIVE2_REPO"
fi

ALIVE2_OLD_HEAD="$(git -C "$ALIVE2_SRC" rev-parse HEAD)"
git -C "$ALIVE2_SRC" fetch origin
git -C "$ALIVE2_SRC" checkout "$ALIVE2_REF"
ALIVE2_NEW_HEAD="$(git -C "$ALIVE2_SRC" rev-parse HEAD)"
if [ "$ALIVE2_OLD_HEAD" != "$ALIVE2_NEW_HEAD" ]; then
    echo "  Alive2 ref changed ($ALIVE2_OLD_HEAD -> $ALIVE2_NEW_HEAD), forcing rebuild."
    rm -f "$ALIVE2_BUILD/alive-tv"
fi

if [ ! -f "$ALIVE2_BUILD/alive-tv" ]; then
    echo "  Configuring Alive2..."
    cmake -S "$ALIVE2_SRC" -B "$ALIVE2_BUILD" \
        -G Ninja \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_C_COMPILER=clang \
        -DCMAKE_CXX_COMPILER=clang++ \
        -DCMAKE_PREFIX_PATH="$LLVM_BUILD_PLAIN" \
        -DLLVM_DIR="$LLVM_BUILD_PLAIN/lib/cmake/llvm" \
        -DBUILD_TV=ON

    echo "  Building Alive2..."
    cmake --build "$ALIVE2_BUILD" -j"$JOBS"
else
    echo "  Alive2 already built, skipping."
fi

echo "  Alive2 ready: $ALIVE2_BUILD"

# --- Alive2 arm-tv (backend translation validation) ---
echo "[5/5] Building Alive2 arm-tv..."
ARM_TV_SRC="$DEPS_DIR/alive2-arm-tv"
ARM_TV_BUILD="$DEPS_DIR/alive2-arm-tv/build"

if [ ! -d "$ARM_TV_SRC" ]; then
    echo "  Cloning alive2 (arm-tv)..."
    git clone "$ARM_TV_REPO" "$ARM_TV_SRC"
else
    echo "  arm-tv source already present, skipping clone."
    git -C "$ARM_TV_SRC" remote set-url origin "$ARM_TV_REPO"
fi

ARM_TV_OLD_HEAD="$(git -C "$ARM_TV_SRC" rev-parse HEAD)"
git -C "$ARM_TV_SRC" fetch origin
git -C "$ARM_TV_SRC" checkout "$ARM_TV_REF"
ARM_TV_NEW_HEAD="$(git -C "$ARM_TV_SRC" rev-parse HEAD)"
if [ "$ARM_TV_OLD_HEAD" != "$ARM_TV_NEW_HEAD" ]; then
    echo "  arm-tv ref changed ($ARM_TV_OLD_HEAD -> $ARM_TV_NEW_HEAD), forcing rebuild."
    rm -f "$ARM_TV_BUILD/alive-tv"
fi

# Note, ASLP is broken-ish for LLVM Trunk. So we are disabling it, to use the hand-written lifters.
# sed -i 's|^\(\s*add_subdirectory(backend_tv/aslp)\)|#\1|' deps/alive2-arm-tv/CMakeLists.txt
# sed -i 's|^\(\s*target_link_libraries(backend_tv PUBLIC bridge)\)|#\1|' deps/alive2-arm-tv/CMakeLists.txt

if [ ! -f "$ARM_TV_BUILD/alive-tv" ]; then
    echo "  Configuring arm-tv..."
    cmake -S "$ARM_TV_SRC" -B "$ARM_TV_BUILD" \
        -G Ninja \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_C_COMPILER=clang \
        -DCMAKE_CXX_COMPILER=clang++ \
        -DCMAKE_PREFIX_PATH="$LLVM_BUILD_PLAIN" \
        -DLLVM_DIR="$LLVM_BUILD_PLAIN/lib/cmake/llvm" \
        -DBUILD_TV=ON

    echo "  Building arm-tv..."
    cmake --build "$ARM_TV_BUILD" -j"$JOBS"
else
    echo "  arm-tv already built, skipping."
fi

echo "  arm-tv ready: $ARM_TV_BUILD"

echo ""
echo "=== All dependencies built ==="
echo ""
echo "  LLVM (sancov): $LLVM_BUILD_SANCOV"
echo "  LLVM (asan):   $LLVM_BUILD_ASAN"
echo "  LLVM (plain):  $LLVM_BUILD_PLAIN"
echo "  Alive2:        $ALIVE2_BUILD"
echo "  Alive2 arm-tv: $ARM_TV_BUILD"