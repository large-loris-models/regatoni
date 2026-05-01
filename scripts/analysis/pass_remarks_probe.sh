#!/usr/bin/env bash
# pass_remarks_probe.sh — capture LLVM optimization remarks for a miscompilation.
#
# Read-only experiment used by docs/investigations/2026-04-30_pass_remarks.md.
# Runs `opt` with all three remark categories enabled (passed/missed/analysis)
# at a configurable -opt-bisect-limit and dumps the YAML to stdout, plus a
# one-line summary of the distinct (Pass, Name) pairs the YAML contains.
#
# usage:
#   pass_remarks_probe.sh <ir_file> [bisect_limit]
#
# bisect_limit defaults to 1000000 (full pipeline). Pass the guilty index
# from bisect_blame.sh to capture remarks at that stopping point; pair with
# the limit-(N-1) run if you want to isolate the guilty invocation.

set -u

REPO_ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
OPT="${REPO_ROOT}/deps/llvm-build-plain/bin/opt"
PIPELINE='default<O2>'
LIMIT="${2:-1000000}"

if [[ $# -lt 1 || $# -gt 2 ]]; then
    echo "usage: $0 <ir_file> [bisect_limit]" >&2
    exit 2
fi
INPUT="$1"

WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT
YAML="$WORK/remarks.yaml"

"$OPT" \
    -opt-bisect-limit="$LIMIT" \
    -passes="$PIPELINE" \
    -pass-remarks-output="$YAML" \
    --pass-remarks='.*' \
    --pass-remarks-missed='.*' \
    --pass-remarks-analysis='.*' \
    "$INPUT" -S -o /dev/null 2>"$WORK/stderr"

bytes=$(wc -c <"$YAML")
blocks=$(grep -c '^---' "$YAML" 2>/dev/null || echo 0)
pairs=$(awk '/^Pass:/{p=$2;next}/^Name:/{print p"/"$2}' "$YAML" | sort -u | tr '\n' ',' | sed 's/,$//')

printf '# input=%s limit=%s bytes=%d blocks=%d distinct=%s\n' \
    "$INPUT" "$LIMIT" "$bytes" "$blocks" "$pairs" >&2

cat "$YAML"
