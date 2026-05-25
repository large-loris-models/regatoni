// compose-gen — emit two-instruction litmus modules from the compat-matrix.
//
// Reads docs/compat_matrix.json (or another --matrix) plus the same rule
// JSON files that fed compat-matrix, then for each matrix edge tries to
// emit a function:
//
//     define <ret> @compose__<A>__<B>__<edge>__<type>(...) {
//       %a = <A pattern>(...)
//       %b = <B pattern>(...%a in slot...)
//       ret <ret> %b
//     }
//
// For memory edges the two ops are connected through an alloca + store +
// load round-trip instead of a direct SSA def.
//
// Instruction emission is purely textual: a per-instruction kind table
// drives the IR shape, so we don't need to link LLVM's IRBuilder. Each
// emitted module is shelled out to opt -O2 -disable-output for sanity
// before being written.
//
// V1 scope: scalar integer types only, SSA + memory edges, single
// integer-pattern intrinsic kinds. FP, pointer, vector, casts, overflow
// extractvalue, and memory intrinsics are out — they'll be added later
// once the spine is in place.

#include "deps/nlohmann-json/json.hpp"

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <fstream>
#include <map>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <unordered_map>
#include <unordered_set>
#include <vector>

using nlohmann::json;

namespace {

// =========================================================================
// Schema. We only carry the fields compose-gen needs (rule id, flag,
// shape, operand types). Everything else from the JSON is discarded.
// =========================================================================

struct Operand {
  std::string name;
  std::string type_constraint;
};

struct Rule {
  std::string id;
  std::string instruction;
  std::optional<std::string> flag;
  std::string shape;
};

struct Instruction {
  std::string name;
  std::vector<Operand> operands;
  std::vector<Rule> rules;
};

struct Database {
  std::vector<Instruction> instructions;
  std::unordered_map<std::string, size_t> by_name;
  // rule_id -> (instruction index, rule index)
  std::unordered_map<std::string, std::pair<size_t, size_t>> rule_index;
};

// =========================================================================
// JSON helpers. Mirror what compat-matrix and litmus-gen already do — the
// rule files come in two flavors (`instructions` map and family-grouped
// `intrinsics`), and we need to accept both.
// =========================================================================

std::string optString(const json &j, const char *field) {
  auto it = j.find(field);
  if (it == j.end() || !it->is_string()) return {};
  return it->get<std::string>();
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
  std::string fs = optString(ruleJson, "flag");
  if (!fs.empty()) r.flag = fs;
  return r;
}

void parseOneFile(Database &db, const std::string &path) {
  std::ifstream f(path);
  if (!f) {
    std::fprintf(stderr, "compose-gen: cannot open %s\n", path.c_str());
    return;
  }
  std::stringstream ss;
  ss << f.rdbuf();
  json root;
  try {
    root = json::parse(ss.str());
  } catch (const json::parse_error &e) {
    std::fprintf(stderr, "compose-gen: %s: %s\n", path.c_str(), e.what());
    return;
  }

  std::map<std::string, const json *> families;
  auto famIt = root.find("families");
  if (famIt != root.end() && famIt->is_object()) {
    for (auto it = famIt->begin(); it != famIt->end(); ++it)
      families[it.key()] = &it.value();
  }

  const json *instMap = nullptr;
  auto i1 = root.find("instructions");
  if (i1 != root.end() && i1->is_object()) {
    instMap = &*i1;
  } else {
    auto i2 = root.find("intrinsics");
    if (i2 != root.end() && i2->is_object()) instMap = &*i2;
  }
  if (!instMap) {
    std::fprintf(stderr, "compose-gen: %s: no top-level instructions/intrinsics\n",
                 path.c_str());
    return;
  }

  for (auto it = instMap->begin(); it != instMap->end(); ++it) {
    const std::string &instName = it.key();
    const json &instJson = it.value();
    if (!instJson.is_object()) continue;

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

void buildRuleIndex(Database &db) {
  for (size_t i = 0; i < db.instructions.size(); ++i) {
    const auto &inst = db.instructions[i];
    for (size_t r = 0; r < inst.rules.size(); ++r)
      db.rule_index[inst.rules[r].id] = {i, r};
  }
}

// =========================================================================
// Pattern kinds. Each kind owns a fixed IR shape; the dispatch by
// instruction name lives in kindOf() below. Adding a new instruction is
// a one-line table entry plus (for new shapes) a new case in the emit /
// slot helpers.
// =========================================================================

enum Kind {
  K_BINARY,
  K_CAST,
  K_UNARY_INTRINSIC,
  K_BINARY_INTRINSIC,
  K_TERNARY_INTRINSIC,
  K_INTRINSIC_WITH_FLAG,
  K_OVERFLOW_INTRINSIC,
  K_FIXED_POINT_INTRINSIC,
  K_CMP_INTRINSIC,
  K_FREEZE,
  K_SELECT,
  K_ICMP,
  K_GEP,
  K_LOAD,
  K_STORE,
  K_MEMINTRINSIC,
  K_EXTRACTVALUE,
  K_EXTRACTELEMENT,
  K_INSERTELEMENT,
  K_VECTOR_REDUCE,
  K_VECTOR_REDUCE_FOLD,
  K_BITCAST,
  K_FP_BINARY,
  K_FP_UNARY,
  K_FP_UNARY_INTRINSIC,
  K_FP_BINARY_INTRINSIC,
  K_FP_TERNARY_INTRINSIC,
  K_FCMP,
  K_FP_INT_CAST,
  K_FP_CAST,
  K_POWI_INTRINSIC,
  K_FPCLASS_INTRINSIC,
  K_FPTOI_SAT,
  K_UNSUPPORTED,
};

const char *kindName(Kind k) {
  switch (k) {
    case K_BINARY: return "binary";
    case K_CAST: return "cast";
    case K_UNARY_INTRINSIC: return "unary_intrinsic";
    case K_BINARY_INTRINSIC: return "binary_intrinsic";
    case K_TERNARY_INTRINSIC: return "ternary_intrinsic";
    case K_INTRINSIC_WITH_FLAG: return "intrinsic_with_flag";
    case K_OVERFLOW_INTRINSIC: return "overflow_intrinsic";
    case K_FIXED_POINT_INTRINSIC: return "fixed_point_intrinsic";
    case K_CMP_INTRINSIC: return "cmp_intrinsic";
    case K_FREEZE: return "freeze";
    case K_SELECT: return "select";
    case K_ICMP: return "icmp";
    case K_GEP: return "gep";
    case K_LOAD: return "load";
    case K_STORE: return "store";
    case K_MEMINTRINSIC: return "memintrinsic";
    case K_EXTRACTVALUE: return "extractvalue";
    case K_EXTRACTELEMENT: return "extractelement";
    case K_INSERTELEMENT: return "insertelement";
    case K_VECTOR_REDUCE: return "vector_reduce";
    case K_VECTOR_REDUCE_FOLD: return "vector_reduce_fold";
    case K_BITCAST: return "bitcast";
    case K_FP_BINARY: return "fp_binary";
    case K_FP_UNARY: return "fp_unary";
    case K_FP_UNARY_INTRINSIC: return "fp_unary_intrinsic";
    case K_FP_BINARY_INTRINSIC: return "fp_binary_intrinsic";
    case K_FP_TERNARY_INTRINSIC: return "fp_ternary_intrinsic";
    case K_FCMP: return "fcmp";
    case K_FP_INT_CAST: return "fp_int_cast";
    case K_FP_CAST: return "fp_cast";
    case K_POWI_INTRINSIC: return "powi_intrinsic";
    case K_FPCLASS_INTRINSIC: return "fpclass_intrinsic";
    case K_FPTOI_SAT: return "fptoi_sat";
    case K_UNSUPPORTED: return "unsupported";
  }
  return "?";
}

Kind kindOf(const std::string &name) {
  // V1 dispatch table. The brief lists more kinds (binary, cast, gep,
  // load_store, extractvalue) — they're declared in the enum so the rest
  // of the code can be extended without churning the switch, but no rule
  // in our current JSONs hits them.
  static const std::unordered_map<std::string, Kind> table = {
      // Binary integer intrinsics
      {"llvm.smax", K_BINARY_INTRINSIC},
      {"llvm.smin", K_BINARY_INTRINSIC},
      {"llvm.umax", K_BINARY_INTRINSIC},
      {"llvm.umin", K_BINARY_INTRINSIC},
      {"llvm.sadd.sat", K_BINARY_INTRINSIC},
      {"llvm.ssub.sat", K_BINARY_INTRINSIC},
      {"llvm.uadd.sat", K_BINARY_INTRINSIC},
      {"llvm.usub.sat", K_BINARY_INTRINSIC},
      {"llvm.sshl.sat", K_BINARY_INTRINSIC},
      {"llvm.ushl.sat", K_BINARY_INTRINSIC},
      {"llvm.clmul", K_BINARY_INTRINSIC},
      // Unary integer intrinsics
      {"llvm.bitreverse", K_UNARY_INTRINSIC},
      {"llvm.ctpop", K_UNARY_INTRINSIC},
      {"llvm.bswap", K_UNARY_INTRINSIC},
      // Ternary integer intrinsics
      {"llvm.fshl", K_TERNARY_INTRINSIC},
      {"llvm.fshr", K_TERNARY_INTRINSIC},
      // Unary integer + i1-immarg
      {"llvm.cttz", K_INTRINSIC_WITH_FLAG},
      {"llvm.ctlz", K_INTRINSIC_WITH_FLAG},
      {"llvm.abs", K_INTRINSIC_WITH_FLAG},
      // Overflow intrinsics: (iN, iN) -> {iN, i1}. Always emitted with an
      // extractvalue 0 to expose the iN result; control edges also extract
      // element 1 for the overflow bit.
      {"llvm.sadd.with.overflow", K_OVERFLOW_INTRINSIC},
      {"llvm.uadd.with.overflow", K_OVERFLOW_INTRINSIC},
      {"llvm.ssub.with.overflow", K_OVERFLOW_INTRINSIC},
      {"llvm.usub.with.overflow", K_OVERFLOW_INTRINSIC},
      {"llvm.smul.with.overflow", K_OVERFLOW_INTRINSIC},
      {"llvm.umul.with.overflow", K_OVERFLOW_INTRINSIC},
      // Fixed-point intrinsics: (iN, iN, i32 immarg scale) -> iN. Scale is
      // a constant immarg; the rule operand at index 2 is rejected by
      // operandToSlot so target_op=2 edges are filtered upstream.
      {"llvm.smul.fix", K_FIXED_POINT_INTRINSIC},
      {"llvm.umul.fix", K_FIXED_POINT_INTRINSIC},
      {"llvm.sdiv.fix", K_FIXED_POINT_INTRINSIC},
      {"llvm.udiv.fix", K_FIXED_POINT_INTRINSIC},
      {"llvm.smul.fix.sat", K_FIXED_POINT_INTRINSIC},
      {"llvm.umul.fix.sat", K_FIXED_POINT_INTRINSIC},
      {"llvm.sdiv.fix.sat", K_FIXED_POINT_INTRINSIC},
      {"llvm.udiv.fix.sat", K_FIXED_POINT_INTRINSIC},
      // Three-way comparison intrinsics: (iN, iN) -> i8. Result type is
      // fixed at i8 so the kind is treated as cast-like (hardcoded
      // operand-type/result-type pairs).
      {"llvm.scmp", K_CMP_INTRINSIC},
      {"llvm.ucmp", K_CMP_INTRINSIC},
      // Integer casts
      {"trunc", K_CAST},
      {"zext", K_CAST},
      {"sext", K_CAST},
      // FP <-> integer conversions. Cross-type casts.
      {"fptoui", K_FP_INT_CAST},
      {"fptosi", K_FP_INT_CAST},
      {"uitofp", K_FP_INT_CAST},
      {"sitofp", K_FP_INT_CAST},
      // FP precision casts.
      {"fpext", K_FP_CAST},
      {"fptrunc", K_FP_CAST},
      // Saturating FP-to-int conversions: (FP) -> int, clamped.
      {"llvm.fptoui.sat", K_FPTOI_SAT},
      {"llvm.fptosi.sat", K_FPTOI_SAT},
      // libm-style FP-to-int rounding: (FP) -> i32 or i64. Same emission
      // pattern as K_FPTOI_SAT (two-suffix mangling: result type then
      // source type) so we reuse the kind.
      {"llvm.lround", K_FPTOI_SAT},
      {"llvm.llround", K_FPTOI_SAT},
      {"llvm.lrint", K_FPTOI_SAT},
      {"llvm.llrint", K_FPTOI_SAT},
      // Vector reductions (integer): (<4 x iT>) -> iT.
      {"llvm.vector.reduce.add", K_VECTOR_REDUCE},
      {"llvm.vector.reduce.mul", K_VECTOR_REDUCE},
      {"llvm.vector.reduce.and", K_VECTOR_REDUCE},
      {"llvm.vector.reduce.or", K_VECTOR_REDUCE},
      {"llvm.vector.reduce.xor", K_VECTOR_REDUCE},
      {"llvm.vector.reduce.smax", K_VECTOR_REDUCE},
      {"llvm.vector.reduce.smin", K_VECTOR_REDUCE},
      {"llvm.vector.reduce.umax", K_VECTOR_REDUCE},
      {"llvm.vector.reduce.umin", K_VECTOR_REDUCE},
      // Vector reductions (FP, single vector input): (<4 x fpT>) -> fpT.
      {"llvm.vector.reduce.fmax", K_VECTOR_REDUCE},
      {"llvm.vector.reduce.fmin", K_VECTOR_REDUCE},
      {"llvm.vector.reduce.fmaximum", K_VECTOR_REDUCE},
      {"llvm.vector.reduce.fminimum", K_VECTOR_REDUCE},
      // Vector reductions (FP, fold with start value): (fpT, <4 x fpT>) -> fpT.
      {"llvm.vector.reduce.fadd", K_VECTOR_REDUCE_FOLD},
      {"llvm.vector.reduce.fmul", K_VECTOR_REDUCE_FOLD},
      // Vector element accessors.
      {"extractelement", K_EXTRACTELEMENT},
      {"insertelement", K_INSERTELEMENT},
      // Memory ops.
      {"getelementptr", K_GEP},
      {"load", K_LOAD},
      {"store", K_STORE},
      {"llvm.memcpy", K_MEMINTRINSIC},
      {"llvm.memmove", K_MEMINTRINSIC},
      {"llvm.memset", K_MEMINTRINSIC},
      // Bitcast: int <-> FP (same size). Pointer<->pointer bitcasts are
      // gone with opaque pointers.
      {"bitcast", K_BITCAST},
      // powi: (FP Val, i32 power) -> FP. Slot 1 is i32 regardless of
      // primary FP type; typeCombosForEdge has a special branch for that.
      {"llvm.powi", K_POWI_INTRINSIC},
      // is.fpclass: (FP, i32 immarg test) -> i1. Slot 1 (test) is immarg
      // and operandToSlot returns -1 for it.
      {"llvm.is.fpclass", K_FPCLASS_INTRINSIC},
      // Misc
      {"freeze", K_FREEZE},
      {"select", K_SELECT},
      {"icmp", K_ICMP},
      // FP binary ops
      {"fadd", K_FP_BINARY},
      {"fsub", K_FP_BINARY},
      {"fmul", K_FP_BINARY},
      {"fdiv", K_FP_BINARY},
      {"frem", K_FP_BINARY},
      // FP unary op
      {"fneg", K_FP_UNARY},
      // FP unary intrinsics (note: llvm.trunc is the FP rounding intrinsic,
      // distinct from the integer 'trunc' cast above)
      {"llvm.sqrt", K_FP_UNARY_INTRINSIC},
      {"llvm.sin", K_FP_UNARY_INTRINSIC},
      {"llvm.cos", K_FP_UNARY_INTRINSIC},
      {"llvm.exp", K_FP_UNARY_INTRINSIC},
      {"llvm.exp2", K_FP_UNARY_INTRINSIC},
      {"llvm.log", K_FP_UNARY_INTRINSIC},
      {"llvm.log10", K_FP_UNARY_INTRINSIC},
      {"llvm.log2", K_FP_UNARY_INTRINSIC},
      {"llvm.fabs", K_FP_UNARY_INTRINSIC},
      {"llvm.floor", K_FP_UNARY_INTRINSIC},
      {"llvm.ceil", K_FP_UNARY_INTRINSIC},
      {"llvm.round", K_FP_UNARY_INTRINSIC},
      {"llvm.rint", K_FP_UNARY_INTRINSIC},
      {"llvm.nearbyint", K_FP_UNARY_INTRINSIC},
      {"llvm.roundeven", K_FP_UNARY_INTRINSIC},
      {"llvm.canonicalize", K_FP_UNARY_INTRINSIC},
      {"llvm.trunc", K_FP_UNARY_INTRINSIC},
      // FP binary intrinsics
      {"llvm.pow", K_FP_BINARY_INTRINSIC},
      {"llvm.copysign", K_FP_BINARY_INTRINSIC},
      {"llvm.minnum", K_FP_BINARY_INTRINSIC},
      {"llvm.maxnum", K_FP_BINARY_INTRINSIC},
      {"llvm.minimum", K_FP_BINARY_INTRINSIC},
      {"llvm.maximum", K_FP_BINARY_INTRINSIC},
      {"llvm.minimumnum", K_FP_BINARY_INTRINSIC},
      {"llvm.maximumnum", K_FP_BINARY_INTRINSIC},
      // FP ternary intrinsics
      {"llvm.fma", K_FP_TERNARY_INTRINSIC},
      {"llvm.fmuladd", K_FP_TERNARY_INTRINSIC},
      // FP comparison
      {"fcmp", K_FCMP},
  };
  auto it = table.find(name);
  return it == table.end() ? K_UNSUPPORTED : it->second;
}

// =========================================================================
// Type helpers. Instantiates against scalar integer types ("i8", "i16",
// "i32", "i64") and scalar FP types ("float", "double"). Everything else
// is rejected at the per-rule type filter.
// =========================================================================

bool isInt(const std::string &t) {
  if (t.empty() || t[0] != 'i') return false;
  for (size_t i = 1; i < t.size(); ++i)
    if (!std::isdigit((unsigned char)t[i])) return false;
  return t.size() > 1;
}

int widthOf(const std::string &t) {
  if (!isInt(t)) return -1;
  return std::atoi(t.c_str() + 1);
}

bool isFP(const std::string &t) { return t == "float" || t == "double"; }

// Intrinsic name suffix: integer types map to "iN", FP to "f32"/"f64".
std::string intrinsicSuffix(const std::string &t) {
  if (isInt(t)) return t;
  if (t == "float") return "f32";
  if (t == "double") return "f64";
  return {};
}

bool isFPKind(Kind k) {
  switch (k) {
    case K_FP_BINARY:
    case K_FP_UNARY:
    case K_FP_UNARY_INTRINSIC:
    case K_FP_BINARY_INTRINSIC:
    case K_FP_TERNARY_INTRINSIC:
    case K_FCMP:
    case K_FP_CAST:
    case K_POWI_INTRINSIC:
    case K_FPCLASS_INTRINSIC:
    case K_VECTOR_REDUCE_FOLD:
      return true;
    default:
      return false;
  }
}

bool isFPVectorReduce(const std::string &inst_name) {
  return inst_name == "llvm.vector.reduce.fmax" ||
         inst_name == "llvm.vector.reduce.fmin" ||
         inst_name == "llvm.vector.reduce.fmaximum" ||
         inst_name == "llvm.vector.reduce.fminimum";
}

// Cast-like kinds carry distinct (in, out) types and use a hardcoded
// (src, dst) pair list (castPairs) instead of being instantiated at the
// requested-types T.
bool isCastKind(Kind k) {
  switch (k) {
    case K_CAST:
    case K_FP_INT_CAST:
    case K_FP_CAST:
    case K_FPTOI_SAT:
    case K_CMP_INTRINSIC:
    case K_BITCAST:
      return true;
    default:
      return false;
  }
}

bool isFastMathFlag(const std::string &f) {
  return f == "fast" || f == "nnan" || f == "ninf" || f == "nsz" ||
         f == "arcp" || f == "contract" || f == "afn" || f == "reassoc";
}

// FMF prefix for an FP instruction, derived from rule.flag. Trailing space
// included for direct concatenation; empty string when no FMF applies.
std::string fmfPrefix(const Rule &rule) {
  if (rule.flag.has_value() && isFastMathFlag(*rule.flag))
    return *rule.flag + " ";
  return {};
}

std::string typedZero(const std::string &ty) {
  if (ty == "ptr") return "null";
  if (!ty.empty() && ty[0] == '<') return "zeroinitializer";
  if (isFP(ty)) return "0.0";
  return "0";
}

// Number of value-bearing operand slots for this kind. Constant/immarg
// operands are not value slots.
int numValueSlots(Kind k) {
  switch (k) {
    case K_BINARY: return 2;
    case K_BINARY_INTRINSIC: return 2;
    case K_UNARY_INTRINSIC: return 1;
    case K_TERNARY_INTRINSIC: return 3;
    case K_INTRINSIC_WITH_FLAG: return 1;
    case K_OVERFLOW_INTRINSIC: return 2;
    case K_FIXED_POINT_INTRINSIC: return 2;  // a, b (scale is immarg)
    case K_CMP_INTRINSIC: return 2;
    case K_FREEZE: return 1;
    case K_SELECT: return 3;
    case K_ICMP: return 2;
    case K_CAST: return 1;
    case K_FP_BINARY: return 2;
    case K_FP_UNARY: return 1;
    case K_FP_UNARY_INTRINSIC: return 1;
    case K_FP_BINARY_INTRINSIC: return 2;
    case K_FP_TERNARY_INTRINSIC: return 3;
    case K_FCMP: return 2;
    case K_FP_INT_CAST: return 1;
    case K_FP_CAST: return 1;
    case K_POWI_INTRINSIC: return 2;          // Val (FP), power (i32)
    case K_FPCLASS_INTRINSIC: return 1;       // op (test is immarg)
    case K_FPTOI_SAT: return 1;
    case K_BITCAST: return 1;
    case K_GEP: return 2;                     // ptrval, idx
    case K_LOAD: return 1;                    // pointer
    case K_STORE: return 2;                   // value, pointer
    case K_MEMINTRINSIC: return 3;            // dest, src/val, len
    case K_EXTRACTELEMENT: return 2;          // vector, idx
    case K_INSERTELEMENT: return 3;           // vector, scalar elt, idx
    case K_VECTOR_REDUCE: return 1;           // <4 x T>
    case K_VECTOR_REDUCE_FOLD: return 2;      // start (T), <4 x T>
    default: return 0;
  }
}

// Type of value slot `slot`. Most kinds use `in_type` for every value
// slot; the exceptions are select.cond (always i1), cast ops whose
// single operand is the cast's source type (= in_type), and powi's
// power slot (always i32 regardless of the primary FP type).
std::string slotType(Kind k, const std::string &inst_name, int slot,
                     const std::string &in_type,
                     const std::string &out_type) {
  (void)out_type;
  if (k == K_SELECT && slot == 0) return "i1";
  if (k == K_POWI_INTRINSIC && slot == 1) return "i32";
  if (k == K_GEP) {
    if (slot == 0) return "ptr";
    if (slot == 1) return "i64";
  }
  if (k == K_LOAD) return "ptr";
  if (k == K_STORE) {
    if (slot == 0) return in_type;
    if (slot == 1) return "ptr";
  }
  if (k == K_MEMINTRINSIC) {
    if (slot == 0) return "ptr";
    if (slot == 1) {
      if (inst_name == "llvm.memset") return "i8";
      return "ptr";  // memcpy/memmove src
    }
    if (slot == 2) return "i64";
  }
  if (k == K_EXTRACTELEMENT) {
    if (slot == 0) return "<4 x " + in_type + ">";
    if (slot == 1) return "i32";
  }
  if (k == K_INSERTELEMENT) {
    if (slot == 0) return "<4 x " + in_type + ">";
    if (slot == 1) return in_type;
    if (slot == 2) return "i32";
  }
  if (k == K_VECTOR_REDUCE) return "<4 x " + in_type + ">";
  if (k == K_VECTOR_REDUCE_FOLD) {
    if (slot == 0) return in_type;
    if (slot == 1) return "<4 x " + in_type + ">";
  }
  return in_type;
}

// Result type of this instruction. For most kinds in_type == out_type
// and either works. K_CAST returns out_type (the cast destination).
// K_ICMP / K_FCMP / K_FPCLASS_INTRINSIC are always i1 regardless of the
// operand type. K_POWI_INTRINSIC returns the FP primary type (in_type).
// K_GEP returns ptr. K_LOAD returns the element type (in_type).
// K_STORE / K_MEMINTRINSIC have void as the textual result; composeEdge
// generates a load-back epilogue for K_STORE and emits ret void for
// K_MEMINTRINSIC, so the value we return here is only used for the SSA
// connection check on the source side (where these kinds never appear).
// K_VECTOR_REDUCE / K_VECTOR_REDUCE_FOLD return the element type.
// K_EXTRACTELEMENT returns the element type. K_INSERTELEMENT returns
// <4 x element>.
std::string resultType(Kind k, const std::string &in_type,
                       const std::string &out_type) {
  if (k == K_ICMP || k == K_FCMP || k == K_FPCLASS_INTRINSIC) return "i1";
  if (k == K_POWI_INTRINSIC) return in_type;
  if (k == K_GEP) return "ptr";
  if (k == K_LOAD) return in_type;
  if (k == K_STORE) return "void";
  if (k == K_MEMINTRINSIC) return "void";
  if (k == K_VECTOR_REDUCE || k == K_VECTOR_REDUCE_FOLD) return in_type;
  if (k == K_EXTRACTELEMENT) return in_type;
  if (k == K_INSERTELEMENT) return "<4 x " + in_type + ">";
  (void)in_type;
  return out_type;
}

// Map a rule operand index (the index reported by compat-matrix.json) to
// a value slot. Returns -1 when the operand is a non-value position.
//
// Most kinds use a 1-to-1 mapping. Two exceptions:
//   - cttz/ctlz/abs (K_INTRINSIC_WITH_FLAG): operand 0 is the integer
//     value, operand 1 is the i1 immarg flag (not a value).
//   - icmp (K_ICMP): the rule JSON lists three operands — cond
//     (condition-code keyword at index 0), op1 (index 1), op2 (index 2)
//     — so the two value slots are at operand indices 1 and 2, not 0
//     and 1. compat-matrix already discards target_operand=0 for icmp
//     because cond classifies as a keyword, so all icmp edges arrive
//     here with op_idx in {1, 2}.
int operandToSlot(Kind k, int op_idx) {
  if (k == K_INTRINSIC_WITH_FLAG) return op_idx == 0 ? 0 : -1;
  if (k == K_ICMP || k == K_FCMP) {
    if (op_idx == 1) return 0;
    if (op_idx == 2) return 1;
    return -1;
  }
  if (k == K_CAST || k == K_FP_INT_CAST || k == K_FP_CAST ||
      k == K_FPTOI_SAT || k == K_BITCAST)
    return op_idx == 0 ? 0 : -1;
  // Fixed-point intrinsics: operand 2 is the i32 immarg scale; only
  // operands 0 and 1 are value slots.
  if (k == K_FIXED_POINT_INTRINSIC) return op_idx < 2 ? op_idx : -1;
  // is.fpclass: operand 1 is the i32 immarg test mask.
  if (k == K_FPCLASS_INTRINSIC) return op_idx == 0 ? 0 : -1;
  // GEP rule operands: 0=ty (type, not a value), 1=ptrval, 2=indices.
  if (k == K_GEP) {
    if (op_idx == 1) return 0;
    if (op_idx == 2) return 1;
    return -1;
  }
  // Load rule operands: 0=ty (type), 1=pointer, 2=alignment, 3=ordering.
  if (k == K_LOAD) {
    if (op_idx == 1) return 0;
    return -1;
  }
  // Store rule operands: 0=value, 1=pointer, 2=alignment, 3=ordering.
  if (k == K_STORE) {
    if (op_idx == 0) return 0;
    if (op_idx == 1) return 1;
    return -1;
  }
  // Memcpy/move rule operands: 0=dest, 1=src, 2=len, 3=isvolatile.
  // Memset rule operands: 0=dest, 1=val, 2=len, 3=isvolatile.
  if (k == K_MEMINTRINSIC) {
    if (op_idx >= 0 && op_idx <= 2) return op_idx;
    return -1;  // 3=isvolatile is immarg
  }
  // extractelement: 0=val (vector), 1=idx.
  if (k == K_EXTRACTELEMENT) return op_idx < 2 ? op_idx : -1;
  // insertelement: 0=val (vector), 1=elt (scalar), 2=idx.
  if (k == K_INSERTELEMENT) return op_idx < 3 ? op_idx : -1;
  // Vector reductions: one value operand. fadd/fmul fold has 2 ops
  // (start_value=0, vector=1).
  if (k == K_VECTOR_REDUCE) return op_idx == 0 ? 0 : -1;
  if (k == K_VECTOR_REDUCE_FOLD) {
    if (op_idx == 0 || op_idx == 1) return op_idx;
    return -1;
  }
  int n = numValueSlots(k);
  return (op_idx >= 0 && op_idx < n) ? op_idx : -1;
}

// Whether the instruction's pattern can be instantiated at T (or, for
// casts, at the destination type T). FP kinds accept only float/double;
// integer kinds accept only integer types. Cross-type casts (FP↔int)
// accept either side — their (src, dst) pair list is the real filter.
bool supportsType(Kind k, const std::string &inst_name,
                  const std::string &T) {
  if (k == K_FP_INT_CAST || k == K_FPTOI_SAT || k == K_BITCAST)
    return isFP(T) || isInt(T);
  if (k == K_VECTOR_REDUCE) {
    return isFPVectorReduce(inst_name) ? isFP(T) : isInt(T);
  }
  // Memory kinds: T is the element type. Accept int and FP scalar
  // element types. For K_MEMINTRINSIC the T is mostly inert (slots are
  // ptr/i8/i64); accept any scalar so the connection check can match.
  if (k == K_GEP || k == K_LOAD || k == K_STORE || k == K_MEMINTRINSIC)
    return isInt(T) || isFP(T);
  if (k == K_EXTRACTELEMENT || k == K_INSERTELEMENT)
    return isInt(T) || isFP(T);
  if (isFPKind(k)) return isFP(T);
  if (!isInt(T)) return false;
  if (inst_name == "llvm.bswap") {
    int w = widthOf(T);
    return w > 0 && (w % 16 == 0);
  }
  // Casts: the (src, dst) pair list is the gating filter (see castPairs);
  // any int dst is acceptable here.
  return true;
}

// Preferred order of B's value slots for the control-edge composition.
// Matrix uses target_operand = -1 for control because the dependence is
// on the block guard rather than a specific operand; compose-gen picks
// a slot here, trying them in order until one has a type-compatible
// combo. div/rem put divisor (op2 / slot 1) first since "icmp ne 0" is
// the natural guard against div-by-zero. For select we prefer the
// value-arm slots over the i1 cond slot when A produces an integer —
// the caller's type-combo check handles that.
std::vector<int> controlSlotPriority(Kind tk, const std::string &tgt_name) {
  std::vector<int> out;
  int n = numValueSlots(tk);
  if (n <= 0) return out;
  auto push = [&](int s) {
    if (s < 0 || s >= n) return;
    for (int x : out) if (x == s) return;
    out.push_back(s);
  };
  if (tgt_name == "sdiv" || tgt_name == "udiv" ||
      tgt_name == "srem" || tgt_name == "urem" ||
      tgt_name == "llvm.sdiv.fix" || tgt_name == "llvm.udiv.fix" ||
      tgt_name == "llvm.sdiv.fix.sat" || tgt_name == "llvm.udiv.fix.sat")
    push(1);
  if (tk == K_SELECT) {
    push(1);
    push(2);
    push(0);
  }
  for (int s = 0; s < n; ++s) push(s);
  return out;
}

// Cast type pairs. Mirrors litmus-gen's hard-coded list so the two tools
// emit comparable IR shapes. Returned as (src, dst).
//
// For cmp/fp/sat kinds the pair is interpreted as (operand type,
// result type). scmp/ucmp pin the result at i8 per the brief.
const std::vector<std::pair<std::string, std::string>> &
castPairs(const std::string &inst_name) {
  static const std::vector<std::pair<std::string, std::string>> kTrunc = {
      {"i32", "i16"}, {"i32", "i8"}, {"i64", "i32"}, {"i16", "i8"},
  };
  static const std::vector<std::pair<std::string, std::string>> kExtend = {
      {"i16", "i32"}, {"i8", "i32"}, {"i32", "i64"}, {"i8", "i16"},
  };
  static const std::vector<std::pair<std::string, std::string>> kFpToInt = {
      {"float", "i32"}, {"double", "i64"},
  };
  static const std::vector<std::pair<std::string, std::string>> kIntToFp = {
      {"i32", "float"}, {"i64", "double"},
  };
  static const std::vector<std::pair<std::string, std::string>> kFpExt = {
      {"float", "double"},
  };
  static const std::vector<std::pair<std::string, std::string>> kFpTrunc = {
      {"double", "float"},
  };
  static const std::vector<std::pair<std::string, std::string>> kFpToISat = {
      {"float", "i32"}, {"double", "i64"}, {"float", "i64"}, {"double", "i32"},
  };
  // lround/lrint: result is i32 or i64 (platform "long").
  static const std::vector<std::pair<std::string, std::string>> kLround = {
      {"float", "i32"}, {"double", "i64"}, {"float", "i64"}, {"double", "i32"},
  };
  // llround/llrint: result is always i64.
  static const std::vector<std::pair<std::string, std::string>> kLLround = {
      {"float", "i64"}, {"double", "i64"},
  };
  static const std::vector<std::pair<std::string, std::string>> kCmpIntr = {
      {"i32", "i8"}, {"i64", "i8"},
  };
  // Bitcast: same bit-size between int and FP (and the symmetric pair).
  static const std::vector<std::pair<std::string, std::string>> kBitcast = {
      {"i32", "float"}, {"float", "i32"},
      {"i64", "double"}, {"double", "i64"},
  };
  static const std::vector<std::pair<std::string, std::string>> kEmpty;
  if (inst_name == "trunc") return kTrunc;
  if (inst_name == "zext" || inst_name == "sext") return kExtend;
  if (inst_name == "fptoui" || inst_name == "fptosi") return kFpToInt;
  if (inst_name == "uitofp" || inst_name == "sitofp") return kIntToFp;
  if (inst_name == "fpext") return kFpExt;
  if (inst_name == "fptrunc") return kFpTrunc;
  if (inst_name == "llvm.fptoui.sat" || inst_name == "llvm.fptosi.sat")
    return kFpToISat;
  if (inst_name == "llvm.lround" || inst_name == "llvm.lrint") return kLround;
  if (inst_name == "llvm.llround" || inst_name == "llvm.llrint") return kLLround;
  if (inst_name == "llvm.scmp" || inst_name == "llvm.ucmp") return kCmpIntr;
  if (inst_name == "bitcast") return kBitcast;
  return kEmpty;
}

// =========================================================================
// Per-rule filters. The matrix references rules that aren't applicable
// at scalar i32 (vector / pointer / FMF / aggregate / byte variants).
// Skip them here rather than producing bad IR.
// =========================================================================

bool ruleAppliesAtT(Kind k, const Rule &rule, const std::string &T) {
  (void)T;
  const std::string &id = rule.id;
  // Some new kinds have rule IDs that legitimately contain "vector" /
  // "pointer" — namely vector reductions / extract / insert (vector) and
  // load / store / gep / memintrinsic (pointer). Skip the wholesale id
  // filter for those.
  bool vector_kind = (k == K_VECTOR_REDUCE || k == K_VECTOR_REDUCE_FOLD ||
                      k == K_EXTRACTELEMENT || k == K_INSERTELEMENT);
  bool memory_kind = (k == K_GEP || k == K_LOAD || k == K_STORE ||
                      k == K_MEMINTRINSIC);
  if (!vector_kind && id.find("vector") != std::string::npos) return false;
  if (!memory_kind && id.find("pointer") != std::string::npos) return false;
  if (id.find("fmf") != std::string::npos) return false;
  if (id.find("byte") != std::string::npos) return false;
  if (id.find("aggregate") != std::string::npos) return false;
  // freeze.defined.pointer_nondereferenceable, etc. — already covered
  // by the pointer/aggregate filter above.
  if (k == K_SELECT) {
    // select's non-scalar shapes are filtered above; the FMF-only rule
    // is too.
    if (rule.shape == "vector_elementwise" || rule.shape == "vector_broadcast")
      return false;
  }
  if (k == K_CAST) {
    // .from_i1 needs i1 as the cast source; our pair list doesn't include
    // i1, so the type filter would already drop these. Skip explicitly.
    if (id.find(".from_i1") != std::string::npos) return false;
  }
  // Memory-op rules with metadata flags (!nonnull, !align, !noundef,
  // !invariant.load) — we don't emit metadata so these can't be
  // distinguished from the base case. Skip rather than silently
  // collapsing them onto base.
  if (k == K_LOAD || k == K_STORE) {
    if (rule.flag.has_value()) {
      const std::string &f = *rule.flag;
      if (!f.empty() && f[0] == '!') return false;
      if (f == "volatile" || f == "atomic") return false;
    }
  }
  if (k == K_GEP) {
    // inrange is restricted to constant GEP expressions; not applicable
    // to runtime GEP instructions.
    if (rule.flag.has_value() && *rule.flag == "inrange") return false;
  }
  return true;
}

// Predicate for icmp rules. Returns the LLVM predicate string and the
// samesign flag, or empty pred on rules we don't emit (pointer).
struct IcmpPred {
  std::string pred;
  bool samesign = false;
};
IcmpPred icmpPredFromRule(const Rule &rule) {
  IcmpPred p;
  const std::string &id = rule.id;
  if (id == "icmp.defined.eq") p.pred = "eq";
  else if (id == "icmp.defined.ne") p.pred = "ne";
  else if (id == "icmp.defined.signed") p.pred = "slt";
  else if (id == "icmp.defined.unsigned") p.pred = "ult";
  else if (id == "icmp.poison.samesign") { p.pred = "slt"; p.samesign = true; }
  // icmp.defined.pointer: leave empty -> caller skips
  return p;
}

// Predicate for fcmp rules. The brief specifies "one" as the default
// ordered predicate; "une" mirrors it on the unordered side. The literal
// "false"/"true" predicates are first-class in LLVM IR.
std::string fcmpPredFromRule(const Rule &rule) {
  const std::string &id = rule.id;
  if (id == "fcmp.defined.false") return "false";
  if (id == "fcmp.defined.true") return "true";
  if (id == "fcmp.defined.ordered") return "one";
  if (id == "fcmp.defined.unordered") return "une";
  return {};
}

// For K_INTRINSIC_WITH_FLAG (cttz/ctlz/abs), pick the i1 immarg value
// from the rule. param_flag_poison shapes say "the flag is on", named
// rules carry the explicit case in their id.
bool intrinsicFlagValue(const Rule &rule) {
  const std::string &id = rule.id;
  if (rule.shape == "param_flag_poison") return true;
  if (id.find("is_zero_poison") != std::string::npos) return true;
  if (id.find("int_min") != std::string::npos &&
      id.find("no_poison") == std::string::npos)
    return true;
  return false;
}

// =========================================================================
// Instruction emission. Each kind produces one IR line (the call /
// instruction itself); intrinsic kinds also push their `declare` line
// into the shared module-level declaration set so we don't duplicate.
// =========================================================================

std::string emitInstruction(Kind k, const std::string &inst_name,
                             const Rule &rule,
                             const std::string &result_var,
                             const std::string &in_type,
                             const std::string &out_type,
                             const std::vector<std::string> &slot_vars,
                             std::set<std::string> &decls) {
  // All kinds except K_CAST have in_type == out_type (and emit only one
  // type). K_CAST is the only kind where the two differ.
  const std::string &T = in_type;
  switch (k) {
    case K_BINARY_INTRINSIC: {
      const std::string call = std::string("@") + inst_name + "." + T;
      decls.insert("declare " + T + " " + call + "(" + T + ", " + T + ")");
      return result_var + " = call " + T + " " + call + "(" + T + " " +
             slot_vars[0] + ", " + T + " " + slot_vars[1] + ")";
    }
    case K_UNARY_INTRINSIC: {
      const std::string call = std::string("@") + inst_name + "." + T;
      decls.insert("declare " + T + " " + call + "(" + T + ")");
      return result_var + " = call " + T + " " + call + "(" + T + " " +
             slot_vars[0] + ")";
    }
    case K_TERNARY_INTRINSIC: {
      const std::string call = std::string("@") + inst_name + "." + T;
      decls.insert("declare " + T + " " + call + "(" + T + ", " + T + ", " +
                   T + ")");
      return result_var + " = call " + T + " " + call + "(" + T + " " +
             slot_vars[0] + ", " + T + " " + slot_vars[1] + ", " + T + " " +
             slot_vars[2] + ")";
    }
    case K_INTRINSIC_WITH_FLAG: {
      bool fv = intrinsicFlagValue(rule);
      const std::string call = std::string("@") + inst_name + "." + T;
      decls.insert("declare " + T + " " + call + "(" + T + ", i1)");
      return result_var + " = call " + T + " " + call + "(" + T + " " +
             slot_vars[0] + ", i1 " + (fv ? "true" : "false") + ")";
    }
    case K_OVERFLOW_INTRINSIC: {
      // Two-line emission: the struct-returning call, then extractvalue 0
      // to expose the iN result on `result_var`. The struct itself lives
      // on `result_var + "_res"`, which composeEdge reaches into for the
      // overflow-bit extract on control edges. result_var is "%a" or "%b"
      // (with leading %), so res_var is "%a_res" / "%b_res".
      const std::string call = std::string("@") + inst_name + "." + T;
      decls.insert("declare {" + T + ", i1} " + call + "(" + T + ", " + T + ")");
      const std::string res_var = result_var + "_res";
      return res_var + " = call {" + T + ", i1} " + call + "(" + T + " " +
             slot_vars[0] + ", " + T + " " + slot_vars[1] + ")\n  " +
             result_var + " = extractvalue {" + T + ", i1} " + res_var + ", 0";
    }
    case K_FIXED_POINT_INTRINSIC: {
      // Two iN value operands plus a constant i32 scale=0 immarg.
      if (!isInt(T)) return {};
      const std::string call = std::string("@") + inst_name + "." + T;
      decls.insert("declare " + T + " " + call + "(" + T + ", " + T +
                   ", i32 immarg)");
      return result_var + " = call " + T + " " + call + "(" + T + " " +
             slot_vars[0] + ", " + T + " " + slot_vars[1] + ", i32 0)";
    }
    case K_CMP_INTRINSIC: {
      // Two-suffix mangling: result type then operand type.
      // Example: @llvm.scmp.i8.i32(i32 %x, i32 %y) -> i8.
      if (!isInt(in_type) || !isInt(out_type)) return {};
      const std::string call =
          std::string("@") + inst_name + "." + out_type + "." + in_type;
      decls.insert("declare " + out_type + " " + call + "(" + in_type + ", " +
                   in_type + ")");
      return result_var + " = call " + out_type + " " + call + "(" + in_type +
             " " + slot_vars[0] + ", " + in_type + " " + slot_vars[1] + ")";
    }
    case K_FREEZE:
      return result_var + " = freeze " + T + " " + slot_vars[0];
    case K_SELECT:
      return result_var + " = select i1 " + slot_vars[0] + ", " + T + " " +
             slot_vars[1] + ", " + T + " " + slot_vars[2];
    case K_ICMP: {
      IcmpPred p = icmpPredFromRule(rule);
      if (p.pred.empty()) return {};
      std::string flag_str = p.samesign ? "samesign " : "";
      return result_var + " = icmp " + flag_str + p.pred + " " + T + " " +
             slot_vars[0] + ", " + slot_vars[1];
    }
    case K_CAST: {
      std::string flag_str;
      if (rule.flag.has_value()) {
        const std::string &f = *rule.flag;
        if (f == "nuw" || f == "nsw") {
          if (inst_name != "trunc") return {};
          flag_str = f + " ";
        } else if (f == "nneg") {
          if (inst_name != "zext") return {};
          flag_str = "nneg ";
        } else {
          return {};
        }
      }
      // Cast pair list never starts at i1; if the matrix sends one through
      // anyway it's a bug, but bail out rather than emit invalid IR.
      if (widthOf(in_type) <= 0 || widthOf(out_type) <= 0) return {};
      if (in_type == out_type) return {};
      return result_var + " = " + inst_name + " " + flag_str + in_type + " " +
             slot_vars[0] + " to " + out_type;
    }
    case K_FP_BINARY: {
      // fadd / fsub / fmul / fdiv / frem.
      return result_var + " = " + inst_name + " " + fmfPrefix(rule) + T + " " +
             slot_vars[0] + ", " + slot_vars[1];
    }
    case K_FP_UNARY: {
      // fneg.
      return result_var + " = " + inst_name + " " + fmfPrefix(rule) + T + " " +
             slot_vars[0];
    }
    case K_FP_UNARY_INTRINSIC: {
      const std::string suffix = intrinsicSuffix(T);
      if (suffix.empty()) return {};
      const std::string call = std::string("@") + inst_name + "." + suffix;
      decls.insert("declare " + T + " " + call + "(" + T + ")");
      return result_var + " = call " + fmfPrefix(rule) + T + " " + call + "(" +
             T + " " + slot_vars[0] + ")";
    }
    case K_FP_BINARY_INTRINSIC: {
      const std::string suffix = intrinsicSuffix(T);
      if (suffix.empty()) return {};
      const std::string call = std::string("@") + inst_name + "." + suffix;
      decls.insert("declare " + T + " " + call + "(" + T + ", " + T + ")");
      return result_var + " = call " + fmfPrefix(rule) + T + " " + call + "(" +
             T + " " + slot_vars[0] + ", " + T + " " + slot_vars[1] + ")";
    }
    case K_FP_TERNARY_INTRINSIC: {
      const std::string suffix = intrinsicSuffix(T);
      if (suffix.empty()) return {};
      const std::string call = std::string("@") + inst_name + "." + suffix;
      decls.insert("declare " + T + " " + call + "(" + T + ", " + T + ", " +
                   T + ")");
      return result_var + " = call " + fmfPrefix(rule) + T + " " + call + "(" +
             T + " " + slot_vars[0] + ", " + T + " " + slot_vars[1] + ", " +
             T + " " + slot_vars[2] + ")";
    }
    case K_FCMP: {
      const std::string pred = fcmpPredFromRule(rule);
      if (pred.empty()) return {};
      return result_var + " = fcmp " + fmfPrefix(rule) + pred + " " + T + " " +
             slot_vars[0] + ", " + slot_vars[1];
    }
    case K_FP_INT_CAST: {
      // fptoui / fptosi / uitofp / sitofp. Cross-type cast. Only uitofp
      // accepts the nneg flag (added in LLVM 17). The other directions
      // have no recognized flag in our rules; reject any unexpected flag
      // so we don't silently emit invalid IR.
      std::string flag_str;
      if (rule.flag.has_value()) {
        const std::string &f = *rule.flag;
        if (f == "nneg") {
          if (inst_name != "uitofp") return {};
          flag_str = "nneg ";
        } else {
          return {};
        }
      }
      if (in_type == out_type) return {};
      return result_var + " = " + inst_name + " " + flag_str + in_type + " " +
             slot_vars[0] + " to " + out_type;
    }
    case K_FP_CAST: {
      // fpext / fptrunc. The cast pair list already enforces the direction.
      if (in_type == out_type) return {};
      return result_var + " = " + inst_name + " " + in_type + " " +
             slot_vars[0] + " to " + out_type;
    }
    case K_POWI_INTRINSIC: {
      // (FP Val, i32 power) -> FP. Mangling: @llvm.powi.<fpsuffix>.i32.
      const std::string suffix = intrinsicSuffix(T);
      if (suffix.empty()) return {};
      const std::string call =
          std::string("@") + inst_name + "." + suffix + ".i32";
      decls.insert("declare " + T + " " + call + "(" + T + ", i32)");
      return result_var + " = call " + T + " " + call + "(" + T + " " +
             slot_vars[0] + ", i32 " + slot_vars[1] + ")";
    }
    case K_FPCLASS_INTRINSIC: {
      // (FP, i32 immarg test) -> i1. Use mask 783 (fcNormal | fcSubnormal |
      // fcZero) as the default test.
      const std::string suffix = intrinsicSuffix(T);
      if (suffix.empty()) return {};
      const std::string call = std::string("@") + inst_name + "." + suffix;
      decls.insert("declare i1 " + call + "(" + T + ", i32 immarg)");
      return result_var + " = call i1 " + call + "(" + T + " " + slot_vars[0] +
             ", i32 783)";
    }
    case K_FPTOI_SAT: {
      // Two-suffix mangling: result int type then source FP type.
      // Example: @llvm.fptoui.sat.i32.f32(float %x) -> i32.
      // Shared with llvm.lround/llvm.llround/llvm.lrint/llvm.llrint, which
      // use the same (result, source) mangling convention.
      const std::string src_suffix = intrinsicSuffix(in_type);
      if (src_suffix.empty() || !isInt(out_type)) return {};
      const std::string call = std::string("@") + inst_name + "." + out_type +
                               "." + src_suffix;
      decls.insert("declare " + out_type + " " + call + "(" + in_type + ")");
      return result_var + " = call " + out_type + " " + call + "(" + in_type +
             " " + slot_vars[0] + ")";
    }
    case K_BITCAST: {
      // Int <-> FP same-size bitcast. No flags. Pointer<->pointer bitcasts
      // are invalid under opaque pointers (LLVM 17+).
      if (rule.flag.has_value()) return {};
      if (in_type == out_type) return {};
      return result_var + " = bitcast " + in_type + " " + slot_vars[0] +
             " to " + out_type;
    }
    case K_GEP: {
      // getelementptr [flag] T, ptr %base, i64 %idx. Single-index form.
      // Flags supported here: inbounds, nuw, nusw. inrange is constant-
      // only and filtered upstream.
      std::string flag_str;
      if (rule.flag.has_value()) {
        const std::string &f = *rule.flag;
        if (f == "inbounds") flag_str = "inbounds ";
        else if (f == "nuw") flag_str = "nuw ";
        else if (f == "nusw") flag_str = "nusw ";
        else return {};
      }
      return result_var + " = getelementptr " + flag_str + T + ", ptr " +
             slot_vars[0] + ", i64 " + slot_vars[1];
    }
    case K_LOAD: {
      // load T, ptr %p. Metadata flags filtered upstream.
      return result_var + " = load " + T + ", ptr " + slot_vars[0];
    }
    case K_STORE: {
      // Void-producing. Emit the store; composeEdge appends the load-back
      // and ret on the target side.
      return std::string("store ") + T + " " + slot_vars[0] + ", ptr " +
             slot_vars[1];
    }
    case K_MEMINTRINSIC: {
      // memcpy/memmove signature: (ptr dest, ptr src, i64 len, i1 immarg).
      // memset signature: (ptr dest, i8 val, i64 len, i1 immarg).
      // Mangling: @llvm.memcpy.p0.p0.i64 / @llvm.memset.p0.i64.
      bool is_set = (inst_name == "llvm.memset");
      std::string sig_op1 = is_set ? "i8" : "ptr";
      std::string mangle = is_set ? ".p0.i64" : ".p0.p0.i64";
      const std::string call = std::string("@") + inst_name + mangle;
      decls.insert("declare void " + call + "(ptr, " + sig_op1 +
                   ", i64, i1 immarg)");
      return std::string("call void ") + call + "(ptr " + slot_vars[0] +
             ", " + sig_op1 + " " + slot_vars[1] + ", i64 " + slot_vars[2] +
             ", i1 false)";
    }
    case K_EXTRACTELEMENT: {
      // %a = extractelement <4 x T> %v, i32 %idx
      if (!isInt(T) && !isFP(T)) return {};
      const std::string vt = "<4 x " + T + ">";
      return result_var + " = extractelement " + vt + " " + slot_vars[0] +
             ", i32 " + slot_vars[1];
    }
    case K_INSERTELEMENT: {
      // %a = insertelement <4 x T> %v, T %elt, i32 %idx
      if (!isInt(T) && !isFP(T)) return {};
      const std::string vt = "<4 x " + T + ">";
      return result_var + " = insertelement " + vt + " " + slot_vars[0] +
             ", " + T + " " + slot_vars[1] + ", i32 " + slot_vars[2];
    }
    case K_VECTOR_REDUCE: {
      // (<4 x T>) -> T. Mangling suffix: v4<elem-suffix>. Int reductions
      // do not carry FMF; FP reductions (fmax/fmin/fmaximum/fminimum) do.
      const std::string elem_suffix = intrinsicSuffix(T);
      if (elem_suffix.empty()) return {};
      const std::string vt = "<4 x " + T + ">";
      const std::string suffix = "v4" + elem_suffix;
      const std::string call = std::string("@") + inst_name + "." + suffix;
      decls.insert("declare " + T + " " + call + "(" + vt + ")");
      std::string fmf = isFPVectorReduce(inst_name) ? fmfPrefix(rule) : "";
      return result_var + " = call " + fmf + T + " " + call + "(" + vt +
             " " + slot_vars[0] + ")";
    }
    case K_VECTOR_REDUCE_FOLD: {
      // (T start, <4 x T>) -> T. fadd/fmul. FMF allowed.
      const std::string elem_suffix = intrinsicSuffix(T);
      if (elem_suffix.empty()) return {};
      const std::string vt = "<4 x " + T + ">";
      const std::string suffix = "v4" + elem_suffix;
      const std::string call = std::string("@") + inst_name + "." + suffix;
      decls.insert("declare " + T + " " + call + "(" + T + ", " + vt + ")");
      return result_var + " = call " + fmfPrefix(rule) + T + " " + call +
             "(" + T + " " + slot_vars[0] + ", " + vt + " " + slot_vars[1] +
             ")";
    }
    default:
      return {};
  }
}

// =========================================================================
// Composition. For an edge (A, B, target_operand) at type T, build a
// module text that pipes A's result into B's operand[target_operand]
// either directly (SSA) or through alloca+store+load (memory).
// =========================================================================

std::string varName(int i) {
  // Pick distinct, short parameter names. Pre-canned letters cover all
  // realistic V1 arities (max is select-fed-by-select: 1 + 2 = 3 params).
  static const char *names[] = {"x", "y", "z", "w", "u", "v"};
  size_t nn = sizeof(names) / sizeof(names[0]);
  if (i >= 0 && (size_t)i < nn) return std::string("%") + names[i];
  return std::string("%p") + std::to_string(i);
}

struct ComposeResult {
  std::string body;
  std::string fn_name;
};

// Per-side instantiation. For non-cast instructions in == out == T. For
// casts (in, out) are the (src, dst) of the cast pair. The connection
// constraint A.out == B's target-slot type is enforced by the caller's
// type-combo enumeration; we still re-check here so an unexpected combo
// fails closed.
struct SideType {
  std::string in;
  std::string out;
};

std::optional<ComposeResult>
composeEdge(const Rule &src_rule, const Rule &tgt_rule, int target_slot,
            const std::string &edge_type, const SideType &a, const SideType &b,
            const std::string &fn_name) {
  Kind sk = kindOf(src_rule.instruction);
  Kind tk = kindOf(tgt_rule.instruction);
  if (sk == K_UNSUPPORTED || tk == K_UNSUPPORTED) return std::nullopt;

  // Void-producing kinds and the insertelement target-only kind can't
  // serve as the source. The matrix should already exclude them (no
  // storable result), but reject defensively so an unexpected edge
  // fails closed.
  if (sk == K_STORE || sk == K_MEMINTRINSIC || sk == K_INSERTELEMENT)
    return std::nullopt;

  // For non-cast kinds, supportsType only sees one type. For casts, both
  // src and dst must satisfy whatever per-kind width constraints apply.
  if (!supportsType(sk, src_rule.instruction, a.in)) return std::nullopt;
  if (!supportsType(sk, src_rule.instruction, a.out)) return std::nullopt;
  if (!supportsType(tk, tgt_rule.instruction, b.in)) return std::nullopt;
  if (!supportsType(tk, tgt_rule.instruction, b.out)) return std::nullopt;

  if (!ruleAppliesAtT(sk, src_rule, a.in)) return std::nullopt;
  if (!ruleAppliesAtT(tk, tgt_rule, b.in)) return std::nullopt;

  if (target_slot < 0 || target_slot >= numValueSlots(tk)) return std::nullopt;

  // Type match at the connection point. K_GEP target with the index slot
  // is special: A's int can be any width; we sext to i64 at compose time.
  const std::string src_result = resultType(sk, a.in, a.out);
  const std::string tgt_slot_t =
      slotType(tk, tgt_rule.instruction, target_slot, b.in, b.out);
  const bool gep_idx_sext =
      (tk == K_GEP && target_slot == 1 && isInt(src_result) &&
       widthOf(src_result) > 0 && widthOf(src_result) < 64);
  if (!gep_idx_sext && src_result != tgt_slot_t) return std::nullopt;

  const int n_src_slots = numValueSlots(sk);
  const int n_tgt_slots = numValueSlots(tk);

  // Build the parameter list and slot variables. Source fills slots 0..k-1
  // from %x, %y, %z; target reuses one slot with A's result and fills the
  // rest from the next parameters.
  std::vector<std::string> param_types;
  std::vector<std::string> param_vars;
  std::vector<std::string> src_slots(n_src_slots);
  std::vector<std::string> tgt_slots(n_tgt_slots);
  int next_p = 0;
  for (int i = 0; i < n_src_slots; ++i) {
    src_slots[i] = varName(next_p++);
    param_types.push_back(slotType(sk, src_rule.instruction, i, a.in, a.out));
    param_vars.push_back(src_slots[i]);
  }
  // The variable holding A's result that target B will consume. For
  // memory edges A's value is round-tripped through alloca+store+load
  // (%ld); for SSA and control edges it stays as %a. For the GEP-index
  // sext path the index slot receives %a_ext instead.
  const std::string a_value = (edge_type == "memory") ? "%ld" : "%a";
  for (int i = 0; i < n_tgt_slots; ++i) {
    if (i == target_slot) {
      tgt_slots[i] = gep_idx_sext ? "%a_ext" : a_value;
    } else {
      tgt_slots[i] = varName(next_p++);
      param_types.push_back(
          slotType(tk, tgt_rule.instruction, i, b.in, b.out));
      param_vars.push_back(tgt_slots[i]);
    }
  }

  std::set<std::string> decls;
  std::string a_insn = emitInstruction(sk, src_rule.instruction, src_rule,
                                        "%a", a.in, a.out, src_slots, decls);
  if (a_insn.empty()) return std::nullopt;
  std::string b_insn = emitInstruction(tk, tgt_rule.instruction, tgt_rule,
                                        "%b", b.in, b.out, tgt_slots, decls);
  if (b_insn.empty()) return std::nullopt;

  // Determine the function return type and the variable holding the
  // returned value. For K_STORE and K_GEP targets we synthesize a
  // load-back of the element type so the composition has something to
  // return; K_MEMINTRINSIC returns void.
  std::string ret_type;
  std::string ret_var;
  std::string epilogue;
  if (tk == K_STORE) {
    ret_type = b.in;
    ret_var = "%val";
    // tgt_slots[1] is the store pointer (a parameter, or %ld for memory
    // edge — but K_STORE as target with target_slot=1 would be feeding
    // the pointer from A; we keep the load-back tied to whichever slot
    // ends up holding the pointer).
    const std::string &storeptr = tgt_slots[1];
    epilogue = "%val = load " + b.in + ", ptr " + storeptr;
  } else if (tk == K_GEP) {
    ret_type = b.in;
    ret_var = "%val";
    epilogue = "%val = load " + b.in + ", ptr %b";
  } else if (tk == K_MEMINTRINSIC) {
    ret_type = "void";
    ret_var = "";
  } else {
    ret_type = resultType(tk, b.in, b.out);
    ret_var = "%b";
  }

  // GEP-target sext line: convert A's narrower int to i64.
  std::string sext_line;
  if (gep_idx_sext) {
    sext_line = "%a_ext = sext " + src_result + " " + a_value + " to i64";
  }

  // Helper: emit the return instruction. For void return, no value.
  auto emitRet = [&](std::ostream &out, const std::string &val) {
    if (ret_type == "void") {
      out << "  ret void\n";
    } else {
      out << "  ret " << ret_type << " " << val << "\n";
    }
  };

  // Memory edge stores/loads A's result type. ptr-typed A (e.g. K_GEP)
  // round-trips through `alloca ptr`.
  const std::string &mem_t = src_result;

  std::ostringstream os;
  for (const auto &d : decls) os << d << "\n";
  if (!decls.empty()) os << "\n";
  os << "define " << ret_type << " @" << fn_name << "(";
  for (size_t i = 0; i < param_types.size(); ++i) {
    if (i) os << ", ";
    os << param_types[i] << " " << param_vars[i];
  }
  os << ") {\n";
  if (edge_type == "memory") {
    os << "  %p = alloca " << mem_t << "\n";
    os << "  " << a_insn << "\n";
    os << "  store " << mem_t << " %a, ptr %p\n";
    os << "  %ld = load " << mem_t << ", ptr %p\n";
    if (!sext_line.empty()) os << "  " << sext_line << "\n";
    os << "  " << b_insn << "\n";
    if (!epilogue.empty()) os << "  " << epilogue << "\n";
    emitRet(os, ret_var);
  } else if (edge_type == "control") {
    // Multi-BB: A's value gates B's execution via a comparison + br.
    // In the then branch %a is non-zero (and not poison, since poison
    // through icmp+br would be UB at the branch). B can still use %a in
    // its target slot because entry dominates then. FP sources use
    // "fcmp une <T> %a, 0.0" instead of icmp; ptr sources use
    // "icmp ne ptr %a, null". K_OVERFLOW_INTRINSIC has its own bit in
    // the struct — extract and branch on it directly so the optimizer
    // can infer no-overflow in the taken branch.
    const std::string &a_t = src_result;
    os << "entry:\n";
    os << "  " << a_insn << "\n";
    if (sk == K_OVERFLOW_INTRINSIC) {
      os << "  %a_obit = extractvalue {" << a_t << ", i1} %a_res, 1\n";
      os << "  br i1 %a_obit, label %overflow, label %normal\n";
      os << "normal:\n";
      if (!sext_line.empty()) os << "  " << sext_line << "\n";
      os << "  " << b_insn << "\n";
      if (!epilogue.empty()) os << "  " << epilogue << "\n";
      emitRet(os, ret_var);
      os << "overflow:\n";
      emitRet(os, ret_type == "void" ? "" : typedZero(ret_type));
    } else {
      if (a_t == "i1") {
        // %a is already i1; skip the redundant comparison.
        os << "  br i1 %a, label %then, label %else\n";
      } else if (isFP(a_t)) {
        os << "  %cond = fcmp une " << a_t << " %a, 0.0\n";
        os << "  br i1 %cond, label %then, label %else\n";
      } else if (a_t == "ptr") {
        os << "  %cond = icmp ne ptr %a, null\n";
        os << "  br i1 %cond, label %then, label %else\n";
      } else {
        os << "  %cond = icmp ne " << a_t << " %a, 0\n";
        os << "  br i1 %cond, label %then, label %else\n";
      }
      os << "then:\n";
      if (!sext_line.empty()) os << "  " << sext_line << "\n";
      os << "  " << b_insn << "\n";
      if (!epilogue.empty()) os << "  " << epilogue << "\n";
      emitRet(os, ret_var);
      os << "else:\n";
      emitRet(os, ret_type == "void" ? "" : typedZero(ret_type));
    }
  } else {
    os << "  " << a_insn << "\n";
    if (!sext_line.empty()) os << "  " << sext_line << "\n";
    os << "  " << b_insn << "\n";
    if (!epilogue.empty()) os << "  " << epilogue << "\n";
    emitRet(os, ret_var);
  }
  os << "}\n";

  return ComposeResult{os.str(), fn_name};
}

// =========================================================================
// File naming.
// =========================================================================

// Sanitize a string for use as both a file basename and an LLVM
// identifier: alnum + underscore, everything else becomes underscore.
std::string sanitize(std::string s) {
  for (char &c : s) {
    if (!std::isalnum((unsigned char)c) && c != '_') c = '_';
  }
  return s;
}

// Trim a rule ID's leading instruction prefix when possible. We keep the
// full id for now to avoid ambiguity between sibling rules.
std::string ruleSlug(const std::string &id) { return sanitize(id); }

// Pipeline type slug: collapses adjacent duplicates in the sequence
// (a.in, a.out, b.out). a.out == b.in by the connection constraint, so it
// appears only once. Examples: ("i32","i32","i32","i32") -> "i32";
// ("i32","i16","i16","i16") -> "i32_i16" (cast A, non-cast B);
// ("i32","i16","i16","i32") -> "i32_i16_i32" (round-trip).
std::string typeSlug(const SideType &a, const SideType &b) {
  std::vector<std::string> seq = {a.in, a.out, b.out};
  std::string out;
  for (size_t i = 0; i < seq.size(); ++i) {
    if (i == 0 || seq[i] != seq[i - 1]) {
      if (!out.empty()) out += "_";
      out += seq[i];
    }
  }
  return out;
}

std::string baseName(const Rule &src, const Rule &tgt, int target_operand,
                     const std::string &edge_type, const SideType &a,
                     const SideType &b) {
  std::string out = "compose__";
  out += ruleSlug(src.id);
  out += "__";
  out += ruleSlug(tgt.id);
  if (edge_type == "ssa") {
    out += "_op" + std::to_string(target_operand);
  }
  out += "__" + edge_type;
  out += "__" + typeSlug(a, b);
  return out;
}

// =========================================================================
// File I/O helpers.
// =========================================================================

std::string readFile(const std::string &path) {
  std::ifstream f(path);
  if (!f) throw std::runtime_error("cannot open " + path);
  std::ostringstream os;
  os << f.rdbuf();
  return os.str();
}

void writeFile(const std::string &path, const std::string &body) {
  std::ofstream f(path);
  if (!f) throw std::runtime_error("cannot open for write: " + path);
  f << body;
}

bool ensureDir(const std::string &path) {
  if (path.empty()) return false;
  std::string acc;
  for (size_t i = 0; i <= path.size(); ++i) {
    if (i == path.size() || path[i] == '/') {
      if (!acc.empty() && acc != ".") {
        if (mkdir(acc.c_str(), 0755) != 0 && errno != EEXIST) return false;
      }
      if (i < path.size()) acc.push_back('/');
    } else {
      acc.push_back(path[i]);
    }
  }
  return true;
}

// =========================================================================
// Verification via `opt -O2 -disable-output -`. Pipe IR through stdin,
// capture stderr for the failure log. Each call forks once; this is the
// dominant cost of compose-gen, so we keep the IR pipe simple (one
// process per module).
// =========================================================================

bool verifyByOpt(const std::string &opt_path, const std::string &ir,
                 std::string &err_msg) {
  int in_pipe[2], err_pipe[2];
  if (pipe(in_pipe) != 0) return false;
  if (pipe(err_pipe) != 0) {
    close(in_pipe[0]); close(in_pipe[1]);
    return false;
  }
  pid_t pid = fork();
  if (pid < 0) {
    close(in_pipe[0]); close(in_pipe[1]);
    close(err_pipe[0]); close(err_pipe[1]);
    return false;
  }
  if (pid == 0) {
    dup2(in_pipe[0], 0);
    dup2(err_pipe[1], 2);
    close(in_pipe[1]);
    close(err_pipe[0]);
    int devnull = open("/dev/null", O_WRONLY);
    if (devnull >= 0) {
      dup2(devnull, 1);
      close(devnull);
    }
    execl(opt_path.c_str(), opt_path.c_str(), "-O2", "-disable-output", "-",
          (char *)nullptr);
    _exit(127);
  }
  close(in_pipe[0]);
  close(err_pipe[1]);
  size_t off = 0;
  while (off < ir.size()) {
    ssize_t n = write(in_pipe[1], ir.data() + off, ir.size() - off);
    if (n < 0) {
      if (errno == EINTR) continue;
      break;
    }
    off += (size_t)n;
  }
  close(in_pipe[1]);
  err_msg.clear();
  char buf[4096];
  ssize_t n;
  while ((n = read(err_pipe[0], buf, sizeof(buf))) > 0)
    err_msg.append(buf, buf + n);
  close(err_pipe[0]);
  int status = 0;
  while (waitpid(pid, &status, 0) < 0 && errno == EINTR) {
  }
  return WIFEXITED(status) && WEXITSTATUS(status) == 0;
}

// =========================================================================
// CLI.
// =========================================================================

std::vector<std::string> splitCsv(const std::string &s) {
  std::vector<std::string> out;
  size_t start = 0;
  for (size_t i = 0; i <= s.size(); ++i) {
    if (i == s.size() || s[i] == ',') {
      if (i > start) out.emplace_back(s.substr(start, i - start));
      start = i + 1;
    }
  }
  return out;
}

struct Options {
  std::string matrix_path;
  std::vector<std::string> rules_paths;
  std::string output_dir;
  std::vector<std::string> edge_types{"ssa", "memory", "control"};
  std::vector<std::string> types{"i8", "i16", "i32", "i64", "float", "double"};
  std::string opt_path = "deps/llvm-build-plain/bin/opt";
  bool no_verify = false;
  size_t max_failure_log = 20;
};

// Enumerate all (A.in, A.out, B.in, B.out) combinations that satisfy the
// per-side cast pair constraint and the connection constraint
// (a.out == B's type at target slot). For non-cast sides, types come from
// the user's --types filter. For cast sides, types come from the
// hard-coded pair list, regardless of --types (matches litmus-gen).
std::vector<std::pair<SideType, SideType>>
typeCombosForEdge(Kind sk, const std::string &src_name, Kind tk,
                  const std::string &tgt_name, int target_slot,
                  const std::set<std::string> &requested_types) {
  std::vector<std::pair<SideType, SideType>> out;
  const bool a_cast = isCastKind(sk);
  const bool b_cast = isCastKind(tk);

  auto tgtSlotType = [&](const std::string &b_in, const std::string &b_out) {
    return slotType(tk, tgt_name, target_slot, b_in, b_out);
  };
  auto srcResultType = [&](const std::string &a_in, const std::string &a_out) {
    return resultType(sk, a_in, a_out);
  };

  // Special-case: K_POWI's power slot is i32 regardless of B's primary FP
  // type. The standard enumeration (which forces B.in == A.out) breaks
  // because powi needs an FP primary type, so we can't set B.in = i32.
  // Enumerate B at its FP types and A at whatever produces i32.
  if (tk == K_POWI_INTRINSIC && target_slot == 1) {
    static const char *kFp[] = {"float", "double"};
    for (const char *Tb_c : kFp) {
      std::string Tb = Tb_c;
      if (!requested_types.count(Tb)) continue;
      if (!supportsType(tk, tgt_name, Tb)) continue;
      SideType B{Tb, Tb};
      if (a_cast) {
        for (const auto &pa : castPairs(src_name)) {
          SideType A{pa.first, pa.second};
          if (srcResultType(A.in, A.out) != "i32") continue;
          out.push_back({A, B});
        }
      } else {
        if (!requested_types.count("i32")) continue;
        if (!supportsType(sk, src_name, "i32")) continue;
        SideType A{"i32", "i32"};
        if (srcResultType(A.in, A.out) != "i32") continue;
        out.push_back({A, B});
      }
    }
    return out;
  }

  // Special-case: K_GEP's index slot is i64. A narrower int produced by
  // A is sext'd to i64 in composeEdge, so enumerate over all int Ts (B's
  // element type tracks A's int output type for naming consistency).
  if (tk == K_GEP && target_slot == 1) {
    if (a_cast) {
      for (const auto &pa : castPairs(src_name)) {
        SideType A{pa.first, pa.second};
        if (!isInt(A.out)) continue;
        SideType B{A.out, A.out};
        out.push_back({A, B});
      }
    } else {
      for (const auto &T : requested_types) {
        if (!isInt(T)) continue;
        if (!supportsType(sk, src_name, T)) continue;
        SideType A{T, T}, B{T, T};
        out.push_back({A, B});
      }
    }
    return out;
  }

  if (!a_cast && !b_cast) {
    for (const auto &T : requested_types) {
      // Per-side type gating: FP kinds accept only float/double, int kinds
      // only iN. Without this filter, every (fadd, add)-style edge would
      // emit a combo at every type and be rejected later in composeEdge.
      if (!supportsType(sk, src_name, T)) continue;
      if (!supportsType(tk, tgt_name, T)) continue;
      SideType A{T, T}, B{T, T};
      if (srcResultType(A.in, A.out) != tgtSlotType(B.in, B.out)) continue;
      out.push_back({A, B});
    }
    return out;
  }
  if (a_cast && !b_cast) {
    for (const auto &p : castPairs(src_name)) {
      SideType A{p.first, p.second};
      // B is non-cast → B.in == B.out == some T. Need T == slotType(B).
      // For most kinds slotType returns B.in directly, so T = A.out.
      // For K_SELECT slot 0 == i1, but A.out from our cast pair list is
      // never i1, so that combo is skipped by the connection check.
      const std::string T = A.out;
      if (!supportsType(tk, tgt_name, T)) continue;
      SideType B{T, T};
      if (srcResultType(A.in, A.out) != tgtSlotType(B.in, B.out)) continue;
      out.push_back({A, B});
    }
    return out;
  }
  if (!a_cast && b_cast) {
    for (const auto &p : castPairs(tgt_name)) {
      SideType B{p.first, p.second};
      // A is non-cast → A.in == A.out == T. A.out must equal B's input.
      const std::string T = B.in;
      if (!supportsType(sk, src_name, T)) continue;
      SideType A{T, T};
      // For K_ICMP source, srcResultType is i1 (not T). The cast pair
      // list doesn't include i1 source, so the connection always fails.
      if (srcResultType(A.in, A.out) != tgtSlotType(B.in, B.out)) continue;
      out.push_back({A, B});
    }
    return out;
  }
  // Both casts: enumerate (A pair, B pair) where A.out == B.in.
  for (const auto &pa : castPairs(src_name)) {
    SideType A{pa.first, pa.second};
    for (const auto &pb : castPairs(tgt_name)) {
      SideType B{pb.first, pb.second};
      if (A.out != B.in) continue;
      if (srcResultType(A.in, A.out) != tgtSlotType(B.in, B.out)) continue;
      out.push_back({A, B});
    }
  }
  (void)requested_types;
  return out;
}

[[noreturn]] void usage(int code) {
  std::fprintf(stderr,
               "Usage: compose-gen --matrix <path.json> --rules <rules.json> [--rules ...]\n"
               "                   --output-dir <dir>\n"
               "                   [--edge-types ssa,memory,control]\n"
               "                   [--types i8,i16,i32,i64,float,double]\n"
               "                   [--opt <path-to-opt>]\n"
               "                   [--no-verify]\n");
  std::exit(code);
}

Options parseArgs(int argc, char **argv) {
  Options o;
  auto needValue = [&](int &i, const char *flag) -> std::string {
    if (i + 1 >= argc) {
      std::fprintf(stderr, "compose-gen: %s requires a value\n", flag);
      usage(2);
    }
    return argv[++i];
  };
  for (int i = 1; i < argc; ++i) {
    std::string a = argv[i];
    if (a == "--matrix") {
      o.matrix_path = needValue(i, "--matrix");
    } else if (a.rfind("--matrix=", 0) == 0) {
      o.matrix_path = a.substr(9);
    } else if (a == "--rules") {
      o.rules_paths.push_back(needValue(i, "--rules"));
    } else if (a.rfind("--rules=", 0) == 0) {
      o.rules_paths.push_back(a.substr(8));
    } else if (a == "--output-dir") {
      o.output_dir = needValue(i, "--output-dir");
    } else if (a.rfind("--output-dir=", 0) == 0) {
      o.output_dir = a.substr(13);
    } else if (a == "--edge-types") {
      o.edge_types = splitCsv(needValue(i, "--edge-types"));
    } else if (a.rfind("--edge-types=", 0) == 0) {
      o.edge_types = splitCsv(a.substr(13));
    } else if (a == "--types") {
      o.types = splitCsv(needValue(i, "--types"));
    } else if (a.rfind("--types=", 0) == 0) {
      o.types = splitCsv(a.substr(8));
    } else if (a == "--opt") {
      o.opt_path = needValue(i, "--opt");
    } else if (a.rfind("--opt=", 0) == 0) {
      o.opt_path = a.substr(6);
    } else if (a == "--no-verify") {
      o.no_verify = true;
    } else if (a == "-h" || a == "--help") {
      usage(0);
    } else {
      std::fprintf(stderr, "compose-gen: unknown arg '%s'\n", a.c_str());
      usage(2);
    }
  }
  if (o.matrix_path.empty() || o.rules_paths.empty() || o.output_dir.empty())
    usage(2);
  return o;
}

}  // namespace

int main(int argc, char **argv) {
  Options opts = parseArgs(argc, argv);

  Database db;
  for (const auto &p : opts.rules_paths) parseOneFile(db, p);
  buildRuleIndex(db);

  std::string mtext;
  try {
    mtext = readFile(opts.matrix_path);
  } catch (const std::exception &e) {
    std::fprintf(stderr, "compose-gen: %s\n", e.what());
    return 1;
  }
  json mroot;
  try {
    mroot = json::parse(mtext);
  } catch (const json::parse_error &e) {
    std::fprintf(stderr, "compose-gen: matrix parse error: %s\n", e.what());
    return 1;
  }
  if (!mroot.is_object() || !mroot.contains("edges") ||
      !mroot["edges"].is_array()) {
    std::fprintf(stderr, "compose-gen: matrix missing 'edges' array\n");
    return 1;
  }
  const json &edges = mroot["edges"];

  if (!ensureDir(opts.output_dir)) {
    std::fprintf(stderr, "compose-gen: cannot create output dir %s\n",
                 opts.output_dir.c_str());
    return 1;
  }

  std::set<std::string> requested_edges(opts.edge_types.begin(),
                                        opts.edge_types.end());
  std::set<std::string> requested_types(opts.types.begin(), opts.types.end());

  // ---- Stats ----
  size_t total_edges_seen = 0;
  size_t edges_unknown_rule = 0;
  size_t edges_unsupported_kind = 0;
  size_t edges_skipped_filter = 0;
  size_t generated = 0;
  size_t verified = 0;
  size_t failed = 0;
  std::map<std::string, size_t> by_pair_kind;
  std::map<std::string, size_t> by_edge_type;
  std::unordered_set<std::string> seen_files;
  size_t failure_log_remaining = opts.max_failure_log;

  for (const auto &edge : edges) {
    if (!edge.is_object()) continue;
    if (!edge.contains("source_rule") || !edge.contains("target_rule") ||
        !edge.contains("target_operand") || !edge.contains("edge_types"))
      continue;

    std::string src_id = edge["source_rule"].get<std::string>();
    std::string tgt_id = edge["target_rule"].get<std::string>();
    int target_op = edge["target_operand"].get<int>();
    const json &eta = edge["edge_types"];
    if (!eta.is_array()) continue;
    // Matrix uses target_op = -1 for control (no specific operand
    // position). For SSA / memory edges target_op must be a real slot.
    // We tolerate -1 here and reject per-edge-type below.

    auto sit = db.rule_index.find(src_id);
    auto tit = db.rule_index.find(tgt_id);
    if (sit == db.rule_index.end() || tit == db.rule_index.end()) {
      ++edges_unknown_rule;
      continue;
    }
    const Rule &sr = db.instructions[sit->second.first].rules[sit->second.second];
    const Rule &tr = db.instructions[tit->second.first].rules[tit->second.second];

    Kind sk = kindOf(sr.instruction);
    Kind tk = kindOf(tr.instruction);
    if (sk == K_UNSUPPORTED || tk == K_UNSUPPORTED) {
      ++edges_unsupported_kind;
      continue;
    }

    for (const auto &etv : eta) {
      if (!etv.is_string()) continue;
      std::string et = etv.get<std::string>();
      if (!requested_edges.count(et)) continue;

      // Pick the target value slot for B. SSA/memory: derived from the
      // matrix's target_operand (matrix records the storable-op index
      // for memory). Control: matrix uses -1, so we scan slot candidates
      // in priority order until one yields a type-compatible combo.
      std::vector<int> slot_candidates;
      if (et == "control") {
        if (target_op != -1) {
          ++edges_skipped_filter;
          continue;
        }
        slot_candidates = controlSlotPriority(tk, tr.instruction);
      } else {
        if (target_op < 0) {
          ++edges_skipped_filter;
          continue;
        }
        int s = operandToSlot(tk, target_op);
        if (s >= 0) slot_candidates.push_back(s);
      }
      if (slot_candidates.empty()) {
        ++edges_skipped_filter;
        continue;
      }

      int slot_for_types = -1;
      std::vector<std::pair<SideType, SideType>> combos;
      for (int s : slot_candidates) {
        combos = typeCombosForEdge(sk, sr.instruction, tk, tr.instruction, s,
                                    requested_types);
        if (!combos.empty()) {
          slot_for_types = s;
          break;
        }
      }
      if (combos.empty()) {
        ++edges_skipped_filter;
        continue;
      }

      for (const auto &combo : combos) {
        const SideType &A = combo.first;
        const SideType &B = combo.second;
        ++total_edges_seen;

        std::string base = baseName(sr, tr, target_op, et, A, B);
        std::string fname = base;
        int dedup = 1;
        while (seen_files.count(fname)) {
          fname = base + "_" + std::to_string(++dedup);
        }

        std::string fn_name = sanitize(fname);
        auto cr = composeEdge(sr, tr, slot_for_types, et, A, B, fn_name);
        if (!cr) {
          ++edges_skipped_filter;
          continue;
        }

        ++generated;

        std::string err;
        bool ok = opts.no_verify ||
                  verifyByOpt(opts.opt_path, cr->body, err);
        if (!ok) {
          ++failed;
          if (failure_log_remaining > 0) {
            --failure_log_remaining;
            std::fprintf(stderr, "[compose-gen] FAILED %s.ll\n", fname.c_str());
            if (!err.empty()) std::fprintf(stderr, "%s", err.c_str());
            std::fprintf(stderr, "--- IR ---\n%s---\n", cr->body.c_str());
          }
          continue;
        }
        ++verified;
        seen_files.insert(fname);

        std::string kp = std::string(kindName(sk)) + "->" + kindName(tk);
        ++by_pair_kind[kp];
        ++by_edge_type[et];

        try {
          writeFile(opts.output_dir + "/" + fname + ".ll", cr->body);
        } catch (const std::exception &e) {
          std::fprintf(stderr, "compose-gen: %s\n", e.what());
          ++failed;
        }
      }
    }
  }

  // ---- Summary ----
  std::fprintf(stderr, "\n=== compose-gen summary ===\n");
  std::fprintf(stderr, "Matrix:        %s\n", opts.matrix_path.c_str());
  std::fprintf(stderr, "Rule files:    %zu\n", opts.rules_paths.size());
  for (const auto &p : opts.rules_paths)
    std::fprintf(stderr, "  - %s\n", p.c_str());
  std::fprintf(stderr, "Output dir:    %s\n", opts.output_dir.c_str());
  std::fprintf(stderr, "Edge types:    ");
  for (size_t i = 0; i < opts.edge_types.size(); ++i)
    std::fprintf(stderr, "%s%s", i ? "," : "", opts.edge_types[i].c_str());
  std::fprintf(stderr, "\nTypes:         ");
  for (size_t i = 0; i < opts.types.size(); ++i)
    std::fprintf(stderr, "%s%s", i ? "," : "", opts.types[i].c_str());
  std::fprintf(stderr, "\n\n");
  std::fprintf(stderr, "Total matrix edges:                %zu\n",
               (size_t)edges.size());
  std::fprintf(stderr, "  unknown-rule (matrix vs rules):  %zu\n",
               edges_unknown_rule);
  std::fprintf(stderr, "  unsupported-kind:                %zu\n",
               edges_unsupported_kind);
  std::fprintf(stderr, "(per (edge_type, type) attempts:    %zu)\n",
               total_edges_seen);
  std::fprintf(stderr, "  skipped (filter / type mismatch): %zu\n",
               edges_skipped_filter);
  std::fprintf(stderr, "  generated:                       %zu\n", generated);
  std::fprintf(stderr, "  verified + written:              %zu\n", verified);
  std::fprintf(stderr, "  failed verify:                   %zu\n", failed);
  std::fprintf(stderr, "\nBy (source-kind -> target-kind):\n");
  for (const auto &kv : by_pair_kind)
    std::fprintf(stderr, "  %-44s %zu\n", kv.first.c_str(), kv.second);
  std::fprintf(stderr, "\nBy edge_type:\n");
  for (const auto &kv : by_edge_type)
    std::fprintf(stderr, "  %-10s %zu\n", kv.first.c_str(), kv.second);

  return failed == 0 ? 0 : 1;
}
