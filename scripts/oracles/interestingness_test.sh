#!/usr/bin/env bash
# Interestingness test for llvm-reduce. Exits 0 if the given .ll file
# triggers a genuine Alive2-confirmed miscompilation under opt -O2.
#
# Usage: interestingness_test.sh <input.ll>

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "$SCRIPT_DIR/../build/env.sh" >/dev/null

OPT_PLAIN="$LLVM_BUILD_PLAIN/bin/opt"
TMP_OPT="/tmp/alive_reduce_opt_$$.ll"

cleanup() { rm -f "$TMP_OPT"; }
trap cleanup EXIT

input="$1"

# 1. Optimize the input.
"$OPT_PLAIN" -O2 -S "$input" -o "$TMP_OPT" 2>/dev/null || exit 1

# 2. Run Alive2 translation validation.
output="$(timeout 120 "$ALIVE_TV" --smt-to=5000 "$input" "$TMP_OPT" 2>&1)" || true

# 3. Must contain the top-level failure verdict.
case "$output" in
    *"Transformation doesn't verify"*) ;;
    *) exit 1 ;;
esac

# 4. Reject known false positives (consistent with opt_fuzz_target_alive2.cc).
#    - "did not return"                  : recursion, Alive2 can't model
#    - "timeout" (case-insensitive)      : Z3 SMT solver timeout
#    - "initializes("                    : initializes() attr not handled
#    - "Alive2 approximated the semantics" : approximations, inconclusive
#    - "doesn't type check"             : Alive2 type-checker limitation
case "$output" in
    *"did not return"*)                    exit 1 ;;
    *"initializes("*)                      exit 1 ;;
    *"Alive2 approximated the semantics"*) exit 1 ;;
    *"doesn't type check"*)                exit 1 ;;
esac
# Case-insensitive timeout check.
if echo "$output" | grep -qi "timeout"; then
    exit 1
fi

# 5. Must contain at least one definitive unsound error (is_unsound=true in
#    Alive2's Errors::add). These are the only strings that indicate a real bug.
case "$output" in
    *"Source is more defined than target"*)                  exit 0 ;;
    *"Target is more poisonous than source"*)                exit 0 ;;
    *"Target's return value is more undefined"*)             exit 0 ;;
    *"Value mismatch"*)                                      exit 0 ;;
    *"Mismatch in memory"*)                                  exit 0 ;;
    *"Source and target don't have the same return domain"*) exit 0 ;;
    *"Function attributes not refined"*)                     exit 0 ;;
    *"Parameter attributes not refined"*)                    exit 0 ;;
esac

# No definitive unsound error found.
exit 1
