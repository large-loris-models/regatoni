#!/usr/bin/env bash
# Reduce a miscompilation witness using llvm-reduce + Alive2 interestingness test.
#
# Usage: reduce_miscompilation.sh <input.ll> [output.ll]

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "$SCRIPT_DIR/../build/env.sh" >/dev/null

INTEREST_TEST="$SCRIPT_DIR/interestingness_test.sh"

input="$1"
output="${2:-${input%.ll}.reduced.ll}"

if [[ ! -f "$input" ]]; then
    echo "ERROR: input file not found: $input" >&2
    exit 1
fi

# Verify input is actually interesting before spending time reducing.
echo "Checking that input triggers a genuine miscompilation..."
if ! "$INTEREST_TEST" "$input"; then
    echo "ERROR: input does not trigger a genuine miscompilation: $input" >&2
    exit 1
fi

input_lines="$(wc -l < "$input")"
echo "Input: $input ($input_lines lines)"
echo "Reducing..."

"$LLVM_REDUCE" \
    --test="$INTEREST_TEST" \
    --abort-on-invalid-reduction \
    "$input" \
    -o "$output"

if [[ ! -f "$output" ]]; then
    echo "ERROR: llvm-reduce did not produce output" >&2
    exit 1
fi

output_lines="$(wc -l < "$output")"
echo "Reduced: $output ($output_lines lines, was $input_lines)"
echo "Reduction: $input_lines -> $output_lines lines ($(( (input_lines - output_lines) * 100 / input_lines ))% smaller)"
