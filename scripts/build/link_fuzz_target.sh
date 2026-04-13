#!/usr/bin/env bash

source "$(dirname "$0")/env.sh"
check_prereqs

echo "=== Linking fuzz targets ==="

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

if [[ ! -f "$LLVM_BUILD_ASAN/build.ninja" ]]; then
    echo "ERROR: LLVM asan build not found at $LLVM_BUILD_ASAN" >&2
    exit 1
fi

mkdir -p "$BUILD_OUT"

# Mutation framework sources (shared with tests)
MUTATOR_SRCS=(
    "$PROJECT_ROOT/src/mutators/registry.cc"
    "$PROJECT_ROOT/src/mutators/ir_mutations/swap_binop.cc"
    "$PROJECT_ROOT/src/mutators/ir_mutations/swap_cmp_predicate.cc"
    "$PROJECT_ROOT/src/mutators/ir_mutations/modify_flags.cc"
    "$PROJECT_ROOT/src/mutators/ir_mutations/swap_operands.cc"
    "$PROJECT_ROOT/src/mutators/ir_mutations/replace_operand.cc"
    "$PROJECT_ROOT/src/mutators/ir_mutations/shuffle_instructions.cc"
    "$PROJECT_ROOT/src/mutators/ir_mutations/move_instruction.cc"
    "$PROJECT_ROOT/src/mutators/ir_mutations/inline_call.cc"
    "$PROJECT_ROOT/src/mutators/ir_mutations/remove_void_call.cc"
    "$PROJECT_ROOT/src/mutators/ir_mutations/modify_attributes.cc"
    "$PROJECT_ROOT/src/mutators/ir_mutations/mutate_gep.cc"
    "$PROJECT_ROOT/src/mutators/ir_mutations/resize_type.cc"
    "$PROJECT_ROOT/src/mutators/ir_mutations/mutate_unary.cc"
    "$PROJECT_ROOT/src/mutators/ir_mutations/eliminate_undef.cc"
)

########################################
# Main binary: sancov + Centipede runner
########################################

echo "[link] Extracting LLVM sancov link libraries..."
mapfile -t LINK_LIBS < <(
    cd "$LLVM_BUILD" &&
    ninja -t commands bin/opt 2>/dev/null |
    grep -E '(clang\+\+|ld\.lld).*bin/opt' |
    grep -oP 'lib/\S+\.a' |
    sort -u |
    while read -r lib; do echo "$LLVM_BUILD/$lib"; done
)
echo "[link] Found ${#LINK_LIBS[@]} sancov LLVM libraries"

MAIN_INCLUDES=(
    -I"$PROJECT_ROOT"
    -I"$LLVM_BUILD/include"
    -I"$LLVM_SRC/llvm/include"
)

HARNESS_SRC="$PROJECT_ROOT/src/harness/opt_fuzz_target.cc"
FUZZ_TARGET="$BUILD_OUT/opt_fuzz_target"

echo "[link] Compiling + linking opt_fuzz_target (sancov)..."
$CXX "${FUZZ_CFLAGS[@]}" -std=c++17 -fno-rtti \
    "${MAIN_INCLUDES[@]}" \
    "$HARNESS_SRC" \
    "${MUTATOR_SRCS[@]}" \
    -fuse-ld=lld \
    -Wl,--start-group \
    "${LINK_LIBS[@]}" \
    "$CENTIPEDE_RUNNER" \
    -Wl,--end-group \
    -ldl -lrt -lpthread -lm -lz -ltinfo \
    -o "$FUZZ_TARGET"

echo "[link] ✓ $FUZZ_TARGET ($(du -h "$FUZZ_TARGET" | cut -f1))"

########################################
# Alive2 binary: ASAN+UBSAN, no sancov, no Centipede runner
########################################

echo "[link] Extracting LLVM asan link libraries..."
mapfile -t ASAN_LINK_LIBS < <(
    cd "$LLVM_BUILD_ASAN" &&
    ninja -t commands bin/opt 2>/dev/null |
    grep -E '(clang\+\+|ld\.lld).*bin/opt' |
    grep -oP 'lib/\S+\.a' |
    sort -u |
    while read -r lib; do echo "$LLVM_BUILD_ASAN/$lib"; done
)
echo "[link] Found ${#ASAN_LINK_LIBS[@]} asan LLVM libraries"

# Alive2 llvm_util sources — not packaged as a static library in this build,
# so compile them directly into the binary.
ALIVE2_LLVM_UTIL_SRCS=(
    "$ALIVE2_SRC/llvm_util/compare.cpp"
    "$ALIVE2_SRC/llvm_util/llvm2alive.cpp"
    "$ALIVE2_SRC/llvm_util/llvm_optimizer.cpp"
    "$ALIVE2_SRC/llvm_util/known_fns.cpp"
    "$ALIVE2_SRC/llvm_util/utils.cpp"
)

ALIVE2_LIBS=(
    "$ALIVE2_BUILD/libtools.a"
    "$ALIVE2_BUILD/libir.a"
    "$ALIVE2_BUILD/libsmt.a"
    "$ALIVE2_BUILD/libutil.a"
)

ALIVE_INCLUDES=(
    -I"$PROJECT_ROOT"
    -I"$ALIVE2_SRC"
    -I"$ALIVE2_BUILD"
    -I"$LLVM_BUILD_ASAN/include"
    -I"$LLVM_SRC/llvm/include"
)

ALIVE_CFLAGS=(
    "-g"
    "-O1"
    "-fno-omit-frame-pointer"
    "-fsanitize=address,undefined"
    "-fno-sanitize=vptr"
    "-DFUZZING_BUILD_MODE_UNSAFE_FOR_PRODUCTION"
)

ALIVE_HARNESS_SRC="$PROJECT_ROOT/src/harness/opt_fuzz_target_alive2.cc"
ALIVE_FUZZ_TARGET="$BUILD_OUT/opt_fuzz_target_alive2"

# Try to locate Z3 (Alive2 link dependency)
Z3_LIB="$(pkg-config --libs z3 2>/dev/null || echo -lz3)"

echo "[link] Compiling + linking opt_fuzz_target_alive2 (ASAN+UBSAN)..."
$CXX "${ALIVE_CFLAGS[@]}" -std=c++20 -frtti \
    "${ALIVE_INCLUDES[@]}" \
    "$ALIVE_HARNESS_SRC" \
    "${ALIVE2_LLVM_UTIL_SRCS[@]}" \
    "${MUTATOR_SRCS[@]}" \
    -fuse-ld=lld \
    -Wl,--start-group \
    "${ALIVE2_LIBS[@]}" \
    "${ASAN_LINK_LIBS[@]}" \
    -Wl,--end-group \
    $Z3_LIB \
    -ldl -lrt -lpthread -lm -lz -ltinfo \
    -o "$ALIVE_FUZZ_TARGET"

echo "[link] ✓ $ALIVE_FUZZ_TARGET ($(du -h "$ALIVE_FUZZ_TARGET" | cut -f1))"
