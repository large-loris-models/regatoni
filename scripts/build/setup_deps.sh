#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
DEPS_DIR="$PROJECT_ROOT/deps"

JOBS="${JOBS:-$(nproc)}"

mkdir -p "$DEPS_DIR"

echo "=== Regatoni: Setting up dependencies ==="
echo "  Parallel jobs: $JOBS"
echo "  Deps dir:      $DEPS_DIR"
echo ""

# --- Submodules ---
echo "[1/3] Initializing submodules..."
cd "$PROJECT_ROOT"
git submodule update --init --recursive

# --- LLVM ---
echo "[2/3] Building LLVM from source (main)..."
LLVM_SRC="$DEPS_DIR/llvm-project"
LLVM_BUILD="$DEPS_DIR/llvm-build"
LLVM_INSTALL="$DEPS_DIR/llvm-install"

if [ ! -d "$LLVM_SRC" ]; then
    echo "  Cloning llvm-project..."
    git clone https://github.com/llvm/llvm-project.git "$LLVM_SRC"
else
    echo "  LLVM source already present at $LLVM_SRC, skipping clone."
fi

if [ ! -f "$LLVM_INSTALL/bin/llvm-config" ]; then
    echo "  Configuring LLVM..."
    cmake -S "$LLVM_SRC/llvm" -B "$LLVM_BUILD" \
        -G Ninja \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_INSTALL_PREFIX="$LLVM_INSTALL" \
        -DCMAKE_C_COMPILER=clang \
        -DCMAKE_CXX_COMPILER=clang++ \
        -DLLVM_ENABLE_PROJECTS="clang" \
        -DLLVM_TARGETS_TO_BUILD="X86" \
        -DLLVM_USE_LINKER=lld \
        -DLLVM_ENABLE_ASSERTIONS=ON \
        -DLLVM_OPTIMIZED_TABLEGEN=ON

    echo "  Building LLVM (this will take a while)..."
    cmake --build "$LLVM_BUILD" -j"$JOBS"

    echo "  Installing LLVM to $LLVM_INSTALL..."
    cmake --install "$LLVM_BUILD"
else
    echo "  LLVM already built at $LLVM_INSTALL, skipping."
fi

echo "  LLVM ready: $LLVM_INSTALL"

# --- Alive2 ---
echo "[3/3] Building Alive2..."
ALIVE2_SRC="$DEPS_DIR/alive2"
ALIVE2_BUILD="$DEPS_DIR/alive2/build"

if [ ! -d "$ALIVE2_SRC" ]; then
    echo "  Cloning alive2..."
    git clone https://github.com/AliveToolkit/alive2.git "$ALIVE2_SRC"
else
    echo "  Alive2 source already present, skipping clone."
fi

if [ ! -f "$ALIVE2_BUILD/alive-tv" ]; then
    echo "  Configuring Alive2..."
    cmake -S "$ALIVE2_SRC" -B "$ALIVE2_BUILD" \
        -G Ninja \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_C_COMPILER=clang \
        -DCMAKE_CXX_COMPILER=clang++ \
        -DCMAKE_PREFIX_PATH="$LLVM_INSTALL" \
        -DLLVM_DIR="$LLVM_INSTALL/lib/cmake/llvm"

    echo "  Building Alive2..."
    cmake --build "$ALIVE2_BUILD" -j"$JOBS"
else
    echo "  Alive2 already built, skipping."
fi

echo "  Alive2 ready: $ALIVE2_BUILD"

echo ""
echo "=== All dependencies built ==="
echo ""
