#!/usr/bin/env bash
# run_tests.sh — Compile and run mutation tests
source "$(dirname "$0")/../build/env.sh"
check_prereqs

echo "=== Building mutation tests ==="

if [[ ! -d "$LLVM_BUILD" ]]; then
    echo "ERROR: LLVM build not found at $LLVM_BUILD" >&2
    exit 1
fi

mkdir -p "$BUILD_OUT"

# Include paths from the build tree
INCLUDE_FLAGS=(
    -I"$PROJECT_ROOT"
    -I"$LLVM_SRC/llvm/include"
    -I"$LLVM_BUILD/include"
)

# Collect LLVM libraries we need
LLVM_LIBS=(
    "$LLVM_BUILD/lib/libLLVMIRReader.a"
    "$LLVM_BUILD/lib/libLLVMAsmParser.a"
    "$LLVM_BUILD/lib/libLLVMTransformUtils.a"
    "$LLVM_BUILD/lib/libLLVMAnalysis.a"
    "$LLVM_BUILD/lib/libLLVMProfileData.a"
    "$LLVM_BUILD/lib/libLLVMObject.a"
    "$LLVM_BUILD/lib/libLLVMMCParser.a"
    "$LLVM_BUILD/lib/libLLVMMC.a"
    "$LLVM_BUILD/lib/libLLVMDebugInfoCodeView.a"
    "$LLVM_BUILD/lib/libLLVMTextAPI.a"
    "$LLVM_BUILD/lib/libLLVMSymbolize.a"
    "$LLVM_BUILD/lib/libLLVMDebugInfoDWARF.a"
    "$LLVM_BUILD/lib/libLLVMDebugInfoDWARFLowLevel.a"
    "$LLVM_BUILD/lib/libLLVMDebugInfoPDB.a"
    "$LLVM_BUILD/lib/libLLVMDebugInfoMSF.a"
    "$LLVM_BUILD/lib/libLLVMDebugInfoBTF.a"
    "$LLVM_BUILD/lib/libLLVMBitWriter.a"
    "$LLVM_BUILD/lib/libLLVMBitReader.a"
    "$LLVM_BUILD/lib/libLLVMCore.a"
    "$LLVM_BUILD/lib/libLLVMRemarks.a"
    "$LLVM_BUILD/lib/libLLVMBitstreamReader.a"
    "$LLVM_BUILD/lib/libLLVMBinaryFormat.a"
    "$LLVM_BUILD/lib/libLLVMSupport.a"
    "$LLVM_BUILD/lib/libLLVMDemangle.a"
    "$LLVM_BUILD/lib/libLLVMTargetParser.a"
)

SRCS=(
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
    "$PROJECT_ROOT/src/mutators/registry.cc"
    "$PROJECT_ROOT/tests/test_mutations.cc"
)

$CXX -std=c++17 -O2 \
    "${INCLUDE_FLAGS[@]}" \
    -fno-rtti \
    $SANCOV_FLAGS \
    "${SRCS[@]}" \
    -Wl,--start-group \
    "${LLVM_LIBS[@]}" \
    -Wl,--end-group \
    -ldl -lpthread -lm -lz -ltinfo \
    -o "$BUILD_OUT/test_mutations"

echo "=== Running tests ==="
"$BUILD_OUT/test_mutations"