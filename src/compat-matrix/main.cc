// compat-matrix — derive a rule-to-rule compatibility matrix from the
// LangRef rule database.
//
// Reads one or more rule JSONs (--rules can be passed multiple times),
// classifies each rule with structured output / input tags, then walks
// every rule pair and records SSA edges where A's result can flow into
// some operand of B with an "interesting" interaction (POISON_TO_UB,
// POISON_BARRIER, GUARANTEES_SAFE, ROUND_TRIP, POISON_CHAIN). Also adds
// memory- and control-dependence edges when the types line up.
//
// Output: JSON to --output (default docs/compat_matrix.json) plus a
// top-20 connectivity table and category roll-up to stdout.

#include "deps/nlohmann-json/json.hpp"

#include <algorithm>
#include <bitset>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <map>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

// Use ordered_json so operand insertion order from the JSON is preserved.
// Operand position is load-bearing for the compatibility matrix (e.g.
// "pointer" is operand index 1 of load, not index 2). The default
// nlohmann::json sorts object keys alphabetically.
using json = nlohmann::ordered_json;

namespace {

// =========================================================================
// Schema (richer than litmus-gen's; we need consequence_type, output and
// input properties, and result_type to compute tags).
// =========================================================================

struct Operand {
  std::string name;
  std::string type_constraint;
};

struct Rule {
  std::string id;
  std::string instruction;   // back-pointer so the matrix can stay flat
  std::string shape;
  std::optional<std::string> flag;
  std::string consequence_type;          // "defined-value" | "poison" | "ub" | "nondeterministic" | "not-poison"
  std::string precondition;              // precondition_spec ++ precondition_derived
  std::vector<std::string> output_props; // joined property + note strings
  std::vector<std::string> input_props;
};

struct Instruction {
  std::string name;
  std::vector<Operand> operands;
  std::string result_type;
  std::vector<std::string> flags;
  std::vector<Rule> rules;
};

struct Database {
  std::vector<Instruction> instructions;            // preserves insertion order
  std::unordered_map<std::string, size_t> by_name;  // name -> index into instructions
};

// =========================================================================
// String helpers.
// =========================================================================

std::string toLower(std::string s) {
  for (auto &c : s) c = (char)std::tolower((unsigned char)c);
  return s;
}

bool containsCI(const std::string &hay, const std::string &needle) {
  // Case-insensitive substring search. hay is expected to already be
  // lowercased by callers; needle is a literal we control.
  return hay.find(needle) != std::string::npos;
}

bool containsAny(const std::string &hay,
                 std::initializer_list<const char *> needles) {
  for (const auto *n : needles)
    if (hay.find(n) != std::string::npos) return true;
  return false;
}

// =========================================================================
// JSON parsing — accepts both the V1 schema ("instructions": {...}) and
// the family-grouped schema ("intrinsics" + "families"). The 5
// extraction_*.json files together exercise both.
// =========================================================================

std::string optString(const json &j, const char *field) {
  auto it = j.find(field);
  if (it == j.end()) return {};
  if (it->is_string()) return it->get<std::string>();
  return {};
}

std::vector<std::string> flattenProps(const json &j, const char *field) {
  std::vector<std::string> out;
  auto it = j.find(field);
  if (it == j.end()) return out;
  if (it->is_array()) {
    for (const auto &p : *it) {
      if (p.is_string()) {
        out.push_back(p.get<std::string>());
      } else if (p.is_object()) {
        std::string text = optString(p, "property");
        std::string note = optString(p, "note");
        if (!note.empty()) text += " (" + note + ")";
        if (!text.empty()) out.push_back(std::move(text));
      }
    }
  }
  return out;
}

Rule parseRule(const json &ruleJson, const std::string &instName,
               bool fromFamily) {
  Rule r;
  r.id = optString(ruleJson, "id");
  if (fromFamily) {
    static const std::string ph = "<family>";
    size_t pos = r.id.find(ph);
    if (pos != std::string::npos) r.id.replace(pos, ph.size(), instName);
  }
  r.instruction = instName;
  r.shape = optString(ruleJson, "shape");
  std::string flagStr = optString(ruleJson, "flag");
  if (!flagStr.empty()) r.flag = flagStr;
  r.consequence_type = optString(ruleJson, "consequence_type");
  r.precondition = optString(ruleJson, "precondition_spec");
  std::string derived = optString(ruleJson, "precondition_derived");
  if (!derived.empty()) r.precondition += " | " + derived;
  r.output_props = flattenProps(ruleJson, "output_properties_produced");
  auto ipIt = ruleJson.find("input_properties_consumed");
  if (ipIt != ruleJson.end() && ipIt->is_array()) {
    for (const auto &v : *ipIt)
      if (v.is_string()) r.input_props.push_back(v.get<std::string>());
  }
  return r;
}

void parseOneFile(Database &db, const std::string &path) {
  std::ifstream f(path);
  if (!f) {
    std::fprintf(stderr, "compat-matrix: cannot open %s\n", path.c_str());
    return;
  }
  std::stringstream ss;
  ss << f.rdbuf();
  json root;
  try {
    root = json::parse(ss.str());
  } catch (const json::parse_error &e) {
    std::fprintf(stderr, "compat-matrix: %s: %s\n", path.c_str(), e.what());
    return;
  }

  // Optional family map (extraction_remaining.json).
  std::map<std::string, const json *> families;
  auto famIt = root.find("families");
  if (famIt != root.end() && famIt->is_object()) {
    for (auto it = famIt->begin(); it != famIt->end(); ++it)
      families[it.key()] = &it.value();
  }

  // Per-instruction map: "instructions" or "intrinsics".
  const json *instMap = nullptr;
  auto i1 = root.find("instructions");
  if (i1 != root.end() && i1->is_object()) {
    instMap = &*i1;
  } else {
    auto i2 = root.find("intrinsics");
    if (i2 != root.end() && i2->is_object()) instMap = &*i2;
  }
  if (!instMap) {
    std::fprintf(stderr, "compat-matrix: %s: no top-level instructions/intrinsics\n",
                 path.c_str());
    return;
  }

  for (auto it = instMap->begin(); it != instMap->end(); ++it) {
    const std::string &instName = it.key();
    const json &instJson = it.value();
    if (!instJson.is_object()) continue;

    // Find or create the Instruction entry. Sharing across input files
    // is fine — we just append rules.
    auto bi = db.by_name.find(instName);
    Instruction *inst;
    if (bi == db.by_name.end()) {
      db.instructions.push_back({});
      inst = &db.instructions.back();
      inst->name = instName;
      db.by_name[instName] = db.instructions.size() - 1;
    } else {
      inst = &db.instructions[bi->second];
    }

    if (inst->result_type.empty())
      inst->result_type = optString(instJson, "result_type");

    auto flagsIt = instJson.find("flags");
    if (inst->flags.empty() && flagsIt != instJson.end() && flagsIt->is_array()) {
      for (const auto &v : *flagsIt)
        if (v.is_string()) inst->flags.push_back(v.get<std::string>());
    }

    auto opsIt = instJson.find("operands");
    if (inst->operands.empty() && opsIt != instJson.end() && opsIt->is_object()) {
      for (auto opIt = opsIt->begin(); opIt != opsIt->end(); ++opIt) {
        Operand op;
        op.name = opIt.key();
        op.type_constraint = optString(opIt.value(), "type_constraint");
        inst->operands.push_back(std::move(op));
      }
    }

    auto rulesIt = instJson.find("rules");
    if (rulesIt != instJson.end() && rulesIt->is_array()) {
      for (const json &ruleJson : *rulesIt)
        inst->rules.push_back(parseRule(ruleJson, instName, /*fromFamily=*/false));
    } else {
      auto fIt = instJson.find("family");
      if (fIt != instJson.end() && fIt->is_string()) {
        auto fit = families.find(fIt->get<std::string>());
        if (fit != families.end()) {
          // Pull operands / flags / result_type from family if absent on
          // the member.
          if (inst->result_type.empty())
            inst->result_type = optString(*fit->second, "result_type");
          auto fOpsIt = fit->second->find("operands");
          if (inst->operands.empty() && fOpsIt != fit->second->end() &&
              fOpsIt->is_object()) {
            for (auto opIt = fOpsIt->begin(); opIt != fOpsIt->end(); ++opIt) {
              Operand op;
              op.name = opIt.key();
              op.type_constraint = optString(opIt.value(), "type_constraint");
              inst->operands.push_back(std::move(op));
            }
          }
          auto fFlagsIt = fit->second->find("flags");
          if (inst->flags.empty() && fFlagsIt != fit->second->end() &&
              fFlagsIt->is_array()) {
            for (const auto &v : *fFlagsIt)
              if (v.is_string()) inst->flags.push_back(v.get<std::string>());
          }
          auto fRulesIt = fit->second->find("rules");
          if (fRulesIt != fit->second->end() && fRulesIt->is_array()) {
            for (const json &ruleJson : *fRulesIt)
              inst->rules.push_back(
                  parseRule(ruleJson, instName, /*fromFamily=*/true));
          }
        }
      }
    }
  }
}

// =========================================================================
// Type classification. We collapse the variety of constraint strings
// into a small set of classes; compatibility is set-intersection. The
// classifier is conservative: ambiguous → Unknown, which matches nothing.
// =========================================================================

struct TypeSet {
  bool intT = false;
  bool i1 = false;
  bool fp = false;
  bool ptr = false;
  bool aggregate = false;  // struct / array / "{iN, i1}"
  bool label = false;
  bool keyword = false;    // condition codes, ordering names, immarg
  bool voidT = false;
  bool unknown = false;
};

bool compatible(const TypeSet &a, const TypeSet &b) {
  if (a.unknown || b.unknown) return false;
  return (a.intT && b.intT) || (a.i1 && b.i1) || (a.fp && b.fp) ||
         (a.ptr && b.ptr) || (a.aggregate && b.aggregate) ||
         (a.label && b.label) || (a.keyword && b.keyword);
}

// Word-boundary substring search. Returns true iff `needle` appears in
// `hay` with non-alnum characters on either side (or at string ends).
bool wordMatch(const std::string &hay, const std::string &needle) {
  size_t pos = 0;
  while ((pos = hay.find(needle, pos)) != std::string::npos) {
    bool lb = (pos == 0) ||
              (!std::isalnum((unsigned char)hay[pos - 1]) && hay[pos - 1] != '_');
    size_t end = pos + needle.size();
    bool rb = (end == hay.size()) ||
              (!std::isalnum((unsigned char)hay[end]) && hay[end] != '_');
    if (lb && rb) return true;
    ++pos;
  }
  return false;
}

TypeSet classifyTypeString(const std::string &raw, const Instruction &inst,
                           std::set<std::string> visited) {
  TypeSet out;
  if (raw.empty()) {
    out.unknown = true;
    return out;
  }
  std::string lo = toLower(raw);

  // "same as X" — look up operand X. Strings observed: "same as op1",
  // "same as a", "same as a/b", "same as Mag", "same as val1/val2",
  // "same as operand", "same as operands", "same as src", "same as Val".
  auto sa = lo.find("same as ");
  if (sa != std::string::npos) {
    size_t start = sa + 8;
    size_t end = start;
    while (end < raw.size() &&
           (std::isalnum((unsigned char)raw[end]) || raw[end] == '_'))
      ++end;
    std::string ref(raw.begin() + start, raw.begin() + end);
    if (visited.count(ref) == 0 && !ref.empty()) {
      visited.insert(ref);
      for (const auto &op : inst.operands) {
        if (op.name == ref)
          return classifyTypeString(op.type_constraint, inst, visited);
      }
      // "operand" / "operands" — heuristic: use first operand.
      std::string refLo = toLower(ref);
      if ((refLo == "operand" || refLo == "operands") && !inst.operands.empty())
        return classifyTypeString(inst.operands.front().type_constraint, inst,
                                   visited);
    }
    // Fall through to substring matching on the raw text.
  }

  // Aggregate (struct/array): leading '{' or explicit "aggregate" or
  // "struct".
  if (lo.find('{') != std::string::npos ||
      containsAny(lo, {"aggregate", "struct", "array"}))
    out.aggregate = true;

  // Floating-point.
  if (raw.find("FloatT") != std::string::npos ||
      containsAny(lo, {"ieee 754", "floating-point", "floatingpoint"}))
    out.fp = true;

  // Integer. Use case-sensitive matching for LangRef placeholders
  // ("IntT", "iN", "iM") so we don't catch "in" in normal English text.
  if (raw.find("IntT") != std::string::npos ||
      wordMatch(raw, "iN") || wordMatch(raw, "iM"))
    out.intT = true;
  // Explicit integer widths: i1, i8, i16, i32, i64. Scan for 'i' that
  // begins a token and is followed only by digits.
  bool i1_marker = false;
  for (size_t i = 0; i < raw.size(); ++i) {
    if (raw[i] != 'i') continue;
    if (i > 0 && (std::isalnum((unsigned char)raw[i - 1]) || raw[i - 1] == '_'))
      continue;
    size_t j = i + 1;
    while (j < raw.size() && std::isdigit((unsigned char)raw[j])) ++j;
    if (j == i + 1) continue;  // no digits → not a width token
    if (j < raw.size() &&
        (std::isalnum((unsigned char)raw[j]) || raw[j] == '_'))
      continue;
    std::string num(raw.begin() + i + 1, raw.begin() + j);
    if (num == "1")
      i1_marker = true;
    else
      out.intT = true;
  }
  if (i1_marker) out.i1 = true;

  // Pointer.
  if (wordMatch(lo, "ptr") || wordMatch(lo, "pointer") || wordMatch(lo, "pty"))
    out.ptr = true;

  // Label / control flow.
  if (wordMatch(lo, "label")) out.label = true;

  // Keyword (condition code, ordering, immarg). Constant-only operands
  // (alignment, scale immarg) are also classified as keyword — they
  // cannot be the target of a runtime SSA def from another rule.
  if (containsAny(lo, {"keyword", "immarg", "condition code"}) ||
      wordMatch(lo, "constant"))
    out.keyword = true;

  // Void.
  if (wordMatch(lo, "void")) out.voidT = true;

  // "first-class T" / "first-class value" / "first-class type" — broad;
  // assume any value type.
  if (lo.find("first-class") != std::string::npos) {
    out.intT = out.fp = out.ptr = out.aggregate = true;
  }

  // "ty" alone as the result_type means "same as the named operand 'ty'".
  if (raw == "ty" && visited.count("ty") == 0) {
    visited.insert("ty");
    for (const auto &op : inst.operands)
      if (op.name == "ty")
        return classifyTypeString(op.type_constraint, inst, visited);
  }

  bool any = out.intT || out.i1 || out.fp || out.ptr || out.aggregate ||
             out.label || out.keyword || out.voidT;
  if (!any) out.unknown = true;
  return out;
}

TypeSet classifyOperand(const Operand &op, const Instruction &inst) {
  return classifyTypeString(op.type_constraint, inst, {});
}

TypeSet classifyResult(const Instruction &inst) {
  return classifyTypeString(inst.result_type, inst, {});
}

// =========================================================================
// Output and input tags.
// =========================================================================

enum OutputTag {
  OT_MAY_POISON,
  OT_ALWAYS_DEFINED,
  OT_RANGE_BOUNDED,
  OT_BITS_LOW_ZERO,
  OT_BITS_HIGH_ZERO,
  OT_NONZERO,
  OT_NONNEGATIVE,
  OT_NOT_NAN,
  OT_NOT_INF,
  OT_DIVISIBLE,
  OT_SIGN_PRESERVED,
  OT_RECOVERABLE,
  OT_IS_POINTER,
  OT_COUNT,
};

const char *outputTagName(OutputTag t) {
  switch (t) {
    case OT_MAY_POISON: return "MAY_POISON";
    case OT_ALWAYS_DEFINED: return "ALWAYS_DEFINED";
    case OT_RANGE_BOUNDED: return "RANGE_BOUNDED";
    case OT_BITS_LOW_ZERO: return "BITS_LOW_ZERO";
    case OT_BITS_HIGH_ZERO: return "BITS_HIGH_ZERO";
    case OT_NONZERO: return "NONZERO";
    case OT_NONNEGATIVE: return "NONNEGATIVE";
    case OT_NOT_NAN: return "NOT_NAN";
    case OT_NOT_INF: return "NOT_INF";
    case OT_DIVISIBLE: return "DIVISIBLE";
    case OT_SIGN_PRESERVED: return "SIGN_PRESERVED";
    case OT_RECOVERABLE: return "RECOVERABLE";
    case OT_IS_POINTER: return "IS_POINTER";
    case OT_COUNT: return "<count>";
  }
  return "<unknown>";
}

enum InputTag {
  IT_SENSITIVE_POISON_UB,
  IT_SENSITIVE_POISON_PROP,
  IT_SENSITIVE_POISON_BLOCK,
  IT_SENSITIVE_SHIFT_RANGE,
  IT_SENSITIVE_NONZERO,
  IT_SENSITIVE_HIGH_BITS,
  IT_SENSITIVE_SIGN_BITS,
  IT_SENSITIVE_LOW_BITS,
  IT_SENSITIVE_NONNEG,
  IT_SENSITIVE_DIVISIBLE,
  IT_SENSITIVE_NAN,
  IT_SENSITIVE_INF,
  IT_SENSITIVE_RANGE,
};

using OutputTags = std::bitset<OT_COUNT>;

OutputTags computeOutputTags(const Rule &rule, const Instruction &inst) {
  OutputTags tags;

  if (rule.consequence_type == "poison") tags.set(OT_MAY_POISON);
  if (rule.consequence_type != "poison" && rule.consequence_type != "ub")
    tags.set(OT_ALWAYS_DEFINED);

  std::string blob;
  for (const auto &p : rule.output_props) blob += " " + toLower(p);

  auto has = [&](const char *s) { return blob.find(s) != std::string::npos; };
  auto hasBoth = [&](const char *a, const char *b) {
    return has(a) && has(b);
  };

  if (has("range") || has("bounded") || has("[0,") || has("in bits"))
    tags.set(OT_RANGE_BOUNDED);
  if (hasBoth("low bits", "zero") || hasBoth("bottom bits", "zero") ||
      hasBoth("bottom op", "zero"))
    tags.set(OT_BITS_LOW_ZERO);
  if ((has("high bits") || has("top bits") || has("top order bits") ||
       has("high order bits") || has("top (")) &&
      has("zero"))
    tags.set(OT_BITS_HIGH_ZERO);
  if (has("nonzero") || has("non-zero") || has(">= 1") || has("bit set") ||
      has("at least one bit"))
    tags.set(OT_NONZERO);
  if (has("non-negative") || has(">= 0") || has("nonnegative"))
    tags.set(OT_NONNEGATIVE);
  if (has("not nan") || has("not-nan")) tags.set(OT_NOT_NAN);
  if (rule.flag.has_value() && *rule.flag == "nnan") tags.set(OT_NOT_NAN);
  if (has("not inf") || has("not-inf") || has("finite")) tags.set(OT_NOT_INF);
  if (rule.flag.has_value() && *rule.flag == "ninf") tags.set(OT_NOT_INF);
  if (has("divisible") || has("multiple of")) tags.set(OT_DIVISIBLE);
  if (hasBoth("sign", "preserved")) tags.set(OT_SIGN_PRESERVED);
  if (has("recoverable") || has("round-trip") || has("recovers original"))
    tags.set(OT_RECOVERABLE);

  TypeSet rt = classifyResult(inst);
  if (rt.ptr) tags.set(OT_IS_POINTER);

  return tags;
}

// =========================================================================
// Per-operand input tags. We always set SENSITIVE_POISON_PROP as a
// baseline, then layer on the flag/precondition/hardcoded specifics.
// =========================================================================

using OpTagMap = std::map<int, std::set<InputTag>>;

OpTagMap computeInputTags(const Rule &rule, const Instruction &inst) {
  OpTagMap tags;
  const int nops = (int)inst.operands.size();
  for (int i = 0; i < nops; ++i)
    tags[i].insert(IT_SENSITIVE_POISON_PROP);

  auto isValueOperand = [&](int i) -> bool {
    if (i < 0 || i >= nops) return false;
    TypeSet ts = classifyOperand(inst.operands[i], inst);
    return ts.intT || ts.i1 || ts.fp || ts.ptr;
  };

  auto addToValueOps = [&](InputTag t) {
    for (int i = 0; i < nops; ++i)
      if (isValueOperand(i)) tags[i].insert(t);
  };

  // POISON_TO_UB: hard-coded operand positions where poison input ->
  // UB. Driven by LangRef cross-cutting poison_to_ub rules.
  auto markByName = [&](std::initializer_list<const char *> names, InputTag t) {
    for (int i = 0; i < nops; ++i) {
      for (const auto *n : names)
        if (inst.operands[i].name == n) tags[i].insert(t);
    }
  };
  if (inst.name == "sdiv" || inst.name == "udiv" || inst.name == "srem" ||
      inst.name == "urem")
    markByName({"op2", "b"}, IT_SENSITIVE_POISON_UB);
  if (inst.name == "load" || inst.name == "store")
    markByName({"pointer", "ptr"}, IT_SENSITIVE_POISON_UB);
  if (inst.name == "br" || inst.name == "switch")
    markByName({"cond", "value"}, IT_SENSITIVE_POISON_UB);

  // POISON_BARRIER: select's non-selected value arm is blocked from
  // propagating poison.
  if (inst.name == "select")
    markByName({"val1", "val2"}, IT_SENSITIVE_POISON_BLOCK);

  // Flag-driven sensitivities (apply to all value operands).
  std::string flag = rule.flag.value_or("");
  std::string pre = toLower(rule.precondition);

  if (flag == "nuw") addToValueOps(IT_SENSITIVE_HIGH_BITS);
  if (flag == "nsw") addToValueOps(IT_SENSITIVE_SIGN_BITS);
  if (flag == "exact") {
    // For shifts the "exact" requires low bits to be zero in op1; for
    // div the divisor must exactly divide. Tag both.
    addToValueOps(IT_SENSITIVE_LOW_BITS);
    addToValueOps(IT_SENSITIVE_DIVISIBLE);
  }
  if (flag == "nneg") addToValueOps(IT_SENSITIVE_NONNEG);
  if (flag == "nnan") addToValueOps(IT_SENSITIVE_NAN);
  if (flag == "ninf") addToValueOps(IT_SENSITIVE_INF);
  if (flag == "disjoint") {
    // Disjoint OR: poison if any bit set in both. Both operands matter
    // for the low-bits / high-bits patterns.
    addToValueOps(IT_SENSITIVE_LOW_BITS);
  }

  // Shift-amount sensitivity: only the shamt operand. For binary shifts
  // it's op2 / b; for funnel shifts (fshl/fshr) it's c.
  auto markShamt = [&](std::initializer_list<const char *> names) {
    for (int i = 0; i < nops; ++i) {
      for (const auto *n : names)
        if (inst.operands[i].name == n)
          tags[i].insert(IT_SENSITIVE_SHIFT_RANGE);
    }
  };
  if (inst.name == "shl" || inst.name == "lshr" || inst.name == "ashr" ||
      inst.name == "llvm.sshl.sat" || inst.name == "llvm.ushl.sat")
    markShamt({"op2", "b"});
  if (inst.name == "llvm.fshl" || inst.name == "llvm.fshr")
    markShamt({"c"});
  if (containsAny(pre, {"bitwidth", "shamt", "shift amount"}))
    markShamt({"shamt"});

  // div-by-zero / "checks for zero" → SENSITIVE_NONZERO on the divisor.
  if (containsAny(pre, {"div by zero", "division by zero", "divisor is 0",
                         "divisor == 0", "/ 0", "by zero"}) ||
      inst.name == "sdiv" || inst.name == "udiv" || inst.name == "srem" ||
      inst.name == "urem" || inst.name == "llvm.sdiv.fix" ||
      inst.name == "llvm.udiv.fix" || inst.name == "llvm.sdiv.fix.sat" ||
      inst.name == "llvm.udiv.fix.sat") {
    markByName({"op2", "b"}, IT_SENSITIVE_NONZERO);
  }
  // cttz/ctlz: src!=0 precondition.
  if ((inst.name == "llvm.cttz" || inst.name == "llvm.ctlz") &&
      containsAny(pre, {"src != 0", "src!=0", "input != 0"})) {
    markByName({"src", "op1"}, IT_SENSITIVE_NONZERO);
  }

  // SENSITIVE_RANGE — FP-to-int rounding checks the input value range.
  if (inst.name == "fptoui" || inst.name == "fptosi" ||
      inst.name == "llvm.lround" || inst.name == "llvm.llround" ||
      inst.name == "llvm.lrint" || inst.name == "llvm.llrint")
    addToValueOps(IT_SENSITIVE_RANGE);

  return tags;
}

// =========================================================================
// ROUND_TRIP: hard-coded inverse pairs.
// =========================================================================

bool isRoundTripPair(const std::string &a, const std::string &b) {
  static const std::vector<std::pair<std::string, std::string>> kPairs = {
      {"shl", "lshr"},    {"shl", "ashr"},  {"lshr", "shl"}, {"ashr", "shl"},
      {"trunc", "zext"},  {"trunc", "sext"},
      {"zext", "trunc"},  {"sext", "trunc"},
      {"mul", "sdiv"},    {"mul", "udiv"},
  };
  for (const auto &p : kPairs)
    if (a == p.first && b == p.second) return true;
  return false;
}

// =========================================================================
// Building the matrix.
// =========================================================================

enum Category {
  CAT_POISON_TO_UB,
  CAT_POISON_BARRIER,
  CAT_GUARANTEES_SAFE,
  CAT_ROUND_TRIP,
  CAT_POISON_CHAIN,
};

const char *categoryName(Category c) {
  switch (c) {
    case CAT_POISON_TO_UB: return "POISON_TO_UB";
    case CAT_POISON_BARRIER: return "POISON_BARRIER";
    case CAT_GUARANTEES_SAFE: return "GUARANTEES_SAFE";
    case CAT_ROUND_TRIP: return "ROUND_TRIP";
    case CAT_POISON_CHAIN: return "POISON_CHAIN";
  }
  return "<unknown>";
}

struct Edge {
  std::string source_rule;
  std::string target_rule;
  int target_operand;  // -1 for memory/control edges (no SSA position)
  std::vector<std::string> edge_types;
  std::vector<Category> categories;
  std::string notes;
};

bool isStorable(const TypeSet &ts) {
  return ts.intT || ts.i1 || ts.fp || ts.ptr || ts.aggregate;
}
bool isComparable(const TypeSet &ts) {
  return ts.intT || ts.i1 || ts.fp || ts.ptr;
}

// =========================================================================
// JSON output.
// =========================================================================

void emitEdge(json &arr, const Edge &e) {
  json j;
  j["source_rule"] = e.source_rule;
  j["target_rule"] = e.target_rule;
  j["target_operand"] = e.target_operand;
  j["edge_types"] = e.edge_types;
  j["categories"] = json::array();
  for (auto c : e.categories) j["categories"].push_back(categoryName(c));
  if (!e.notes.empty()) j["notes"] = e.notes;
  arr.push_back(std::move(j));
}

// =========================================================================
// CLI parsing.
// =========================================================================

struct Options {
  std::vector<std::string> rules_paths;
  std::string output_path = "docs/compat_matrix.json";
};

[[noreturn]] void usage(int code) {
  std::fprintf(stderr,
               "Usage: compat-matrix --rules <a.json> [--rules <b.json> ...]\n"
               "                     [--output <path>]\n"
               "\n"
               "  --rules can be repeated; all files are merged into one rule database.\n"
               "  Default --output: docs/compat_matrix.json\n");
  std::exit(code);
}

Options parseArgs(int argc, char **argv) {
  Options o;
  for (int i = 1; i < argc; ++i) {
    std::string a = argv[i];
    auto needValue = [&](const char *flag) -> std::string {
      if (i + 1 >= argc) {
        std::fprintf(stderr, "compat-matrix: %s requires a value\n", flag);
        usage(2);
      }
      return argv[++i];
    };
    if (a == "--rules") {
      o.rules_paths.push_back(needValue("--rules"));
    } else if (a.rfind("--rules=", 0) == 0) {
      o.rules_paths.push_back(a.substr(8));
    } else if (a == "--output") {
      o.output_path = needValue("--output");
    } else if (a.rfind("--output=", 0) == 0) {
      o.output_path = a.substr(9);
    } else if (a == "-h" || a == "--help") {
      usage(0);
    } else {
      std::fprintf(stderr, "compat-matrix: unknown arg '%s'\n", a.c_str());
      usage(2);
    }
  }
  if (o.rules_paths.empty()) usage(2);
  return o;
}

}  // namespace

