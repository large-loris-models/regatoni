#!/usr/bin/env bash
# Reduce a backend-tv miscompilation witness with llvm-reduce, preserving the
# original verdict + isel mode. Codegen analogue of reduce_miscompilation.sh.
#
# Usage:
#   reduce_backend_miscompile.sh <input.ll> [output.ll]
#
# Options (env or flag):
#   --isel dagisel|gisel|auto   (default auto: probe which selector miscompiles)
#   --arch riscv64|aarch64      (default riscv64)
#   --verdict "<substr>"        (default auto: prefer "Value mismatch", else
#                                "Source is more defined than target")
#
# Auto-probe pins exactly ONE (isel, verdict) pair so the reduction can't drift
# to the other selector or degrade a real miscompile into the lifter artifact.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "$SCRIPT_DIR/../build/env.sh" >/dev/null

INTEREST="$SCRIPT_DIR/interestingness_test_backend.sh"
ARCH="riscv64"; ISEL="auto"; VERDICT="auto"
# Tighter per-call wall than the campaign oracle (40s): llvm-reduce makes hundreds
# of calls, and a real defined-input Value-mismatch returns in <1s. Intermediate
# candidates that go quantified hit the wall and are (correctly) rejected — a short
# wall just rejects them ~3x faster. Raise BTV_TIMEOUT only for poison-kept bugs.
SMT_TO="${BTV_SMT_TO:-6000}"; WALL="${BTV_TIMEOUT:-15}"
POS=()
while (( $# )); do
  case "$1" in
    --isel)    ISEL="$2"; shift 2 ;;
    --arch)    ARCH="$2"; shift 2 ;;
    --verdict) VERDICT="$2"; shift 2 ;;
    *)         POS+=("$1"); shift ;;
  esac
done
input="${POS[0]:?usage: reduce_backend_miscompile.sh <input.ll> [output.ll]}"
output="${POS[1]:-${input%.ll}.reduced.ll}"
[[ -f "$input" ]] || { echo "ERROR: input not found: $input" >&2; exit 1; }

# Run backend-tv once in a given (isel, poison) mode; echo the matched verdict.
_probe() {  # $1=dagisel|gisel  $2=poison|nopoison  -> prints verdict substr or nothing
  local isel="$1" pois="$2" flags out
  flags=(--backend="$ARCH" --smt-to="$SMT_TO" --disable-undef-input)
  [[ "$pois" == nopoison ]] && flags+=(--disable-poison-input)
  [[ "$isel" == gisel ]] && flags+=(--global-isel)
  out="$(timeout "$WALL" "$BACKEND_TV_BIN" "${flags[@]}" "$input" 2>&1)" || true
  grep -qE '[1-9][0-9]* incorrect transformations' <<<"$out" || return 0
  if grep -q 'Value mismatch' <<<"$out"; then echo "Value mismatch"
  elif grep -q 'Source is more defined than target' <<<"$out"; then echo "Source is more defined than target"
  elif grep -q 'Target is more poisonous than source' <<<"$out"; then echo "Target is more poisonous than source"; fi
}

# Auto-detect (isel, poison, verdict). Search order matters:
#  - isel: dagisel first (SDAG bugs are higher value), then gisel.
#  - poison: nopoison FIRST. Defined-input witnesses are both the strongest
#    (no poison-semantics caveat) and MUCH faster — the ushl.sat bug returns a
#    clean "Value mismatch" instantly with poison off but TIMES OUT with it on.
#    llvm-reduce makes hundreds of calls, so tractability is decisive. Fall back
#    to poison-kept only for bugs whose witness genuinely needs a poison input.
POISON_MODE=""   # nopoison | poison
if [[ "$ISEL" == auto || "$VERDICT" == auto ]]; then
  echo "Probing (selector x poison) for a stable miscompilation..."
  isel_list=(dagisel gisel); [[ "$ISEL" != auto ]] && isel_list=("$ISEL")
  for cand in "${isel_list[@]}"; do
    for pm in nopoison poison; do
      v="$(_probe "$cand" "$pm")"
      if [[ -n "$v" ]]; then ISEL="$cand"; POISON_MODE="$pm"; [[ "$VERDICT" == auto ]] && VERDICT="$v"; break 2; fi
    done
  done
  [[ -n "$POISON_MODE" ]] || { echo "ERROR: no stable miscompilation found on $input (tried dag/gisel x poison/nopoison)" >&2; exit 1; }
else
  POISON_MODE="${BTV_DISABLE_POISON:+nopoison}"; POISON_MODE="${POISON_MODE:-poison}"
fi

echo "Pinned: arch=$ARCH isel=$ISEL poison=$POISON_MODE verdict=\"$VERDICT\""
export BTV_ARCH="$ARCH" BTV_ISEL="$ISEL" BTV_VERDICT="$VERDICT" BTV_SMT_TO="$SMT_TO" BTV_TIMEOUT="$WALL"
if [[ "$POISON_MODE" == nopoison ]]; then export BTV_DISABLE_POISON=1; else unset BTV_DISABLE_POISON || true; fi

# Sanity: the input itself must be interesting under the pinned settings.
"$INTEREST" "$input" || { echo "ERROR: input not interesting under pinned settings" >&2; exit 1; }

in_lines="$(wc -l < "$input")"
echo "Input: $input ($in_lines lines) — reducing..."
"$LLVM_REDUCE" --test="$INTEREST" --abort-on-invalid-reduction "$input" -o "$output"
[[ -f "$output" ]] || { echo "ERROR: llvm-reduce produced no output" >&2; exit 1; }

out_lines="$(wc -l < "$output")"
echo "Reduced: $output ($out_lines lines, was $in_lines)"
echo "===== reduced witness ====="
cat "$output"
