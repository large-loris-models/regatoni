#!/usr/bin/env python3
"""Coverage analysis: extracted rules vs. (a) rewrites.json, (b) fuzzer mutators.

Reads docs/extraction_*.json, rewrites.json, and the in-tree mutator source
files (statically modeled in MUTATOR_REACH below), then for each extracted
rule reports:
  - covered_by_mutations: does any rewrite in rewrites.json target this rule
    directly (tests_rule == id) or indirectly (opcode + flag match)?
  - covered_by_fuzzer: can any of the 14 mutators produce IR that exercises
    this rule? "yes" / "partial" / "no" / "n/a" with a short reason.
  - requirement_class: what does exercising the rule require? Categories
    include the user-specified six (flag_only, value_dependent,
    param_dependent, structural, memory, vector_specific) plus the implicit
    extras the extraction needed (baseline, nondeterministic,
    optimization_permission, attribute, metadata).

Output: docs/rule_coverage_analysis.json + summary table on stdout.
"""

from __future__ import annotations
import json
import os
import re
import sys
from pathlib import Path
from collections import defaultdict, Counter

REPO = Path(__file__).resolve().parents[2]
DOCS = REPO / "docs"
REWRITES = REPO / "rewrites.json"
OUT = DOCS / "rule_coverage_analysis.json"

# --- mutator capability model ------------------------------------------------

# Each entry: mutator -> { "flags": set of (opcode, flag) it produces;
#                          "predicates": True if it covers cmp predicates;
#                          "operand_constants": set of integer constants it
#                                                can inject;
#                          "fp_constants": set of fp constants it can inject;
#                          "attrs": set of (kind, target) attributes it
#                                   covers (kind in sext/zext);
#                          "actions": misc action strings;
#                        }

# Opcode classes (modify_flags / swap_binop both produce flags on these):
OBO_OPCODES = {"add", "sub", "mul", "shl"}  # OverflowingBinaryOperator
PE_OPCODES  = {"lshr", "ashr", "sdiv", "udiv"}  # PossiblyExactOperator
FPMATH_OPCODES = {"fadd", "fsub", "fmul", "fdiv", "frem", "fneg"}

# modify_flags coverage map: opcode -> flags it can toggle
MODIFY_FLAGS_REACH: dict[str, set[str]] = {}
for op in OBO_OPCODES:
    MODIFY_FLAGS_REACH[op] = {"nsw", "nuw"}
for op in PE_OPCODES:
    MODIFY_FLAGS_REACH[op] = {"exact"}
for op in FPMATH_OPCODES:
    MODIFY_FLAGS_REACH[op] = {
        "nnan", "ninf", "nsz", "arcp", "contract", "afn", "reassoc", "fast"
    }
# Also: phi/select/call may carry FMF in LLVM; modify_flags walks all
# instructions with isa<FPMathOperator>(), so any FP-typed instruction
# that supports FMF is reachable. We don't enumerate them here — handled
# at lookup time via has_fmf().

# Flags modify_flags CANNOT touch (it uses isa checks for OBO/PE/FPMath only):
MODIFY_FLAGS_BLIND = {"disjoint", "nneg", "inbounds", "samesign", "nuw_on_trunc"}

# swap_binop: when creating a new binop in {add,sub,mul,shl}, randomizes
# nsw/nuw; for PE class, randomizes exact; for FPMath, randomizes FMF.
SWAP_BINOP_PRODUCES = MODIFY_FLAGS_REACH  # same flag map

# mutate_gep: toggles inbounds; perturbs constant indices ±1..±2 then drops
# inbounds. Does NOT touch nuw or nusw.
MUTATE_GEP_FLAGS = {"inbounds"}

# modify_attributes: only sext / zext on integer non-bool params + return.
MODIFY_ATTRS_REACH = {"sext", "zext"}

# replace_operand: replaces an operand of a BinaryOperator or CmpInst with
# a dominating value or one of {0, 1, -1} (int) / {0.0, 1.0} (fp).
REPLACE_OPERAND_INT_CONSTS = {0, 1, -1}
REPLACE_OPERAND_FP_CONSTS = {0.0, 1.0}
REPLACE_OPERAND_TARGETS = {"BinaryOperator", "CmpInst"}

# eliminate_undef: replaces undef/poison operand with concrete (constant /
# argument / dominating value) of same type.
ELIM_UNDEF_TARGETS = "any non-PHI, non-terminator instruction operand"

# resize_type: changes integer width within {8,16,32,64} or FP float<->double
# on a BinaryOperator (wraps with ext/trunc).

# --- rule -> opcode/flag extraction ------------------------------------------

