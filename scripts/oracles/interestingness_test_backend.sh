#!/usr/bin/env bash
# Interestingness test for llvm-reduce, BACKEND-TV edition.
#
# Exits 0 iff the given .ll file still triggers the SAME backend-tv verdict, in
# the SAME isel mode, that we are trying to preserve while minimizing. This is
# the codegen analogue of interestingness_test.sh (which targets opt -O2 +
# alive-tv); see scripts/oracles/backend_tv.sh for the matching oracle.
#
# Usage:  interestingness_test_backend.sh <input.ll>
#
# Pinned by env (the reduce wrapper sets these; defaults suit RISC-V hunting):
#   BTV_ARCH     riscv64 | aarch64                (default riscv64)
#   BTV_ISEL     dagisel | gisel                  (default dagisel)
#   BTV_VERDICT  required verdict substring       (default "Value mismatch")
#   BTV_SMT_TO   per-SMT-query budget, ms         (default 8000)
#   BTV_TIMEOUT  per-input wall clock, s          (default 40)
#   BTV_DISABLE_POISON  set=1 to drop poison inputs too (default: keep poison)
#
# Why pin isel + the exact verdict string, not just "any incorrect":
#   - Bugs are often one-selector-only (the ushl.sat SDAG bug verifies CORRECT
#     under GISel). Reducing on "fails in either mode" would let llvm-reduce
#     drift to an unrelated function.
#   - A real "Value mismatch" must NOT be allowed to collapse into the
#     "Source is more defined than target" lifter artifact, so we require the
#     exact verdict we started with. Set BTV_VERDICT to reduce an artifact too.

set -u

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "$SCRIPT_DIR/../build/env.sh" >/dev/null

input="$1"

ARCH="${BTV_ARCH:-riscv64}"
ISEL="${BTV_ISEL:-dagisel}"
VERDICT="${BTV_VERDICT:-Value mismatch}"
SMT_TO="${BTV_SMT_TO:-8000}"
WALL="${BTV_TIMEOUT:-40}"

FLAGS=(--backend="$ARCH" --smt-to="$SMT_TO" --disable-undef-input)
[[ -n "${BTV_DISABLE_POISON:-}" ]] && FLAGS+=(--disable-poison-input)
[[ "$ISEL" == gisel ]] && FLAGS+=(--global-isel)

out="$(timeout "$WALL" "$BACKEND_TV_BIN" "${FLAGS[@]}" "$input" 2>&1)"
rc=$?

# Killed by wall clock -> not a stable witness.
(( rc == 124 )) && exit 1

# Must still be a confirmed miscompilation: >=1 incorrect transformations ...
incorrect="$(grep -oE '[0-9]+ incorrect transformations' <<<"$out" | grep -oE '^[0-9]+' | tail -1)"
[[ "${incorrect:-0}" -ge 1 ]] || exit 1

# ... AND it must be the SAME verdict class we are preserving.
case "$out" in
    *"$VERDICT"*) exit 0 ;;
    *)            exit 1 ;;
esac
