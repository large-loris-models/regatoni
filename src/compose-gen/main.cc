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
  K_FREEZE,
  K_SELECT,
  K_ICMP,
  K_GEP,
  K_LOAD_STORE,
  K_EXTRACTVALUE,
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
    case K_FREEZE: return "freeze";
    case K_SELECT: return "select";
    case K_ICMP: return "icmp";
    case K_GEP: return "gep";
    case K_LOAD_STORE: return "load_store";
    case K_EXTRACTVALUE: return "extractvalue";
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
      // Misc
      {"freeze", K_FREEZE},
      {"select", K_SELECT},
      {"icmp", K_ICMP},
  };
  auto it = table.find(name);
  return it == table.end() ? K_UNSUPPORTED : it->second;
}

// =========================================================================
// Type helpers. V1 instantiates against scalar integer types ("i8", "i16",
// "i32", "i64"). Everything else is rejected at the per-rule type filter.
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

// Number of value-bearing operand slots for this kind. Constant/immarg
// operands are not value slots.
int numValueSlots(Kind k) {
  switch (k) {
    case K_BINARY: return 2;
    case K_BINARY_INTRINSIC: return 2;
    case K_UNARY_INTRINSIC: return 1;
    case K_TERNARY_INTRINSIC: return 3;
    case K_INTRINSIC_WITH_FLAG: return 1;
    case K_FREEZE: return 1;
    case K_SELECT: return 3;
    case K_ICMP: return 2;
    default: return 0;
  }
}

// Type of value slot `slot` at module-instantiation type T.
std::string slotType(Kind k, int slot, const std::string &T) {
  if (k == K_SELECT && slot == 0) return "i1";
  return T;
}

// Result type of this instruction at T.
std::string resultType(Kind k, const std::string &T) {
  if (k == K_ICMP) return "i1";
  return T;
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
  if (k == K_ICMP) {
    if (op_idx == 1) return 0;
    if (op_idx == 2) return 1;
    return -1;
  }
  int n = numValueSlots(k);
  return (op_idx >= 0 && op_idx < n) ? op_idx : -1;
}

// Whether the instruction's pattern can be instantiated at T.
bool supportsType(Kind k, const std::string &inst_name,
                  const std::string &T) {
  if (!isInt(T)) return false;
  if (inst_name == "llvm.bswap") {
    int w = widthOf(T);
    return w > 0 && (w % 16 == 0);
  }
  // The rest accept any scalar integer width.
  (void)k;
  return true;
}

// =========================================================================
// Per-rule filters. The matrix references rules that aren't applicable
// at scalar i32 (vector / pointer / FMF / aggregate / byte variants).
// Skip them here rather than producing bad IR.
// =========================================================================

bool ruleAppliesAtT(Kind k, const Rule &rule, const std::string &T) {
  (void)T;
  const std::string &id = rule.id;
  if (id.find("vector") != std::string::npos) return false;
  if (id.find("fmf") != std::string::npos) return false;
  if (id.find("pointer") != std::string::npos) return false;
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
                             const std::string &T,
                             const std::vector<std::string> &slot_vars,
                             std::set<std::string> &decls) {
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

std::optional<ComposeResult>
composeEdge(const Rule &src_rule, const Rule &tgt_rule, int target_operand,
            const std::string &edge_type, const std::string &T,
            const std::string &fn_name) {
  Kind sk = kindOf(src_rule.instruction);
  Kind tk = kindOf(tgt_rule.instruction);
  if (sk == K_UNSUPPORTED || tk == K_UNSUPPORTED) return std::nullopt;

  if (!supportsType(sk, src_rule.instruction, T)) return std::nullopt;
  if (!supportsType(tk, tgt_rule.instruction, T)) return std::nullopt;

  if (!ruleAppliesAtT(sk, src_rule, T)) return std::nullopt;
  if (!ruleAppliesAtT(tk, tgt_rule, T)) return std::nullopt;

  int target_slot = operandToSlot(tk, target_operand);
  if (target_slot < 0) return std::nullopt;

  // Type match at the connection point.
  const std::string src_result = resultType(sk, T);
  const std::string tgt_slot_t = slotType(tk, target_slot, T);
  if (src_result != tgt_slot_t) return std::nullopt;

  // Source can't be ICMP for now (it produces i1; nothing in V1 reads i1
  // at a value slot besides select.cond at slot 0, which the matrix
  // *does* expose — but it would need i1 source rules that we'd want to
  // emit too. Defer; the type-match check above already filters this
  // unless the user requests --types i1).
  // (No special-case needed: the type match handles it.)

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
    param_types.push_back(slotType(sk, i, T));
    param_vars.push_back(src_slots[i]);
  }
  // The variable holding A's result that target B will consume.
  const std::string a_value = (edge_type == "memory") ? "%ld" : "%a";
  for (int i = 0; i < n_tgt_slots; ++i) {
    if (i == target_slot) {
      tgt_slots[i] = a_value;
    } else {
      tgt_slots[i] = varName(next_p++);
      param_types.push_back(slotType(tk, i, T));
      param_vars.push_back(tgt_slots[i]);
    }
  }

  // Memory edge stores/loads T, so the loaded type must equal T (which
  // is also what A produced if we got this far).
  if (edge_type == "memory" && src_result != T) return std::nullopt;

  std::set<std::string> decls;
  std::string a_insn = emitInstruction(sk, src_rule.instruction, src_rule,
                                        "%a", T, src_slots, decls);
  if (a_insn.empty()) return std::nullopt;
  std::string b_insn = emitInstruction(tk, tgt_rule.instruction, tgt_rule,
                                        "%b", T, tgt_slots, decls);
  if (b_insn.empty()) return std::nullopt;

  const std::string ret_type = resultType(tk, T);

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
    os << "  %p = alloca " << T << "\n";
    os << "  " << a_insn << "\n";
    os << "  store " << T << " %a, ptr %p\n";
    os << "  %ld = load " << T << ", ptr %p\n";
    os << "  " << b_insn << "\n";
  } else {
    os << "  " << a_insn << "\n";
    os << "  " << b_insn << "\n";
  }
  os << "  ret " << ret_type << " %b\n";
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

std::string baseName(const Rule &src, const Rule &tgt, int target_operand,
                     const std::string &edge_type, const std::string &T) {
  std::string out = "compose__";
  out += ruleSlug(src.id);
  out += "__";
  out += ruleSlug(tgt.id);
  if (edge_type == "ssa") {
    out += "_op" + std::to_string(target_operand);
  }
  out += "__" + edge_type;
  out += "__" + T;
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
  std::vector<std::string> edge_types{"ssa", "memory"};
  std::vector<std::string> types{"i32"};
  std::string opt_path = "deps/llvm-build-plain/bin/opt";
  bool no_verify = false;
  size_t max_failure_log = 20;
};

[[noreturn]] void usage(int code) {
  std::fprintf(stderr,
               "Usage: compose-gen --matrix <path.json> --rules <rules.json> [--rules ...]\n"
               "                   --output-dir <dir>\n"
               "                   [--edge-types ssa,memory]\n"
               "                   [--types i32]\n"
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
    if (!eta.is_array() || target_op < 0) continue;

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

      for (const auto &T : opts.types) {
        if (!requested_types.count(T)) continue;
        ++total_edges_seen;

        std::string base = baseName(sr, tr, target_op, et, T);
        std::string fname = base;
        int dedup = 1;
        while (seen_files.count(fname)) {
          fname = base + "_" + std::to_string(++dedup);
        }

        std::string fn_name = sanitize(fname);
        auto cr =
            composeEdge(sr, tr, target_op, et, T, fn_name);
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
