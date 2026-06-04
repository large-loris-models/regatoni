#!/usr/bin/env bash
# Runs every test_*.sh under tests/inject/, prints PASS/FAIL summary,
# exits nonzero if any test script failed.
set -u
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

tests=(
  "$SCRIPT_DIR/test_basic_inject.sh"
  "$SCRIPT_DIR/test_intrinsic_inject.sh"
  "$SCRIPT_DIR/test_type_compat.sh"
  "$SCRIPT_DIR/test_dominance.sh"
)

passed=0
failed=0
for t in "${tests[@]}"; do
  echo
  echo "============================================"
  echo "  $(basename "$t")"
  echo "============================================"
  if bash "$t"; then
    passed=$((passed+1))
  else
    failed=$((failed+1))
  fi
done

echo
echo "============================================"
echo "  Test suite: $passed pass, $failed fail"
echo "============================================"
[[ $failed -eq 0 ]]
