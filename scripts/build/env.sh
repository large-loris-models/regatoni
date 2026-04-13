#!/usr/bin/env bash

set -euo pipefail

PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
export PROJECT_ROOT

export PATH="$HOME/.local/bin:$PATH"

# Toolchain
export CC="${CC:-clang}"
export CXX="${CXX:-clang++}"

# LLVM source
export LLVM_SRC="$PROJECT_ROOT/deps/llvm-project"

# LLVM sancov build (for fuzzing) — no install, link against build dir
export LLVM_BUILD="$PROJECT_ROOT/deps/llvm-build-sancov"
export OPT="$LLVM_BUILD/bin/opt"

# LLVM asan build (for triage)
export LLVM_BUILD_ASAN="$PROJECT_ROOT/deps/llvm-build-asan"

# Alive2
export ALIVE2_SRC="$PROJECT_ROOT/deps/alive2"
export ALIVE2_BUILD="$PROJECT_ROOT/deps/alive2/build"
export ALIVE_TV="$ALIVE2_BUILD/alive-tv"

# FuzzTest / Centipede
export FUZZTEST_SRC="$PROJECT_ROOT/third_party/fuzztest"
export FUZZTEST_BIN="$FUZZTEST_SRC/bazel-bin/centipede"
export CENTIPEDE_BIN="$FUZZTEST_BIN/centipede"
export CENTIPEDE_RUNNER="$FUZZTEST_BIN/libcentipede_runner_static.a"

# Instrumentation
export SANCOV_FLAGS="-fsanitize-coverage=inline-8bit-counters,pc-table,trace-cmp"

export FUZZ_CFLAGS=(
    "-g"
    "-O2"
    "-fno-omit-frame-pointer"
    "-gline-tables-only"
    "-DFUZZING_BUILD_MODE_UNSAFE_FOR_PRODUCTION"
    "$SANCOV_FLAGS"
)

# Corpus / seeds
export CORPUS_DIR="$PROJECT_ROOT/corpus"
export SEEDS_DIR="$PROJECT_ROOT/seeds"

# Build output
export BUILD_OUT="$PROJECT_ROOT/build"

# Parallelism
export NUM_JOBS="${NUM_JOBS:-$(nproc)}"

check_prereqs() {
    for tool in "$CC" "$CXX" cmake ninja; do
        if ! command -v "$tool" &>/dev/null; then
            echo "ERROR: $tool not found" >&2
            return 1
        fi
    done
    echo "[env] PROJECT_ROOT=$PROJECT_ROOT"
}