# Map LLVM langref instruction name → opcode key used in rewrites.json
INSTR_TO_OPCODE = {
    "add": "add", "sub": "sub", "mul": "mul",
    "sdiv": "sdiv", "udiv": "udiv", "srem": "srem", "urem": "urem",
    "shl": "shl", "lshr": "lshr", "ashr": "ashr",
    "and": "and", "or": "or", "xor": "xor",
    "fadd": "fadd", "fsub": "fsub", "fmul": "fmul", "fdiv": "fdiv",
    "frem": "frem", "fneg": "fneg",
    "trunc": "trunc", "zext": "zext", "sext": "sext",
    "fptrunc": "fptrunc", "fpext": "fpext",
    "fptoui": "fptoui", "fptosi": "fptosi",
    "uitofp": "uitofp", "sitofp": "sitofp",
    "ptrtoint": "ptrtoint", "inttoptr": "inttoptr",
    "bitcast": "bitcast", "addrspacecast": "addrspacecast",
    "alloca": "alloca",
    "load": "load", "store": "store",
    "getelementptr": "getelementptr",
    "phi": "phi", "select": "select",
    "icmp": "icmp", "fcmp": "fcmp",
    "br": "br", "switch": "switch", "ret": "ret",
    "call": "call", "invoke": "invoke", "indirectbr": "indirectbr",
    "unreachable": "unreachable",
    "extractelement": "extractelement", "insertelement": "insertelement",
    "shufflevector": "shufflevector",
    "extractvalue": "extractvalue", "insertvalue": "insertvalue",
    "freeze": "freeze",
    "fence": "fence",
    "atomicrmw": "atomicrmw",
    "cmpxchg": "cmpxchg",
    "landingpad": "landingpad",
    "resume": "resume",
    "catchswitch": "catchswitch",
    "catchret": "catchret",
    "cleanupret": "cleanupret",
    "catchpad": "catchpad",
    "cleanuppad": "cleanuppad",
    "va_arg": "va_arg",
    "callbr": "callbr",
}

FP_OPCODES = {"fadd", "fsub", "fmul", "fdiv", "frem", "fneg", "fcmp",
              "fptrunc", "fpext", "fptoui", "fptosi", "uitofp", "sitofp"}

INT_BINOPS = {"add", "sub", "mul", "sdiv", "udiv", "srem", "urem",
              "shl", "lshr", "ashr", "and", "or", "xor"}

# --- data loaders ------------------------------------------------------------

def load_rules() -> list[dict]:
    """Walk every extraction_*.json and pull each rule out, attaching the
    parent instruction/intrinsic key it came from."""
    rules = []
    seen_ids = {}
    for fname in sorted(os.listdir(DOCS)):
        if not (fname.startswith("extraction_") and fname.endswith(".json")):
            continue
        path = DOCS / fname
        with open(path) as f:
            d = json.load(f)
        _walk_rules(d, fname, rules, parent_chain=[])

    # Treat cross_cutting entries (non-rules dict entries) as rule-like.
    for fname in sorted(os.listdir(DOCS)):
        if not (fname.startswith("extraction_") and fname.endswith(".json")):
            continue
        with open(DOCS / fname) as f:
            d = json.load(f)
        for cc_key in ("cross_cutting_applicable", "cross_cutting",
                       "cross_cutting_rules"):
            if cc_key not in d:
                continue
            for name, body in d[cc_key].items():
                if not isinstance(body, dict):
                    continue
                if "rules" in body:
                    continue  # already collected in _walk_rules
                rid = name  # treat the key as a rule id
                rules.append({
                    "rule_id": rid,
                    "source_file": fname,
                    "parent": name,  # store the actual cross_cutting key (e.g. "fastmath.nnan")
                    "parent_section": cc_key,
                    "kind": "cross_cutting",
                    "shape": body.get("shape", "cross_cutting"),
                    "flag": None,
                    "consequence_type": _infer_cross_cutting_consequence(body),
                    "instruction": None,
                    "precondition_spec": body.get("spec", ""),
                    "precondition_derived": "",
                    "raw": body,
                })

    # Dedup by rule_id keeping the first occurrence.
    seen = {}
    out = []
    for r in rules:
        if r["rule_id"] in seen:
            continue
        seen[r["rule_id"]] = True
        out.append(r)
    return out


