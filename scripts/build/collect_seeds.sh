#!/usr/bin/env bash

source "$(dirname "$0")/env.sh"

echo "=== Collecting seed .ll files ==="

if [[ ! -d "$LLVM_SRC/llvm/test/Transforms" ]]; then
    echo "ERROR: LLVM source not found at $LLVM_SRC" >&2
    exit 1
fi

mkdir -p "$SEEDS_DIR"

# Grab small .ll files (<10KB) from key opt pass test directories
for dir in InstCombine SROA GVN SCCP SimplifyCFG InferAddressSpaces \
           LoopUnroll IndVarSimplify MemCpyOpt DeadStoreElimination \
           Reassociate LoopVectorize SLPVectorizer; do
    SRC="$LLVM_SRC/llvm/test/Transforms/$dir"
    [[ -d "$SRC" ]] || continue
    find "$SRC" -name '*.ll' -size -10k -exec cp {} "$SEEDS_DIR/" \;
done

TOTAL=$(find "$SEEDS_DIR" -name '*.ll' | wc -l)
echo "[seeds] ✓ Collected $TOTAL .ll files into $SEEDS_DIR/"