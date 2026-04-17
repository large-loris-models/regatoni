#!/usr/bin/env bash

source "$(dirname "$0")/env.sh"

echo "=== Collecting seed .ll files ==="

if [[ ! -d "$LLVM_SRC/llvm/test/Transforms" ]]; then
    echo "ERROR: LLVM source not found at $LLVM_SRC" >&2
    exit 1
fi

mkdir -p "$SEEDS_DIR"


find "$LLVM_SRC/llvm/test/Transforms" -name '*.ll' -print0 | while IFS= read -r -d '' f; do
    dir=$(basename "$(dirname "$f")")
    base=$(basename "$f")
    cp "$f" "$SEEDS_DIR/${dir}__${base}"
done

TOTAL=$(find "$SEEDS_DIR" -name '*.ll' | wc -l)
echo "[seeds] Collected $TOTAL .ll files into $SEEDS_DIR/"