def _walk_rules(node, src_file, out, parent_chain):
    if isinstance(node, dict):
        # If this is a node that contains "rules", attach its key as the
        # parent instruction/intrinsic/family
        for k, v in node.items():
            if k == "rules" and isinstance(v, list):
                parent_key = parent_chain[-1] if parent_chain else None
                parent_section = parent_chain[-2] if len(parent_chain) >= 2 else None
                # Determine kind from the second-to-last key
                kind = _infer_kind(parent_section)
                # Also pull operands info / flags list from sibling fields
                sibling = node
                for r in v:
                    rid = r.get("id", "<no-id>")
                    instruction = parent_key
                    # Family-level rules: use placeholder, expand later
                    if rid.startswith("<family>"):
                        rid = f"{parent_key}.{rid[len('<family>.'):]}"
                    out.append({
                        "rule_id": rid,
                        "source_file": src_file,
                        "parent": parent_key,
                        "parent_section": parent_section,
                        "kind": kind,
                        "shape": r.get("shape"),
                        "flag": r.get("flag"),
                        "consequence_type": r.get("consequence_type"),
                        "instruction": _normalize_instr(parent_key),
                        "precondition_spec": r.get("precondition_spec", ""),
                        "precondition_derived": r.get("precondition_derived", ""),
                        "input_properties_consumed": r.get("input_properties_consumed", []),
                        "operands_info": sibling.get("operands"),
                        "instruction_flags": sibling.get("flags", []),
                        "vector_poison_scoping": sibling.get("vector_poison_scoping"),
                        "vector_semantics": sibling.get("vector_semantics"),
                        "raw": r,
                    })
            elif isinstance(v, (dict, list)):
                _walk_rules(v, src_file, out, parent_chain + [k])
    elif isinstance(node, list):
        for x in node:
            _walk_rules(x, src_file, out, parent_chain)


def _infer_kind(section: str | None) -> str:
    if section == "instructions":
        return "instruction"
    if section == "intrinsics":
        return "intrinsic"
    if section == "families":
        return "family"
    if section and "cross_cutting" in section:
        return "cross_cutting"
    return section or "unknown"


def _normalize_instr(name: str | None) -> str | None:
    if not name:
        return None
    # llvm.x.y.z → return as-is (intrinsic)
    if name.startswith("llvm."):
        return name
    # Otherwise plain opcode
    return name


def _infer_cross_cutting_consequence(body: dict) -> str:
    spec = (body.get("spec") or "").lower()
    cons = (body.get("consequence") or "").lower()
    blob = spec + " " + cons
    if "poison" in blob:
        return "poison"
    if "undefined behavior" in blob or "ub" in blob.split():
        return "ub"
    if "nondetermin" in blob:
        return "nondeterministic"
    return "defined-value"


def load_rewrites():
    with open(REWRITES) as f:
        d = json.load(f)
    return d["rewrites"]


# --- classification logic ----------------------------------------------------