int main(int argc, char **argv) {
  Options opts = parseArgs(argc, argv);

  Database db;
  for (const auto &p : opts.rules_paths) parseOneFile(db, p);

  // Resolve instruction-level summaries: total rules.
  size_t total_rules = 0;
  for (const auto &inst : db.instructions) total_rules += inst.rules.size();

  // Precompute tags + result-type classes for every rule, indexed by a
  // flat rule index so the O(N^2) pair loop stays tight.
  struct PreRule {
    const Rule *rule;
    const Instruction *inst;
    OutputTags out_tags;
    TypeSet result_ts;
  };
  std::vector<PreRule> all_rules;
  all_rules.reserve(total_rules);
  // Per-instruction precomputation: input tags per rule, and operand
  // type classes (shared across this instruction's rules).
  struct PreInst {
    const Instruction *inst;
    std::vector<TypeSet> op_ts;
    std::vector<OpTagMap> rule_input_tags;  // parallel to inst->rules
  };
  std::vector<PreInst> all_insts;
  all_insts.reserve(db.instructions.size());
  std::unordered_map<const Instruction *, size_t> inst_to_idx;
  for (const auto &inst : db.instructions) {
    PreInst pi;
    pi.inst = &inst;
    pi.op_ts.reserve(inst.operands.size());
    for (const auto &op : inst.operands)
      pi.op_ts.push_back(classifyOperand(op, inst));
    pi.rule_input_tags.reserve(inst.rules.size());
    for (const auto &r : inst.rules)
      pi.rule_input_tags.push_back(computeInputTags(r, inst));
    inst_to_idx[&inst] = all_insts.size();
    all_insts.push_back(std::move(pi));
  }
  for (const auto &inst : db.instructions) {
    for (const auto &r : inst.rules) {
      PreRule pr;
      pr.rule = &r;
      pr.inst = &inst;
      pr.out_tags = computeOutputTags(r, inst);
      pr.result_ts = classifyResult(inst);
      all_rules.push_back(pr);
    }
  }

  // Build edges.
  json edges = json::array();
  std::map<Category, size_t> cat_counts;
  // Track partners separately per edge type — control/memory edges
  // dominate (an i1 producer connects to nearly everything), so a
  // combined connectivity table is uninformative.
  std::unordered_map<std::string, std::unordered_set<std::string>> ssa_partners;
  std::unordered_map<std::string, std::unordered_set<std::string>> mem_partners;
  std::unordered_map<std::string, std::unordered_set<std::string>> ctl_partners;

  auto bumpPartner =
      [&](std::unordered_map<std::string, std::unordered_set<std::string>> &m,
          const std::string &a, const std::string &b) {
        m[a].insert(b);
        m[b].insert(a);
      };

  for (const auto &A : all_rules) {
    if (A.result_ts.unknown) continue;  // can't drive any edge
    for (const auto &B : all_rules) {
      const PreInst &Bpi = all_insts[inst_to_idx[B.inst]];

      // --- SSA edges (per operand position) ---
      const int nops = (int)B.inst->operands.size();
      for (int oi = 0; oi < nops; ++oi) {
        const TypeSet &b_ts = Bpi.op_ts[oi];
        if (!compatible(A.result_ts, b_ts)) continue;

        // Compute categories for this position.
        size_t rule_idx = (size_t)(B.rule - B.inst->rules.data());
        const std::set<InputTag> &b_op_tags =
            Bpi.rule_input_tags[rule_idx].at(oi);
        bool A_may_poison = A.out_tags[OT_MAY_POISON];

        std::vector<Category> cats;
        std::string note;

        if (A_may_poison && b_op_tags.count(IT_SENSITIVE_POISON_UB))
          cats.push_back(CAT_POISON_TO_UB);
        if (A_may_poison && b_op_tags.count(IT_SENSITIVE_POISON_BLOCK))
          cats.push_back(CAT_POISON_BARRIER);

        auto checkGuarantee = [&](OutputTag ot, InputTag it, const char *msg) {
          if (A.out_tags[ot] && b_op_tags.count(it)) {
            if (std::find(cats.begin(), cats.end(), CAT_GUARANTEES_SAFE) ==
                cats.end())
              cats.push_back(CAT_GUARANTEES_SAFE);
            if (!note.empty()) note += "; ";
            note += msg;
          }
        };
        checkGuarantee(OT_BITS_LOW_ZERO, IT_SENSITIVE_LOW_BITS,
                       "low bits zero satisfies exact");
        checkGuarantee(OT_BITS_HIGH_ZERO, IT_SENSITIVE_HIGH_BITS,
                       "high bits zero satisfies nuw");
        checkGuarantee(OT_NONZERO, IT_SENSITIVE_NONZERO,
                       "nonzero satisfies divisor / no-div-by-zero");
        checkGuarantee(OT_NONNEGATIVE, IT_SENSITIVE_NONNEG,
                       "non-negative satisfies nneg");
        checkGuarantee(OT_RANGE_BOUNDED, IT_SENSITIVE_SHIFT_RANGE,
                       "range-bounded satisfies shamt < bitwidth");
        checkGuarantee(OT_NOT_NAN, IT_SENSITIVE_NAN,
                       "not-NaN satisfies nnan");
        checkGuarantee(OT_NOT_INF, IT_SENSITIVE_INF,
                       "not-Inf satisfies ninf");
        checkGuarantee(OT_DIVISIBLE, IT_SENSITIVE_DIVISIBLE,
                       "divisible satisfies exact div");

        if (isRoundTripPair(A.inst->name, B.inst->name))
          cats.push_back(CAT_ROUND_TRIP);

        // POISON_CHAIN only when nothing else fired (and A may poison).
        if (cats.empty() && A_may_poison &&
            b_op_tags.count(IT_SENSITIVE_POISON_PROP))
          cats.push_back(CAT_POISON_CHAIN);

        if (cats.empty()) continue;

        Edge e;
        e.source_rule = A.rule->id;
        e.target_rule = B.rule->id;
        e.target_operand = oi;
        e.edge_types = {"ssa"};
        e.categories = cats;
        e.notes = note;
        emitEdge(edges, e);
        for (auto c : cats) ++cat_counts[c];
        bumpPartner(ssa_partners, A.rule->id, B.rule->id);
      }

      // --- MEMORY edge: A's result storable, B has a storable operand
      // (single edge per (A, B), reported with target_operand = -1). To
      // keep the matrix from exploding, we only emit when there is
      // actually a type-compatible operand pairing (i.e. an SSA edge
      // could also fire, ignoring category constraints).
      bool A_storable = isStorable(A.result_ts);
      bool B_has_storable = false;
      int first_storable_op = -1;
      if (A_storable) {
        for (int oi = 0; oi < nops; ++oi) {
          if (compatible(A.result_ts, Bpi.op_ts[oi]) &&
              isStorable(Bpi.op_ts[oi])) {
            B_has_storable = true;
            first_storable_op = oi;
            break;
          }
        }
      }
      if (B_has_storable) {
        Edge e;
        e.source_rule = A.rule->id;
        e.target_rule = B.rule->id;
        e.target_operand = first_storable_op;
        e.edge_types = {"memory"};
        // No specific category — memory is a path, not a guarantee.
        emitEdge(edges, e);
        bumpPartner(mem_partners, A.rule->id, B.rule->id);
      }

      // --- CONTROL edge: A's result is comparable (could feed an icmp →
      // br guarding B's basic block). One edge per (A, B). To keep
      // bounded, restrict to A that produces i1 or whose result type is
      // i1-compatible (the most direct "feeds into br" path) OR when B
      // has an i1 operand named "cond".
      bool A_comparable = isComparable(A.result_ts);
      bool B_has_cond = false;
      for (int oi = 0; oi < nops; ++oi) {
        if (B.inst->operands[oi].name == "cond" && Bpi.op_ts[oi].i1) {
          B_has_cond = true;
          break;
        }
      }
      if (A_comparable && (A.result_ts.i1 || B_has_cond)) {
        Edge e;
        e.source_rule = A.rule->id;
        e.target_rule = B.rule->id;
        e.target_operand = -1;
        e.edge_types = {"control"};
        emitEdge(edges, e);
        bumpPartner(ctl_partners, A.rule->id, B.rule->id);
      }
    }
  }

  // Aggregate metadata.
  size_t pair_count = 0;
  std::set<std::pair<std::string, std::string>> seen_pairs;
  for (const auto &e : edges) {
    std::pair<std::string, std::string> k{e["source_rule"].get<std::string>(),
                                          e["target_rule"].get<std::string>()};
    if (seen_pairs.insert(k).second) ++pair_count;
  }

  json root;
  json meta;
  meta["total_rules"] = total_rules;
  meta["total_instructions"] = db.instructions.size();
  meta["total_edges"] = edges.size();
  meta["total_compatible_pairs"] = pair_count;
  json bc = json::object();
  for (int c = CAT_POISON_TO_UB; c <= CAT_POISON_CHAIN; ++c) {
    Category cat = (Category)c;
    bc[categoryName(cat)] = (uint64_t)cat_counts[cat];
  }
  meta["by_category"] = bc;
  meta["inputs"] = opts.rules_paths;
  root["metadata"] = meta;
  root["edges"] = edges;

  // Write JSON.
  std::ofstream of(opts.output_path);
  if (!of) {
    std::fprintf(stderr, "compat-matrix: cannot open output: %s\n",
                 opts.output_path.c_str());
    return 1;
  }
  of << root.dump(2) << "\n";

  // ----- stdout summary -----
  // Top-20 ordered by SSA partners (the interesting axis); memory and
  // control partner counts are shown alongside so the reader can see how
  // much they add.
  auto sizeOf =
      [](const std::unordered_map<std::string, std::unordered_set<std::string>> &m,
         const std::string &k) -> size_t {
    auto it = m.find(k);
    return it == m.end() ? 0 : it->second.size();
  };
  std::set<std::string> all_endpoints;
  for (const auto &m : {std::ref(ssa_partners), std::ref(mem_partners),
                         std::ref(ctl_partners)})
    for (const auto &kv : m.get()) all_endpoints.insert(kv.first);

  struct Row {
    std::string id;
    size_t ssa, mem, ctl;
  };
  std::vector<Row> rows;
  rows.reserve(all_endpoints.size());
  for (const auto &id : all_endpoints) {
    rows.push_back({id, sizeOf(ssa_partners, id), sizeOf(mem_partners, id),
                    sizeOf(ctl_partners, id)});
  }
  std::sort(rows.begin(), rows.end(), [](const Row &a, const Row &b) {
    if (a.ssa != b.ssa) return a.ssa > b.ssa;
    return a.id < b.id;
  });

  std::printf("=== compat-matrix summary ===\n");
  std::printf("Inputs: %zu file(s)\n", opts.rules_paths.size());
  for (const auto &p : opts.rules_paths)
    std::printf("  - %s\n", p.c_str());
  std::printf("Instructions: %zu, rules: %zu\n", db.instructions.size(),
              total_rules);
  std::printf("Edges: %zu (across %zu unique (source,target) pairs)\n",
              (size_t)edges.size(), pair_count);
  std::printf("\nCategory distribution (SSA edges):\n");
  for (int c = CAT_POISON_TO_UB; c <= CAT_POISON_CHAIN; ++c) {
    Category cat = (Category)c;
    std::printf("  %-18s %zu\n", categoryName(cat), cat_counts[cat]);
  }
  std::printf("\nTop-20 most-connected rules (by SSA partners):\n");
  std::printf("  %-50s %5s %5s %5s\n", "rule_id", "ssa", "mem", "ctl");
  std::printf("  %-50s %5s %5s %5s\n", "-------", "---", "---", "---");
  size_t topN = std::min<size_t>(20, rows.size());
  for (size_t i = 0; i < topN; ++i)
    std::printf("  %-50s %5zu %5zu %5zu\n", rows[i].id.c_str(), rows[i].ssa,
                rows[i].mem, rows[i].ctl);
  std::printf("\nWrote %s\n", opts.output_path.c_str());

  return 0;
}
