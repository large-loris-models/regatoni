#!/usr/bin/env bash

source "$(dirname "$0")/env.sh"

echo "=== Collecting seed .ll files ==="

if [[ ! -d "$LLVM_SRC/llvm/test/Transforms" ]]; then
    echo "ERROR: LLVM source not found at $LLVM_SRC" >&2
    exit 1
fi

mkdir -p "$SEEDS_DIR"


# Restrict seeds to intraprocedural pass test directories. Alive2 can soundly
# detect miscompilations for these single-function transforms; interprocedural
# passes (inlining, IPO, etc.) are excluded because Alive2 verifies one
# function at a time.
INTRAPROCEDURAL_DIRS=(
    InstCombine LoopVectorize SLPVectorizer SimplifyCFG InstSimplify
    IndVarSimplify LoopUnroll GVN LICM ConstraintElimination
    VectorCombine JumpThreading SimpleLoopUnswitch Reassociate LoopIdiom
    MemCpyOpt SROA CorrelatedValuePropagation EarlyCSE
    AggressiveInstCombine LoopDeletion ADCE LoopDistribute MergeICmps
    LoopSimplifyCFG SpeculativeExecution Float2Int AlignmentFromAssumptions
    ExpandMemCmp LoopLoadElimination LibCallsShrinkWrap
)

for dir in "${INTRAPROCEDURAL_DIRS[@]}"; do
    test_dir="$LLVM_SRC/llvm/test/Transforms/$dir"
    if [[ ! -d "$test_dir" ]]; then
        echo "[seeds] skipping missing directory: $test_dir" >&2
        continue
    fi
    find "$test_dir" -name '*.ll' -size -100k -print0 | while IFS= read -r -d '' f; do
        base=$(basename "$f")
        cp "$f" "$SEEDS_DIR/${dir}__${base}"
    done
done

TOTAL=$(find "$SEEDS_DIR" -name '*.ll' | wc -l)
echo "[seeds] Collected $TOTAL .ll files into $SEEDS_DIR/"