def classify_requirement(rule: dict) -> str:
    shape = (rule.get("shape") or "").strip()
    cons = rule.get("consequence_type")
    flag = rule.get("flag")
    instr = rule.get("instruction") or ""
    pre = (rule.get("precondition_spec") or "").lower()
    pre_d = (rule.get("precondition_derived") or "").lower()
    rid = rule.get("rule_id", "")
    parent = rule.get("parent") or ""

    # Cross_cutting attribute / fastmath / metadata routing happens FIRST,
    # because the bodies inherit shape strings from the affected instructions
    # (e.g. attr_param.noundef has shape=operand_value_ub from the load it
    # gates) and we want the requirement class to reflect the rule's true
    # nature, not the borrowed shape.
    if rule.get("kind") == "cross_cutting":
        if parent.startswith("fastmath."):
            return "flag_only"
        if parent.startswith("attr_param.") or parent.startswith("attr_fn.") \
                or parent.startswith("attr_ret."):
            return "attribute"
        if parent.startswith("metadata_") or parent in (
                "range_metadata", "nofpclass_metadata"):
            return "structural"
        if parent.startswith("poison_propagation"):
            return "structural"
        if parent.startswith("floatenv."):
            return "baseline"
        if parent.startswith("floatnan."):
            return "nondeterministic"
        if parent.startswith("arith_overflow.preamble"):
            return "baseline"
        if parent.startswith("allocated_objects") \
                or parent.startswith("object_lifetime"):
            return "memory"

    # Vector first (override).
    vps = (rule.get("vector_poison_scoping") or "").lower()
    if shape == "element_scoped_poison" or vps == "per-element":
        return "vector_specific"
    if "scalable_mask" in shape or "scalable" in rid:
        return "vector_specific"

    # Memory rules.
    if shape in ("NEW:alignment_overestimate_ub",
                 "NEW:object_layout_ub",
                 "NEW:object_layout_invariant",
                 "NEW:nonbyte_load_poison",
                 "NEW:nonbyte_store_zext",
                 "NEW:aggregate_padding_undef",
                 "NEW:lifetime_load_poison",
                 "NEW:lifetime_store_ub",
                 "NEW:invariant_violation_ub",
                 "NEW:resource_ub",
                 "NEW:overlap_ub"):
        return "memory"
    if instr in {"load", "store", "alloca", "atomicrmw", "cmpxchg", "fence"}:
        if cons in ("poison", "ub", "defined-value") and shape != "defined_semantics":
            return "memory"
        if shape == "defined_semantics" and cons == "defined-value":
            # bare load/store base semantics are "baseline"
            return "baseline"

    # Metadata rules.
    if shape == "NEW:metadata_constraint_poison" or "metadata" in (rule.get("parent") or ""):
        return "structural"  # need to set metadata, treat as a structural setup
    if instr == "load" and "metadata" in (rule.get("precondition_spec") or "").lower():
        return "structural"

    # GEP inbounds + access patterns
    if shape in ("NEW:inrange_access_ub", "NEW:inrange_compare_ub",
                 "NEW:inbounds_subsumes_nusw", "NEW:allzero_inbounds_corollary"):
        if shape == "NEW:inrange_access_ub":
            return "memory"
        return "flag_only"

    if shape == "defined_semantics" and cons == "defined-value":
        return "baseline"

    # GEP-specific (instruction == getelementptr).
    if instr == "getelementptr" and shape != "defined_semantics":
        return "memory"

    # Flag-gated rules go to flag_only even if their consequence is
    # nondeterministic — exercising the rule requires producing the flag.
    if flag is not None and flag != "" and shape in (
            "nondeterministic", "optimization_permission"):
        # Skip the bare "None" / "" flag cases.
        if isinstance(flag, str) and flag.startswith("!"):
            return "structural"
        return "flag_only"

    if shape == "nondeterministic" or cons == "nondeterministic":
        return "nondeterministic"

    if shape == "optimization_permission":
        return "nondeterministic"

    if shape == "multi_return":
        return "structural"

    if shape == "poison_barrier":
        return "structural"

    if shape == "NEW:target_unsupported_poison":
        return "structural"

    if shape == "NEW:call_signature_mismatch_target_specific":
        return "structural"

    if shape == "NEW:freeze_poison_on_overflow":
        return "value_dependent"

    # Flag rules: shape involves a flag AND the rule's flag is set
    if flag is not None and shape in ("input_constraint_poison",
                                       "discard_poison",
                                       "param_flag_poison",
                                       "result_flag_poison"):
        # If the flag-rule's precondition further demands a specific input
        # value (e.g. trunc nuw requires high bits non-zero), it's still
        # flag-gated but also value-conditional. Call it flag_only — value
        # variety is handled by the random fuzzer.
        return "flag_only"

    # operand value rules
    if shape in ("operand_value_ub", "operand_value_poison"):
        return "value_dependent"

    # intrinsic parameter "is_zero_poison", "is_int_min_poison": param_dependent.
    if flag in {"is_zero_poison", "is_int_min_poison"}:
        return "param_dependent"

    # !nonnull / !align / !noundef / !invariant.load / nofpclass metadata
    if flag and isinstance(flag, str) and flag.startswith("!"):
        return "structural"

    # Attribute-level flags (e.g. "nonnull (on return)").
    if flag and isinstance(flag, str) and ("on return" in flag
                                            or "on parameter" in flag
                                            or "noundef" in flag
                                            or "nonnull" in flag
                                            or "dereferenceable" in flag):
        return "attribute"

    # Intrinsic-level fast-math handle
    if instr and instr.startswith("llvm.") and flag in {"nsz", "afn",
                                                          "reassoc", "nnan",
                                                          "ninf", "arcp",
                                                          "contract", "fast"}:
        return "flag_only"

    # Cross_cutting fastmath.* entries (now caught above; kept for safety)
    if rule.get("kind") == "cross_cutting" and parent.startswith("fastmath."):
        return "flag_only"
    if rule.get("kind") == "cross_cutting" and parent.startswith("attr_param."):
        return "attribute"
    if rule.get("kind") == "cross_cutting" and parent.startswith("attr_fn."):
        return "attribute"
    if rule.get("kind") == "cross_cutting" and parent.startswith("attr_ret."):
        return "attribute"
    if rule.get("kind") == "cross_cutting" and parent.startswith("metadata_"):
        return "structural"
    if rule.get("kind") == "cross_cutting" and parent.startswith("poison_propagation"):
        return "structural"
    if rule.get("kind") == "cross_cutting" and parent.startswith("floatenv."):
        return "baseline"
    if rule.get("kind") == "cross_cutting" and parent.startswith("floatnan."):
        return "nondeterministic"
    if rule.get("kind") == "cross_cutting" and parent.startswith("arith_overflow.preamble"):
        return "baseline"

    # Default fallback.
    if cons == "ub":
        return "structural"
    if cons == "poison":
        return "structural"
    return "baseline"


