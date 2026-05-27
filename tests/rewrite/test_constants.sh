#!/usr/bin/env bash
# Exercises replace_with_constant.
source "$(dirname "$0")/lib.sh"
F="$REPO_ROOT/tests/fixtures"

echo "[test_constants]"

# Replace shl operand 1 with bitwidth_minus_1 → "shl i32 %x, 31"
spec="$(write_spec shl_bw '{"rewrites":[{"id":"shl_bw","category":"constant",
  "match":{"target":"instruction","opcode":["shl"],"operand_index":1,"operand_is_param":true},
  "transform":{"action":"replace_with_constant","operand":1,"scheme":"bitwidth_minus_1"}}]}')"
out="$TMP/out_shl_bw"
run_rewrite "$spec" "$F/basic_shl.ll" "$out"
assert_match "$out" 'shl i32 %x, 31' "shl shamt → bitwidth_minus_1" >/dev/null

# Replace sdiv divisor with 1 → "sdiv i32 %x, 1"
spec="$(write_spec sdiv_one '{"rewrites":[{"id":"sdiv_one","category":"constant",
  "match":{"target":"instruction","opcode":["sdiv"],"operand_index":1,"operand_is_param":true},
  "transform":{"action":"replace_with_constant","operand":1,"scheme":"one"}}]}')"
out="$TMP/out_sdiv_one"
run_rewrite "$spec" "$F/basic_sdiv.ll" "$out"
assert_match "$out" 'sdiv i32 %x, 1' "sdiv divisor → one" >/dev/null

# Replace add rhs with smin → "add i32 %x, -2147483648"
spec="$(write_spec add_smin '{"rewrites":[{"id":"add_smin","category":"constant",
  "match":{"target":"instruction","opcode":["add"],"operand_index":1,"operand_is_param":true},
  "transform":{"action":"replace_with_constant","operand":1,"scheme":"smin"}}]}')"
out="$TMP/out_add_smin"
run_rewrite "$spec" "$F/basic_add.ll" "$out"
assert_match "$out" 'add i32 %x, -2147483648' "add rhs → smin" >/dev/null

for dir in "$TMP"/out_shl_bw "$TMP"/out_sdiv_one "$TMP"/out_add_smin; do
  assert_opt_clean "$dir" "$(basename "$dir") survives opt -O2"
done

summarize
