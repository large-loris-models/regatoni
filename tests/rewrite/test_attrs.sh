#!/usr/bin/env bash
# Exercises add_param_attr, add_fn_attr, add_ret_attr.
source "$(dirname "$0")/lib.sh"
F="$REPO_ROOT/tests/fixtures"

echo "[test_attrs]"

# 1. add noundef to integer param → output has "i32 noundef %x"
spec="$(write_spec p_noundef '{"rewrites":[{"id":"pn","category":"attribute",
  "match":{"target":"parameter","type_class":"integer","attr_absent":"noundef"},
  "transform":{"action":"add_param_attr","attr":"noundef"}}]}')"
out="$TMP/out_p_noundef"
run_rewrite "$spec" "$F/basic_add.ll" "$out"
assert_match "$out" 'i32 noundef %x' "param noundef on i32 %x" >/dev/null

# 2. add nonnull to pointer param → output has "ptr nonnull %p"
spec="$(write_spec p_nonnull '{"rewrites":[{"id":"pnn","category":"attribute",
  "match":{"target":"parameter","type_class":"pointer","attr_absent":"nonnull"},
  "transform":{"action":"add_param_attr","attr":"nonnull"}}]}')"
out="$TMP/out_p_nonnull"
run_rewrite "$spec" "$F/ptr_param.ll" "$out"
assert_match "$out" 'ptr nonnull %p' "param nonnull on ptr %p" >/dev/null

# 3. add willreturn to function → output has "willreturn"
spec="$(write_spec fn_wr '{"rewrites":[{"id":"wr","category":"attribute",
  "match":{"target":"function","attr_absent":"willreturn"},
  "transform":{"action":"add_fn_attr","attr":"willreturn"}}]}')"
out="$TMP/out_fn_wr"
run_rewrite "$spec" "$F/basic_add.ll" "$out"
assert_match "$out" 'willreturn' "function willreturn" >/dev/null

# 4. add dereferenceable(8) to pointer param → "ptr dereferenceable(8) %p"
spec="$(write_spec p_deref '{"rewrites":[{"id":"pd","category":"attribute",
  "match":{"target":"parameter","type_class":"pointer","attr_absent":"dereferenceable"},
  "transform":{"action":"add_param_attr","attr":"dereferenceable","value":8}}]}')"
out="$TMP/out_p_deref"
run_rewrite "$spec" "$F/ptr_param.ll" "$out"
assert_match "$out" 'ptr dereferenceable\(8\) %p' "param dereferenceable(8)" >/dev/null

# All outputs survive opt -O2.
for dir in "$TMP"/out_p_noundef "$TMP"/out_p_nonnull "$TMP"/out_fn_wr "$TMP"/out_p_deref; do
  label="$(basename "$dir") survives opt -O2"
  assert_opt_clean "$dir" "$label"
done

summarize