def coverage_by_mutations(rule: dict, rewrites: list[dict],
                          by_tests_rule: dict[str, list[dict]],
                          by_opcode_flag: dict[tuple, list[dict]],
                          by_flag_any: dict[str, list[dict]],
                          by_attr_any: dict[str, list[dict]]) -> tuple[bool, list[str], str]:
    """Return (covered, matching_rewrite_ids, reason)."""
    rid = rule["rule_id"]
    instr = rule.get("instruction")
    flag = rule.get("flag")
    parent = rule.get("parent") or ""
    matches: list[str] = []

    # 1) Direct tests_rule match.
    if rid in by_tests_rule:
        matches.extend(r["id"] for r in by_tests_rule[rid])

    # 2) Indirect opcode+flag/action match for flag-style rules.
    if instr and flag and isinstance(flag, str):
        bare_flag = flag.split()[0]
        for r in by_opcode_flag.get((instr, bare_flag), []):
            matches.append(r["id"])

    # 3) Cross_cutting fastmath.X: match rewrites adding flag X on any FP op.
    if rule.get("kind") == "cross_cutting" and parent.startswith("fastmath."):
        flag_name = parent.split(".", 1)[1]
        for r in by_flag_any.get(flag_name, []):
            matches.append(r["id"])

    # 4) Cross_cutting attribute families.
    if rule.get("kind") == "cross_cutting" and parent in ("attr_param.noundef",
                                                            "attr_ret.noundef"):
        for r in by_attr_any.get("noundef", []):
            matches.append(r["id"])
    if rule.get("kind") == "cross_cutting" and parent in ("attr_param.nonnull",
                                                            "attr_ret.nonnull"):
        for r in by_attr_any.get("nonnull", []):
            matches.append(r["id"])
    if rule.get("kind") == "cross_cutting" and parent in ("attr_param.dereferenceable",
                                                            "attr_param.dereferenceable_or_null"):
        for r in by_attr_any.get("dereferenceable", []):
            matches.append(r["id"])
    if rule.get("kind") == "cross_cutting" and parent == "attr_fn.willreturn":
        for r in by_attr_any.get("willreturn", []):
            matches.append(r["id"])
    if rule.get("kind") == "cross_cutting" and parent == "attr_fn.mustprogress":
        for r in by_attr_any.get("mustprogress", []):
            matches.append(r["id"])
    if rule.get("kind") == "cross_cutting" and parent == "attr_fn.nounwind":
        for r in by_attr_any.get("nounwind", []):
            matches.append(r["id"])

    matches = sorted(set(matches))
    if matches:
        return True, matches, f"matched {len(matches)} rewrite(s)"
    return False, [], ""


def coverage_by_fuzzer(rule: dict) -> tuple[str, str]:
    """Return ("yes"/"partial"/"no"/"n/a", reason)."""
    shape = rule.get("shape") or ""
    cons = rule.get("consequence_type")
    flag = rule.get("flag")
    instr = rule.get("instruction") or ""
    parent = rule.get("parent") or ""
    rid = rule.get("rule_id", "")
    pre_d = (rule.get("precondition_derived") or "").lower()
    pre_s = (rule.get("precondition_spec") or "").lower()

    # baseline / defined_semantics → exercised whenever the instruction is
    # present. Mutators that produce or preserve the instruction count.
    if shape == "defined_semantics":
        return "yes", "exercised by any seed/mutation that yields the instruction"

    # FMF: covered by modify_flags on any FP-typed instruction; covered by
    # swap_binop when creating an FP binop.
    if flag in {"nnan", "ninf", "nsz", "arcp", "contract",
                "afn", "reassoc", "fast", "(any fast-math flag)"}:
        if instr in FPMATH_OPCODES:
            return "yes", "modify_flags + swap_binop randomize FMF on FP binops"
        # call/phi/select/intrinsics with FMF
        if instr in {"phi", "select", "fcmp"} or instr.startswith("llvm."):
            return "yes", "modify_flags walks all isa<FPMathOperator>() instructions"
        return "partial", "modify_flags covers any FP-typed instruction with FMF support"

    # Integer wrap flags.
    if flag in {"nsw", "nuw"}:
        if instr in OBO_OPCODES:
            return "yes", "modify_flags toggles nsw/nuw on add/sub/mul/shl; swap_binop also randomizes"
        if instr == "trunc":
            return "no", "modify_flags only walks OverflowingBinaryOperator; trunc.nuw/nsw aren't reached"
        if instr == "getelementptr":
            return "no", "mutate_gep only toggles inbounds; gep.nuw not reached"
        return "partial", f"nsw/nuw on {instr} not in modify_flags reach"

    if flag == "exact":
        if instr in PE_OPCODES:
            return "yes", "modify_flags toggles exact on sdiv/udiv/lshr/ashr"
        return "partial", f"exact on {instr} requires PossiblyExactOperator"

    if flag == "nneg":
        return "no", "modify_flags is blind to nneg (zext)"

    if flag == "disjoint":
        return "no", "modify_flags is blind to disjoint (or)"

    if flag == "samesign":
        return "no", "modify_flags is blind to samesign (icmp)"

    if flag in {"inbounds"}:
        return "yes", "mutate_gep toggles inbounds"

    if flag in {"nusw"}:
        return "no", "mutate_gep does not toggle nusw"

    # Intrinsic parameter flags: is_zero_poison, is_int_min_poison
    if flag in {"is_zero_poison", "is_int_min_poison"}:
        return "no", "no mutator alters intrinsic immediate parameters"

    # Attribute-style flags.
    if flag and isinstance(flag, str) and (
            "on return" in flag or "on parameter" in flag
            or flag in {"!nonnull", "!align", "!noundef", "!invariant.load"}):
        return "no", "modify_attributes only covers sext/zext on integer params/return"

    # operand_value_ub / operand_value_poison
    if shape in ("operand_value_ub", "operand_value_poison"):
        # divisor zero
        if "div" in instr and ("zero" in pre_d or "zero" in pre_s):
            return "partial", "replace_operand can substitute 0 as divisor (uniform random)"
        if "rem" in instr and ("zero" in pre_d or "zero" in pre_s):
            return "partial", "replace_operand can substitute 0 as divisor for rem"
        if instr in {"sdiv", "srem"} and "int_min" in pre_d.replace("_", "_").lower():
            return "partial", "replace_operand has -1 but not INT_MIN; only partial coverage"
        # shift amount poison: replace_operand has 0, 1, -1; -1 sign-extends
        # to a large unsigned value (>= bitwidth in 8/16 widths).
        if "shamt" in rid or ("shift" in pre_s and "bitwidth" in pre_s):
            return "partial", ("replace_operand can substitute -1 (>= bitwidth at small widths) "
                                "but not bitwidth itself")
        return "no", "operand-value precondition not produced by any mutator's constant pool"

    # NEW:freeze_poison_on_overflow
    if shape == "NEW:freeze_poison_on_overflow":
        return "no", "no mutator inserts overflow scenarios on FP conversions"

    # Nondeterministic / multi_return / optimization_permission
    if shape in ("nondeterministic",) or cons == "nondeterministic":
        return "n/a", "nondeterministic behavior; nothing to 'exercise' per se"

    if shape == "multi_return":
        return "yes" if instr.startswith("llvm.") else "n/a", "extractvalue on existing intrinsic call exercises this"

    if shape == "optimization_permission":
        return "n/a", "optimization permission for the optimizer, not directly exercisable"

    # poison_barrier (freeze)
    if shape == "poison_barrier":
        return "no", "no mutator inserts freeze; only the spec-mutate engine does"

    # element_scoped_poison
    if shape == "element_scoped_poison":
        return "no", "vector poison scoping not exercised by any current mutator"

    # NEW:target_unsupported_poison
    if shape == "NEW:target_unsupported_poison":
        return "no", "addrspacecast target support not perturbed by any mutator"

    # NEW:scalable_mask_restriction
    if shape == "NEW:scalable_mask_restriction":
        return "no", "scalable vector mask shape not perturbed"

    # NEW:call_signature_mismatch_target_specific
    if shape == "NEW:call_signature_mismatch_target_specific":
        return "no", "no mutator perturbs call signatures"

    # Cross_cutting: fastmath family
    if rule.get("kind") == "cross_cutting" and parent.startswith("fastmath."):
        return "yes", "modify_flags can toggle all FMF flags on FP instructions"

    # Cross_cutting attribute families.
    if rule.get("kind") == "cross_cutting" and parent.startswith("attr_"):
        return "no", "modify_attributes only covers sext/zext"

    # Cross_cutting poison_propagation: trivial — any poison-producing
    # mutation propagates through later instructions.
    if rule.get("kind") == "cross_cutting" and parent.startswith("poison_propagation"):
        return "yes", "poison from any mutation propagates through subsequent ops"

    # Memory shapes
    if shape in ("NEW:alignment_overestimate_ub",
                 "NEW:object_layout_ub",
                 "NEW:object_layout_invariant",
                 "NEW:nonbyte_load_poison",
                 "NEW:nonbyte_store_zext",
                 "NEW:aggregate_padding_undef",
                 "NEW:lifetime_load_poison",
                 "NEW:lifetime_store_ub",
                 "NEW:invariant_violation_ub",
                 "NEW:resource_ub",
                 "NEW:overlap_ub"):
        return "no", "memory layout / lifetime scenarios not produced by any mutator"

    # Metadata constraint poison
    if shape == "NEW:metadata_constraint_poison":
        return "no", "no mutator adds range/nofpclass/nonnull metadata"

    # NEW:inrange_*: gep inrange requires inrange attribute on a GEP; no mutator adds it
    if shape in ("NEW:inrange_access_ub", "NEW:inrange_compare_ub",
                 "NEW:inbounds_subsumes_nusw"):
        return "no", "no mutator adds or perturbs the inrange attribute"

    if shape == "NEW:allzero_inbounds_corollary":
        return "yes", "trivially exercised by any all-zero-index GEP in seeds"

    # NEW:undef_select_yields_undef
    if shape == "NEW:undef_select_yields_undef":
        return "partial", "eliminate_undef removes undef rather than producing it"

    # NEW:zero_size_nonunique
    if shape == "NEW:zero_size_nonunique":
        return "no", "alloca size not perturbed by any mutator"

    # Catch-all
    return "no", f"no mutator known to produce shape={shape}, cons={cons}, flag={flag}"


def main():
    rules = load_rules()
    rewrites = load_rewrites()

    # Index rewrites by tests_rule.
    by_tests_rule: dict[str, list[dict]] = defaultdict(list)
    for r in rewrites:
        tr = r.get("tests_rule")
        if tr:
            by_tests_rule[tr].append(r)

    # Index rewrites by (opcode, flag) — for indirect match.
    by_opcode_flag: dict[tuple, list[dict]] = defaultdict(list)
    by_flag_any: dict[str, list[dict]] = defaultdict(list)
    by_attr_any: dict[str, list[dict]] = defaultdict(list)
    for r in rewrites:
        m = r.get("match", {})
        opcodes = m.get("opcode") or []
        if isinstance(opcodes, str):
            opcodes = [opcodes]
        t = r.get("transform", {})
        flag = t.get("flag") or m.get("flag_present") or m.get("flag_absent")
        attr = t.get("attr")
        for op in opcodes:
            if flag:
                by_opcode_flag[(op, flag)].append(r)
            if attr:
                by_opcode_flag[(op, attr)].append(r)
        if flag:
            by_flag_any[flag].append(r)
        if attr:
            by_attr_any[attr].append(r)

    # Score each rule.
    out_entries = []
    by_class = Counter()
    by_class_mut = Counter()
    by_class_fuz = Counter()
    by_class_gap = Counter()
    cov_mut = 0
    cov_fuz = 0
    cov_both = 0
    cov_neither = 0
    gap_rules = []

    for r in rules:
        req = classify_requirement(r)
        cm, matches, mreason = coverage_by_mutations(r, rewrites,
                                                      by_tests_rule,
                                                      by_opcode_flag,
                                                      by_flag_any,
                                                      by_attr_any)
        cf_status, cf_reason = coverage_by_fuzzer(r)
        cf = cf_status in ("yes", "partial")
        not_testable = cf_status == "n/a"
        # Aggregates
        if cm:
            cov_mut += 1
        if cf:
            cov_fuz += 1
        if cm and cf:
            cov_both += 1
        # Gap = no rewrite, no fuzzer path, AND the rule is testable.
        if not cm and not cf and not not_testable:
            cov_neither += 1
            gap_rules.append(r["rule_id"])

        by_class[req] += 1
        if cm:
            by_class_mut[req] += 1
        if cf:
            by_class_fuz[req] += 1
        if not cm and not cf and not not_testable:
            by_class_gap[req] += 1

        gap_note = ""
        if not_testable:
            gap_note = "not directly exercisable (nondeterministic / optimization permission)"
        elif not cm and not cf:
            gap_note = "no rewrite, no mutator path; needs litmus injection or new mutator"
        elif not cm and cf:
            gap_note = "fuzzer-only (random); no targeted rewrite"
        elif cm and not cf:
            gap_note = "rewrite-only; no fuzzer path (this is the value of spec-mutate)"

        combined_fuzzer = f"{cf_status} ({cf_reason})" if cf_reason else cf_status
        out_entries.append({
            "rule_id": r["rule_id"],
            "source_file": r["source_file"],
            "instruction": r.get("instruction"),
            "kind": r.get("kind"),
            "shape": r.get("shape"),
            "flag": r.get("flag"),
            "consequence_type": r.get("consequence_type"),
            "covered_by_mutations": cm,
            "covered_by_mutations_matches": matches,
            "covered_by_fuzzer": combined_fuzzer,
            "covered_by_fuzzer_status": cf_status,
            "requirement_class": req,
            "gap_note": gap_note,
        })

    # Count not-testable separately.
    not_testable = sum(1 for r in out_entries
                       if r["covered_by_fuzzer_status"] == "n/a")

    summary = {
        "total_rules": len(rules),
        "covered_by_mutations": cov_mut,
        "covered_by_fuzzer": cov_fuz,
        "covered_by_both": cov_both,
        "covered_by_neither": cov_neither,
        "not_testable": not_testable,
        "by_requirement_class": dict(by_class),
        "by_requirement_class_mutation_covered": dict(by_class_mut),
        "by_requirement_class_fuzzer_covered": dict(by_class_fuz),
        "by_requirement_class_gap": dict(by_class_gap),
    }

    methodology = {
        "rule_source": ("docs/extraction_*.json — five files containing "
                         "~271 instruction/intrinsic rules + 34 unique "
                         "cross_cutting rules (296 total after dedup, of "
                         "which 158 are defined_semantics 'baseline'). The "
                         "integer arithmetic instructions (add, sub, mul, "
                         "sdiv, udiv, srem, urem, shl, lshr, ashr, and, or, "
                         "xor) are NOT yet extracted, so 42 of 53 unique "
                         "`tests_rule` values in rewrites.json point to "
                         "rule IDs that do not yet exist."),
        "covered_by_mutations": ("Set True if (a) any rewrite's "
                                  "`tests_rule` matches the rule id exactly, "
                                  "or (b) any rewrite's opcode + flag/attr "
                                  "matches the rule's instruction + flag, "
                                  "or (c) cross_cutting fastmath.X / "
                                  "attr_*.Y matches a rewrite that adds the "
                                  "corresponding flag/attribute on any "
                                  "instruction."),
        "covered_by_fuzzer": ("'yes' if a mutator definitely produces IR "
                               "that satisfies the rule's precondition; "
                               "'partial' if a mutator might produce it "
                               "(uniform-random constant pool happens to "
                               "include the value); 'no' if no mutator "
                               "path exists; 'n/a' if the rule is not "
                               "directly exercisable (nondeterministic, "
                               "optimization_permission)."),
        "requirement_class": ("Classification: the user-specified six "
                               "(flag_only, value_dependent, "
                               "param_dependent, structural, memory, "
                               "vector_specific) plus four extras the data "
                               "needed: baseline (defined_semantics; "
                               "exercised by any seed containing the "
                               "instruction), attribute (function/param/"
                               "return attribute, separated from `flag_only` "
                               "because modify_attributes only handles "
                               "sext/zext, blind to noundef/nonnull/etc.), "
                               "nondeterministic (no specific output to "
                               "verify, so not directly testable)."),
        "mutator_capability_assumptions": {
            "modify_flags": ("nsw/nuw on {add,sub,mul,shl}, exact on "
                              "{sdiv,udiv,lshr,ashr}, FMF on any "
                              "isa<FPMathOperator>(); blind to disjoint, "
                              "nneg, inbounds, samesign, gep.nuw, trunc.nuw/nsw."),
            "swap_binop": ("Same flag reach as modify_flags on the new "
                            "instruction; randomizes opcode within "
                            "intArith/bitwise/shifts/fpArith class."),
            "swap_cmp_predicate": "All icmp + fcmp predicates.",
            "replace_operand": ("Substitutes {0, 1, -1} for int, {0.0, "
                                 "1.0} for FP. Covers div-by-zero, "
                                 "sdiv-1, identity-zero/one. Does NOT cover "
                                 "INT_MIN, large/bitwidth values, "
                                 "out-of-range FP."),
            "modify_attributes": ("Only sext/zext on integer non-bool "
                                    "params + return. Blind to "
                                    "noundef/nonnull/dereferenceable/range/"
                                    "willreturn/etc."),
            "mutate_gep": ("Toggles inbounds; perturbs constant indices "
                            "±1..±2 (drops inbounds when doing so). Does "
                            "NOT toggle nuw or nusw."),
            "resize_type": ("Changes int width within {8,16,32,64} or FP "
                             "float<->double on a BinaryOperator with "
                             "wrapping ext/trunc."),
            "eliminate_undef": ("Replaces undef/poison operand with "
                                 "concrete value (constant / arg / "
                                 "dominating value)."),
            "mutate_unary": "Replaces an FP binop with fneg of one operand.",
            "shuffle_instructions / move_instruction / inline_call / "
            "remove_void_call / swap_operands": ("Structural mutations; do "
                                                  "not target any specific "
                                                  "rule preconditions."),
        },
    }

    output = {
        "_doc": ("Coverage analysis of extracted rules vs. rewrites.json + "
                 "fuzzer mutators. Generated by "
                 "scripts/analysis/rule_coverage_analysis.py."),
        "_methodology": methodology,
        "summary": summary,
        "rules": out_entries,
        "gap_rules": gap_rules,
    }

    with open(OUT, "w") as f:
        json.dump(output, f, indent=2)

    # Print summary table.
    print(f"Wrote {OUT}  ({len(rules)} rules)")
    print()
    print(f"Total rules:           {summary['total_rules']}")
    print(f"Covered by mutations:  {cov_mut}")
    print(f"Covered by fuzzer:     {cov_fuz}")
    print(f"Covered by both:       {cov_both}")
    print(f"Not directly testable: {not_testable}  "
           "(nondeterministic / optimization permission)")
    print(f"Covered by neither:    {cov_neither}  ← THE GAP")
    print()
    classes_order = ["baseline", "flag_only", "value_dependent",
                     "param_dependent", "structural", "memory",
                     "vector_specific", "attribute",
                     "nondeterministic"]
    # include any other classes that appeared
    for c in by_class:
        if c not in classes_order:
            classes_order.append(c)

    print(f"  {'Requirement class':<22} │ {'Total':>5} │ {'Mutations':>9} │ "
          f"{'Fuzzer':>6} │ {'Gap':>3}")
    print(f"  {'-'*22}─┼─{'-'*5}─┼─{'-'*9}─┼─{'-'*6}─┼─{'-'*3}")
    for c in classes_order:
        if by_class.get(c, 0) == 0:
            continue
        print(f"  {c:<22} │ {by_class[c]:>5} │ {by_class_mut.get(c,0):>9} │ "
              f"{by_class_fuz.get(c,0):>6} │ {by_class_gap.get(c,0):>3}")


if __name__ == "__main__":
    main()
