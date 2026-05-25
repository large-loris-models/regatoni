#include "src/litmus-gen/generators.h"

#include <set>
#include <sstream>
#include <string>

namespace regatoni::litmus {

namespace generators {

namespace {

const std::set<std::string> kSupportedBinaryOps = {
    "add", "sub",  "mul", "sdiv", "udiv", "srem", "urem",
    "shl", "lshr", "ashr", "and", "or",   "xor",
};

const std::set<std::string> kSupportedOverflowIntrinsics = {
    "llvm.sadd.with.overflow", "llvm.uadd.with.overflow",
    "llvm.ssub.with.overflow", "llvm.usub.with.overflow",
    "llvm.smul.with.overflow", "llvm.umul.with.overflow",
};

const std::set<std::string> kBinaryIntIntrinsics = {
    "llvm.smax", "llvm.smin", "llvm.umax", "llvm.umin",
    "llvm.sadd.sat", "llvm.uadd.sat", "llvm.ssub.sat", "llvm.usub.sat",
};

const std::set<std::string> kUnaryIntIntrinsics = {
    "llvm.ctpop", "llvm.bitreverse", "llvm.bswap",
};

const std::set<std::string> kFpMinMaxIntrinsics = {
    "llvm.minnum",     "llvm.maxnum",     "llvm.minimum",
    "llvm.maximum",    "llvm.minimumnum", "llvm.maximumnum",
};

const std::set<std::string> kComparisonIntrinsics = {
    "llvm.scmp", "llvm.ucmp",
};

const std::set<std::string> kMemoryIntrinsics = {
    "llvm.memcpy", "llvm.memmove", "llvm.memset",
};

const std::set<std::string> kSatConversionIntrinsics = {
    "llvm.fptoui.sat", "llvm.fptosi.sat",
};

const std::set<std::string> kSatShiftIntrinsics = {
    "llvm.sshl.sat", "llvm.ushl.sat",
};

const std::set<std::string> kFixedPointIntrinsics = {
    "llvm.smul.fix", "llvm.umul.fix", "llvm.sdiv.fix", "llvm.udiv.fix",
};

const std::set<std::string> kUnaryFPIntrinsics = {
    "llvm.sqrt",       "llvm.sin",       "llvm.cos",        "llvm.exp",
    "llvm.exp2",       "llvm.log",       "llvm.log10",      "llvm.log2",
    "llvm.fabs",       "llvm.floor",     "llvm.ceil",       "llvm.trunc",
    "llvm.round",      "llvm.rint",      "llvm.nearbyint",  "llvm.roundeven",
    "llvm.canonicalize",
};

const std::set<std::string> kBinaryFPIntrinsics = {
    "llvm.pow", "llvm.copysign",
};

const std::set<std::string> kTernaryFPIntrinsics = {
    "llvm.fma", "llvm.fmuladd",
};

const std::set<std::string> kFpToIntIntrinsics = {
    "llvm.lround", "llvm.llround", "llvm.lrint", "llvm.llrint",
};

const std::set<std::string> kIntegerVectorReductions = {
    "llvm.vector.reduce.add",  "llvm.vector.reduce.mul",
    "llvm.vector.reduce.and",  "llvm.vector.reduce.or",
    "llvm.vector.reduce.xor",  "llvm.vector.reduce.smax",
    "llvm.vector.reduce.smin", "llvm.vector.reduce.umax",
    "llvm.vector.reduce.umin",
};

const std::set<std::string> kFpVectorReductions = {
    "llvm.vector.reduce.fadd", "llvm.vector.reduce.fmul",
};

const std::set<std::string> kFpMinMaxVectorReductions = {
    "llvm.vector.reduce.fmax",     "llvm.vector.reduce.fmin",
    "llvm.vector.reduce.fmaximum", "llvm.vector.reduce.fminimum",
};

const std::set<std::string> kFixedPointSatIntrinsics = {
    "llvm.smul.fix.sat", "llvm.umul.fix.sat",
    "llvm.sdiv.fix.sat", "llvm.udiv.fix.sat",
};

// FP fast-math flags. Rules carrying these would only differ from the base
// rule by the FMF prefix on the call; we skip them to avoid near-duplicate
// IR. The base (flag=null) rule is always emitted.
const std::set<std::string> kFmfFlags = {
    "nnan", "ninf", "nsz",     "arcp",
    "contract", "afn",  "reassoc", "fast",
};

bool isFmfRule(const Rule &rule) {
  return rule.flag.has_value() && kFmfFlags.count(*rule.flag);
}

// Parse a "v<N>x<elem>" descriptor (e.g. "v4xi32" -> N="4", elem="i32",
// vec_ty="<4 x i32>"). Returns false on malformed input.
bool parseVectorDescriptor(const std::string &type, std::string &n_str,
                           std::string &elem, std::string &vec_ty) {
  if (type.size() < 5 || type[0] != 'v') return false;
  size_t xpos = type.find('x', 1);
  if (xpos == std::string::npos || xpos == 1) return false;
  n_str = type.substr(1, xpos - 1);
  elem = type.substr(xpos + 1);
  if (n_str.empty() || elem.empty()) return false;
  vec_ty = "<" + n_str + " x " + elem + ">";
  return true;
}

// "float" -> "f32", "double" -> "f64".
const char *fpMangle(const std::string &type) {
  if (type == "float") return "f32";
  if (type == "double") return "f64";
  return nullptr;
}

// Build a function name from rule.id, type, and optional disambiguator.
std::string fnName(const Rule &rule, const std::string &type,
                   const std::string &suffix = "") {
  std::string out = mangleRuleId(rule.id) + "_" + type;
  if (!suffix.empty()) out += "_" + suffix;
  return out;
}

// Single-file convenience wrapper.
std::vector<NamedFile> singleFile(std::string body) {
  std::vector<NamedFile> out;
  out.push_back({/*suffix=*/"", std::move(body)});
  return out;
}

// Parse "i32_to_i16" → ("i32", "i16"). Returns false on malformed input.
bool splitCastPair(const std::string &type, std::string &src,
                   std::string &dst) {
  size_t pos = type.find("_to_");
  if (pos == std::string::npos) return false;
  src = type.substr(0, pos);
  dst = type.substr(pos + 4);
  return true;
}

// Rule-ID suffix → which struct field a defined_semantics rule on an
// overflow intrinsic is talking about. -1 if neither match.
int overflowElementIndex(const std::string &rule_id) {
  auto endsWith = [&](const char *suf) {
    size_t n = std::string(suf).size();
    return rule_id.size() >= n &&
           rule_id.compare(rule_id.size() - n, n, suf) == 0;
  };
  if (endsWith(".first_element")) return 0;
  if (endsWith(".second_element")) return 1;
  return -1;
}

std::string overflowExtractBody(const Instruction &inst, const Rule &rule,
                                const std::string &type, int idx) {
  const std::string fn = fnName(rule, type);
  const std::string call = "@" + inst.name + "." + type;
  const std::string ret_ty = idx == 0 ? type : std::string("i1");
  std::ostringstream os;
  os << "declare {" << type << ", i1} " << call << "(" << type << ", "
     << type << ")\n\n";
  os << "define " << ret_ty << " @" << fn << "(" << type << " %a, " << type
     << " %b) {\n";
  os << "  %r = call {" << type << ", i1} " << call << "(" << type
     << " %a, " << type << " %b)\n";
  os << "  %f = extractvalue {" << type << ", i1} %r, " << idx << "\n";
  os << "  ret " << ret_ty << " %f\n";
  os << "}\n";
  return os.str();
}

std::string overflowStructBody(const Instruction &inst, const Rule &rule,
                               const std::string &type) {
  const std::string fn = fnName(rule, type);
  const std::string call = "@" + inst.name + "." + type;
  std::ostringstream os;
  os << "declare {" << type << ", i1} " << call << "(" << type << ", "
     << type << ")\n\n";
  os << "define {" << type << ", i1} @" << fn << "(" << type << " %a, "
     << type << " %b) {\n";
  os << "  %r = call {" << type << ", i1} " << call << "(" << type
     << " %a, " << type << " %b)\n";
  os << "  ret {" << type << ", i1} %r\n";
  os << "}\n";
  return os.str();
}

}  // namespace

std::string mangleRuleId(const std::string &rule_id) {
  std::string out = rule_id;
  for (char &c : out)
    if (c == '.') c = '_';
  return out;
}

// ---------------------------------------------------------------------
// Binary ops: %r = <op> [flag] <ty> %op1, %op2
// ---------------------------------------------------------------------
std::vector<NamedFile> binaryOpWithFlag(const Instruction &inst,
                                        const Rule &rule,
                                        const std::string &type) {
  if (!kSupportedBinaryOps.count(inst.name)) return {};
  if (inst.operands.size() < 2) return {};

  std::string flag_str;
  if (rule.flag.has_value()) {
    const std::string &f = *rule.flag;
    if (f == "nuw" || f == "nsw") {
      if (inst.name != "add" && inst.name != "sub" && inst.name != "mul" &&
          inst.name != "shl")
        return {};
      flag_str = f + " ";
    } else if (f == "exact") {
      if (inst.name != "sdiv" && inst.name != "udiv" && inst.name != "lshr" &&
          inst.name != "ashr")
        return {};
      flag_str = "exact ";
    } else if (f == "disjoint") {
      if (inst.name != "or") return {};
      flag_str = "disjoint ";
    } else {
      return {};
    }
  }

  const std::string fn = fnName(rule, type);
  std::ostringstream os;
  os << "define " << type << " @" << fn << "(" << type << " %op1, " << type
     << " %op2) {\n";
  os << "  %r = " << inst.name << " " << flag_str << type
     << " %op1, %op2\n";
  os << "  ret " << type << " %r\n";
  os << "}\n";
  return singleFile(os.str());
}

// ---------------------------------------------------------------------
// Overflow intrinsics: extractvalue variants for defined_semantics rules
// naming a struct element; whole-struct return for multi_return shape.
// ---------------------------------------------------------------------
std::vector<NamedFile> overflowGen(const Instruction &inst, const Rule &rule,
                                    const std::string &type) {
  if (!kSupportedOverflowIntrinsics.count(inst.name)) return {};
  int idx = overflowElementIndex(rule.id);
  if (idx >= 0) return singleFile(overflowExtractBody(inst, rule, type, idx));
  return singleFile(overflowStructBody(inst, rule, type));
}

// ---------------------------------------------------------------------
// Cast ops: trunc / zext / sext. Accepts type as "src_to_dst" pair.
// ---------------------------------------------------------------------
std::vector<NamedFile> castOp(const Instruction &inst, const Rule &rule,
                               const std::string &type) {
  std::string src, dst;
  if (!splitCastPair(type, src, dst)) return {};

  std::string flag_str;
  if (rule.flag.has_value()) {
    const std::string &f = *rule.flag;
    if (f == "nuw" || f == "nsw") {
      if (inst.name != "trunc") return {};
      flag_str = f + " ";
    } else if (f == "nneg") {
      if (inst.name != "zext") return {};
      flag_str = "nneg ";
    } else {
      return {};
    }
  }

  // Skip i1-only rules (LangRef has explicit "When zero/sign extending
  // from i1, ..." rules — none of our pair set starts at i1).
  if (rule.id.find(".from_i1") != std::string::npos) return {};

  const std::string fn = fnName(rule, type);
  std::ostringstream os;
  os << "define " << dst << " @" << fn << "(" << src << " %op1) {\n";
  os << "  %r = " << inst.name << " " << flag_str << src << " %op1 to " << dst
     << "\n";
  os << "  ret " << dst << " %r\n";
  os << "}\n";
  return singleFile(os.str());
}

// ---------------------------------------------------------------------
// icmp: one variant per predicate. fcmp skipped (FP not in V1).
// ---------------------------------------------------------------------
std::vector<NamedFile> comparison(const Instruction &inst, const Rule &rule,
                                   const std::string &type) {
  if (inst.name != "icmp") return {};

  std::vector<std::string> preds;
  bool samesign = false;
  const std::string &id = rule.id;
  if (id == "icmp.defined.eq") preds = {"eq"};
  else if (id == "icmp.defined.ne") preds = {"ne"};
  else if (id == "icmp.defined.unsigned") preds = {"ugt", "uge", "ult", "ule"};
  else if (id == "icmp.defined.signed") preds = {"sgt", "sge", "slt", "sle"};
  else if (id == "icmp.poison.samesign") { preds = {"slt"}; samesign = true; }
  // pointer rule and anything else: skip
  if (preds.empty()) return {};

  std::vector<NamedFile> out;
  out.reserve(preds.size());
  for (const auto &p : preds) {
    const std::string fn = fnName(rule, type, p);
    std::string flag_str = samesign ? "samesign " : "";
    std::ostringstream os;
    os << "define i1 @" << fn << "(" << type << " %op1, " << type
       << " %op2) {\n";
    os << "  %r = icmp " << flag_str << p << " " << type << " %op1, %op2\n";
    os << "  ret i1 %r\n";
    os << "}\n";
    out.push_back({p, os.str()});
  }
  return out;
}

// ---------------------------------------------------------------------
// select: scalar i1 cond + two same-typed values. Vector / fmf rules
// are skipped in V1.
// ---------------------------------------------------------------------
std::vector<NamedFile> selectOp(const Instruction &inst, const Rule &rule,
                                 const std::string &type) {
  if (inst.name != "select") return {};
  const std::string &id = rule.id;
  // Only scalar-i1-cond cases. Vector and FP-fmf rules are out of V1.
  if (id == "select.defined.vector_elementwise" ||
      id == "select.defined.vector_broadcast" ||
      id == "select.poison.fmf_result_only")
    return {};

  const std::string fn = fnName(rule, type);
  std::ostringstream os;
  os << "define " << type << " @" << fn << "(i1 %cond, " << type << " %val1, "
     << type << " %val2) {\n";
  os << "  %r = select i1 %cond, " << type << " %val1, " << type << " %val2\n";
  os << "  ret " << type << " %r\n";
  os << "}\n";
  return singleFile(os.str());
}

// ---------------------------------------------------------------------
// freeze: one input, same type out. Skip pointer / aggregate / byte
// rules (not in V1).
// ---------------------------------------------------------------------
std::vector<NamedFile> freezeOp(const Instruction &inst, const Rule &rule,
                                 const std::string &type) {
  if (inst.name != "freeze") return {};
  const std::string &id = rule.id;
  if (id == "freeze.defined.pointer_nondereferenceable" ||
      id == "freeze.defined.elementwise_aggregate" ||
      id == "freeze.defined.byte_per_bit")
    return {};

  const std::string fn = fnName(rule, type);
  std::ostringstream os;
  os << "define " << type << " @" << fn << "(" << type << " %op1) {\n";
  os << "  %r = freeze " << type << " %op1\n";
  os << "  ret " << type << " %r\n";
  os << "}\n";
  return singleFile(os.str());
}

// ---------------------------------------------------------------------
// Unary integer intrinsics: ctpop / bitreverse / bswap.
// ---------------------------------------------------------------------
std::vector<NamedFile> unaryIntrinsic(const Instruction &inst,
                                       const Rule &rule,
                                       const std::string &type) {
  if (!kUnaryIntIntrinsics.count(inst.name)) return {};
  const std::string fn = fnName(rule, type);
  const std::string call = "@" + inst.name + "." + type;
  std::ostringstream os;
  os << "declare " << type << " " << call << "(" << type << ")\n\n";
  os << "define " << type << " @" << fn << "(" << type << " %op1) {\n";
  os << "  %r = call " << type << " " << call << "(" << type << " %op1)\n";
  os << "  ret " << type << " %r\n";
  os << "}\n";
  return singleFile(os.str());
}

// ---------------------------------------------------------------------
// ctlz / cttz: integer input + i1 is_zero_poison constant.
// Variant selection by rule.shape:
//   - "param_flag_poison" (poison rule) → only is_zero_poison=true
//   - "defined_semantics" with rule_id naming "zero_poison_false" →
//     only is_zero_poison=false
//   - all other defined_semantics rules → both variants
// ---------------------------------------------------------------------
std::vector<NamedFile> unaryIntrinsicWithFlag(const Instruction &inst,
                                               const Rule &rule,
                                               const std::string &type) {
  if (inst.name != "llvm.ctlz" && inst.name != "llvm.cttz") return {};

  std::vector<bool> variants;
  if (rule.shape == "param_flag_poison") {
    variants = {true};
  } else if (rule.id.find("zero_poison_false") != std::string::npos) {
    variants = {false};
  } else {
    variants = {true, false};
  }

  std::vector<NamedFile> out;
  out.reserve(variants.size());
  for (bool zp : variants) {
    const char *suf = zp ? "zp_true" : "zp_false";
    const std::string fn = fnName(rule, type, suf);
    const std::string call = "@" + inst.name + "." + type;
    std::ostringstream os;
    os << "declare " << type << " " << call << "(" << type << ", i1)\n\n";
    os << "define " << type << " @" << fn << "(" << type << " %op1) {\n";
    os << "  %r = call " << type << " " << call << "(" << type
       << " %op1, i1 " << (zp ? "true" : "false") << ")\n";
    os << "  ret " << type << " %r\n";
    os << "}\n";
    out.push_back({suf, os.str()});
  }
  return out;
}

// ---------------------------------------------------------------------
// Binary integer intrinsics: smax/smin/umax/umin + saturating arith.
// ---------------------------------------------------------------------
std::vector<NamedFile> binaryIntrinsic(const Instruction &inst,
                                        const Rule &rule,
                                        const std::string &type) {
  if (!kBinaryIntIntrinsics.count(inst.name)) return {};
  const std::string fn = fnName(rule, type);
  const std::string call = "@" + inst.name + "." + type;
  std::ostringstream os;
  os << "declare " << type << " " << call << "(" << type << ", " << type
     << ")\n\n";
  os << "define " << type << " @" << fn << "(" << type << " %op1, " << type
     << " %op2) {\n";
  os << "  %r = call " << type << " " << call << "(" << type << " %op1, "
     << type << " %op2)\n";
  os << "  ret " << type << " %r\n";
  os << "}\n";
  return singleFile(os.str());
}

// ---------------------------------------------------------------------
// llvm.abs: integer input + i1 is_int_min_poison. Variant selection
// mirrors cttz/ctlz.
// ---------------------------------------------------------------------
std::vector<NamedFile> absIntrinsic(const Instruction &inst, const Rule &rule,
                                     const std::string &type) {
  if (inst.name != "llvm.abs") return {};

  std::vector<bool> variants;
  if (rule.shape == "param_flag_poison") {
    variants = {true};
  } else if (rule.id.find("no_poison") != std::string::npos) {
    variants = {false};
  } else {
    variants = {true, false};
  }

  std::vector<NamedFile> out;
  out.reserve(variants.size());
  for (bool imp : variants) {
    const char *suf = imp ? "imp_true" : "imp_false";
    const std::string fn = fnName(rule, type, suf);
    const std::string call = "@" + inst.name + "." + type;
    std::ostringstream os;
    os << "declare " << type << " " << call << "(" << type << ", i1)\n\n";
    os << "define " << type << " @" << fn << "(" << type << " %op1) {\n";
    os << "  %r = call " << type << " " << call << "(" << type
       << " %op1, i1 " << (imp ? "true" : "false") << ")\n";
    os << "  ret " << type << " %r\n";
    os << "}\n";
    out.push_back({suf, os.str()});
  }
  return out;
}

// ---------------------------------------------------------------------
// Ternary integer intrinsics: fshl / fshr (funnel shifts).
// ---------------------------------------------------------------------
std::vector<NamedFile> ternaryIntrinsic(const Instruction &inst,
                                         const Rule &rule,
                                         const std::string &type) {
  if (inst.name != "llvm.fshl" && inst.name != "llvm.fshr") return {};
  const std::string fn = fnName(rule, type);
  const std::string call = "@" + inst.name + "." + type;
  std::ostringstream os;
  os << "declare " << type << " " << call << "(" << type << ", " << type
     << ", " << type << ")\n\n";
  os << "define " << type << " @" << fn << "(" << type << " %a, " << type
     << " %b, " << type << " %c) {\n";
  os << "  %r = call " << type << " " << call << "(" << type << " %a, "
     << type << " %b, " << type << " %c)\n";
  os << "  ret " << type << " %r\n";
  os << "}\n";
  return singleFile(os.str());
}

// ---------------------------------------------------------------------
// fneg: single fneg over a floating-point parameter.
// ---------------------------------------------------------------------
std::vector<NamedFile> fnegOp(const Instruction &inst, const Rule &rule,
                               const std::string &type) {
  if (inst.name != "fneg") return {};
  if (type != "float" && type != "double") return {};
  const std::string fn = fnName(rule, type);
  std::ostringstream os;
  os << "define " << type << " @" << fn << "(" << type << " %op1) {\n";
  os << "  %r = fneg " << type << " %op1\n";
  os << "  ret " << type << " %r\n";
  os << "}\n";
  return singleFile(os.str());
}

// ---------------------------------------------------------------------
// getelementptr: one-step GEP with parameter base and index. Flags
// (inbounds / nuw / nusw) are taken from rule.flag. inrange is skipped
// because it's only legal in constant GEP expressions, not runtime
// instructions.
// ---------------------------------------------------------------------
std::vector<NamedFile> gepOp(const Instruction &inst, const Rule &rule,
                              const std::string &type) {
  if (inst.name != "getelementptr") return {};

  std::string flag_str;
  if (rule.flag.has_value()) {
    const std::string &f = *rule.flag;
    if (f == "inbounds") flag_str = "inbounds ";
    else if (f == "nuw") flag_str = "nuw ";
    else if (f == "nusw") flag_str = "nusw ";
    else if (f == "inrange") return {};
    else return {};
  }

  // Rules whose precondition is "all-zero indices" use a literal 0.
  bool allzero = rule.id.find("allzero") != std::string::npos;

  const std::string fn = fnName(rule, type);
  std::ostringstream os;
  if (allzero) {
    os << "define ptr @" << fn << "(ptr %base) {\n";
    os << "  %r = getelementptr " << flag_str << type
       << ", ptr %base, i64 0\n";
  } else {
    os << "define ptr @" << fn << "(ptr %base, i64 %idx) {\n";
    os << "  %r = getelementptr " << flag_str << type
       << ", ptr %base, i64 %idx\n";
  }
  os << "  ret ptr %r\n";
  os << "}\n";
  return singleFile(os.str());
}

// ---------------------------------------------------------------------
// load / store / alloca: alloca + store + load round-trip. Rules whose
// precondition is about loaded-pointer metadata (!nonnull, !align) are
// skipped because they need the loaded type to be ptr.
// ---------------------------------------------------------------------
std::vector<NamedFile> loadStoreOp(const Instruction &inst, const Rule &rule,
                                    const std::string &type) {
  if (inst.name != "load" && inst.name != "store" && inst.name != "alloca")
    return {};
  if (rule.id == "load.poison.nonnull_violation" ||
      rule.id == "load.poison.align_violation")
    return {};

  const std::string fn = fnName(rule, type);
  std::ostringstream os;
  os << "define " << type << " @" << fn << "(" << type << " %val) {\n";
  os << "  %p = alloca " << type << "\n";
  os << "  store " << type << " %val, ptr %p\n";
  os << "  %r = load " << type << ", ptr %p\n";
  os << "  ret " << type << " %r\n";
  os << "}\n";
  return singleFile(os.str());
}

// ---------------------------------------------------------------------
// extractelement / insertelement on a vector parameter. The type
// descriptor encodes the vector shape and element type as
// "v<N>x<elem>" (e.g. "v4xi32" → <4 x i32>); the generator parses it
// and emits the corresponding LLVM syntax.
// ---------------------------------------------------------------------
std::vector<NamedFile> vectorOp(const Instruction &inst, const Rule &rule,
                                 const std::string &type) {
  if (inst.name != "extractelement" && inst.name != "insertelement")
    return {};
  if (type.size() < 5 || type[0] != 'v') return {};
  size_t xpos = type.find('x', 1);
  if (xpos == std::string::npos || xpos == 1) return {};
  std::string n_str = type.substr(1, xpos - 1);
  std::string elem = type.substr(xpos + 1);
  if (n_str.empty() || elem.empty()) return {};
  const std::string vec_ty = "<" + n_str + " x " + elem + ">";

  const std::string fn = fnName(rule, type);
  std::ostringstream os;
  if (inst.name == "extractelement") {
    os << "define " << elem << " @" << fn << "(" << vec_ty
       << " %vec, i32 %idx) {\n";
    os << "  %r = extractelement " << vec_ty << " %vec, i32 %idx\n";
    os << "  ret " << elem << " %r\n";
  } else {
    os << "define " << vec_ty << " @" << fn << "(" << vec_ty << " %vec, "
       << elem << " %elt, i32 %idx) {\n";
    os << "  %r = insertelement " << vec_ty << " %vec, " << elem
       << " %elt, i32 %idx\n";
    os << "  ret " << vec_ty << " %r\n";
  }
  os << "}\n";
  return singleFile(os.str());
}

// ---------------------------------------------------------------------
// FP min/max intrinsics: two same-typed FP inputs, same-typed output.
// Six intrinsics differing only in NaN / signed-zero handling.
// Mangled name: @llvm.<op>.<fp-mangle>  (e.g. llvm.minnum.f32).
// ---------------------------------------------------------------------
std::vector<NamedFile> fpMinMax(const Instruction &inst, const Rule &rule,
                                 const std::string &type) {
  if (!kFpMinMaxIntrinsics.count(inst.name)) return {};
  const char *fp = fpMangle(type);
  if (!fp) return {};
  const std::string fn = fnName(rule, type);
  const std::string call = "@" + inst.name + "." + fp;
  std::ostringstream os;
  os << "declare " << type << " " << call << "(" << type << ", " << type
     << ")\n\n";
  os << "define " << type << " @" << fn << "(" << type << " %a, " << type
     << " %b) {\n";
  os << "  %r = call " << type << " " << call << "(" << type << " %a, "
     << type << " %b)\n";
  os << "  ret " << type << " %r\n";
  os << "}\n";
  return singleFile(os.str());
}

// ---------------------------------------------------------------------
// Three-way comparison intrinsics: llvm.scmp / llvm.ucmp. The type
// descriptor is "<in>_to_<out>" (e.g. "i32_to_i8"). The intrinsic
// mangled name reverses the order: llvm.scmp.<out>.<in>.
// ---------------------------------------------------------------------
std::vector<NamedFile> comparisonIntrinsic(const Instruction &inst,
                                            const Rule &rule,
                                            const std::string &type) {
  if (!kComparisonIntrinsics.count(inst.name)) return {};
  std::string in_ty, out_ty;
  if (!splitCastPair(type, in_ty, out_ty)) return {};
  const std::string fn = fnName(rule, type);
  const std::string call = "@" + inst.name + "." + out_ty + "." + in_ty;
  std::ostringstream os;
  os << "declare " << out_ty << " " << call << "(" << in_ty << ", " << in_ty
     << ")\n\n";
  os << "define " << out_ty << " @" << fn << "(" << in_ty << " %a, " << in_ty
     << " %b) {\n";
  os << "  %r = call " << out_ty << " " << call << "(" << in_ty << " %a, "
     << in_ty << " %b)\n";
  os << "  ret " << out_ty << " %r\n";
  os << "}\n";
  return singleFile(os.str());
}

// ---------------------------------------------------------------------
// Memory intrinsics: memcpy / memmove / memset.
// Type descriptor encodes the length integer type ("i32" or "i64").
// Pointers go through alloca-backed scratch buffers so we don't depend
// on caller-provided memory.
// ---------------------------------------------------------------------
std::vector<NamedFile> memoryIntrinsic(const Instruction &inst,
                                        const Rule &rule,
                                        const std::string &type) {
  if (!kMemoryIntrinsics.count(inst.name)) return {};
  // The brief uses i64 for length; allow i32 too if requested in future.
  if (type != "i32" && type != "i64") return {};

  const std::string fn = fnName(rule, type);
  std::ostringstream os;

  if (inst.name == "llvm.memset") {
    const std::string call = "@llvm.memset.p0." + type;
    os << "declare void " << call << "(ptr, i8, " << type << ", i1)\n\n";
    os << "define void @" << fn << "(ptr %dst, i8 %val, " << type
       << " %len) {\n";
    os << "  call void " << call << "(ptr %dst, i8 %val, " << type
       << " %len, i1 false)\n";
  } else {
    // memcpy / memmove
    const std::string call = "@" + inst.name + ".p0.p0." + type;
    os << "declare void " << call << "(ptr, ptr, " << type << ", i1)\n\n";
    os << "define void @" << fn << "(ptr %dst, ptr %src, " << type
       << " %len) {\n";
    os << "  call void " << call << "(ptr %dst, ptr %src, " << type
       << " %len, i1 false)\n";
  }
  os << "  ret void\n";
  os << "}\n";
  return singleFile(os.str());
}

// ---------------------------------------------------------------------
// Saturating FP-to-int conversions: llvm.fptoui.sat / llvm.fptosi.sat.
// Type descriptor is "<src>_to_<dst>" (e.g. "float_to_i32"). Mangled
// intrinsic name: llvm.fptoui.sat.<dst>.<src-fp-mangle>.
// ---------------------------------------------------------------------
std::vector<NamedFile> satConversionIntrinsic(const Instruction &inst,
                                               const Rule &rule,
                                               const std::string &type) {
  if (!kSatConversionIntrinsics.count(inst.name)) return {};
  std::string src, dst;
  if (!splitCastPair(type, src, dst)) return {};
  const char *fp = fpMangle(src);
  if (!fp) return {};

  const std::string fn = fnName(rule, type);
  const std::string call = "@" + inst.name + "." + dst + "." + fp;
  std::ostringstream os;
  os << "declare " << dst << " " << call << "(" << src << ")\n\n";
  os << "define " << dst << " @" << fn << "(" << src << " %x) {\n";
  os << "  %r = call " << dst << " " << call << "(" << src << " %x)\n";
  os << "  ret " << dst << " %r\n";
  os << "}\n";
  return singleFile(os.str());
}

// ---------------------------------------------------------------------
// Saturating shifts: llvm.sshl.sat / llvm.ushl.sat. Two same-typed int
// inputs, same-typed result.
// ---------------------------------------------------------------------
std::vector<NamedFile> satShiftIntrinsic(const Instruction &inst,
                                          const Rule &rule,
                                          const std::string &type) {
  if (!kSatShiftIntrinsics.count(inst.name)) return {};
  const std::string fn = fnName(rule, type);
  const std::string call = "@" + inst.name + "." + type;
  std::ostringstream os;
  os << "declare " << type << " " << call << "(" << type << ", " << type
     << ")\n\n";
  os << "define " << type << " @" << fn << "(" << type << " %a, " << type
     << " %b) {\n";
  os << "  %r = call " << type << " " << call << "(" << type << " %a, "
     << type << " %b)\n";
  os << "  ret " << type << " %r\n";
  os << "}\n";
  return singleFile(os.str());
}

// ---------------------------------------------------------------------
// Fixed-point arithmetic: llvm.smul.fix / llvm.umul.fix / llvm.sdiv.fix
// / llvm.udiv.fix. Two same-typed int inputs plus an i32 immarg scale.
// Generate two variants per (rule, type): scale=0 and scale=1.
// ---------------------------------------------------------------------
std::vector<NamedFile> fixedPointIntrinsic(const Instruction &inst,
                                            const Rule &rule,
                                            const std::string &type) {
  if (!kFixedPointIntrinsics.count(inst.name)) return {};

  std::vector<NamedFile> out;
  for (int scale : {0, 1}) {
    const std::string suf = "scale" + std::to_string(scale);
    const std::string fn = fnName(rule, type, suf);
    const std::string call = "@" + inst.name + "." + type;
    std::ostringstream os;
    os << "declare " << type << " " << call << "(" << type << ", " << type
       << ", i32)\n\n";
    os << "define " << type << " @" << fn << "(" << type << " %a, " << type
       << " %b) {\n";
    os << "  %r = call " << type << " " << call << "(" << type << " %a, "
       << type << " %b, i32 " << scale << ")\n";
    os << "  ret " << type << " %r\n";
    os << "}\n";
    out.push_back({suf, os.str()});
  }
  return out;
}

// ---------------------------------------------------------------------
// Unary FP intrinsics: sqrt/sin/cos/exp/log/fabs/floor/ceil/round/rint/
// nearbyint/roundeven/canonicalize and FP trunc. (FP %x) -> FP. Mangled
// name is @llvm.<name>.<fp-mangle>. Rules carrying an FMF flag are
// skipped (the base unflagged rule produces the canonical IR).
// ---------------------------------------------------------------------
std::vector<NamedFile> unaryFPIntrinsic(const Instruction &inst,
                                         const Rule &rule,
                                         const std::string &type) {
  if (!kUnaryFPIntrinsics.count(inst.name)) return {};
  if (isFmfRule(rule)) return {};
  const char *fp = fpMangle(type);
  if (!fp) return {};
  const std::string fn = fnName(rule, type);
  const std::string call = "@" + inst.name + "." + fp;
  std::ostringstream os;
  os << "declare " << type << " " << call << "(" << type << ")\n\n";
  os << "define " << type << " @" << fn << "(" << type << " %x) {\n";
  os << "  %r = call " << type << " " << call << "(" << type << " %x)\n";
  os << "  ret " << type << " %r\n";
  os << "}\n";
  return singleFile(os.str());
}

// ---------------------------------------------------------------------
// Binary FP intrinsics: pow, copysign. (FP, FP) -> FP.
// ---------------------------------------------------------------------
std::vector<NamedFile> binaryFPIntrinsic(const Instruction &inst,
                                          const Rule &rule,
                                          const std::string &type) {
  if (!kBinaryFPIntrinsics.count(inst.name)) return {};
  if (isFmfRule(rule)) return {};
  const char *fp = fpMangle(type);
  if (!fp) return {};
  const std::string fn = fnName(rule, type);
  const std::string call = "@" + inst.name + "." + fp;
  std::ostringstream os;
  os << "declare " << type << " " << call << "(" << type << ", " << type
     << ")\n\n";
  os << "define " << type << " @" << fn << "(" << type << " %a, " << type
     << " %b) {\n";
  os << "  %r = call " << type << " " << call << "(" << type << " %a, "
     << type << " %b)\n";
  os << "  ret " << type << " %r\n";
  os << "}\n";
  return singleFile(os.str());
}

// ---------------------------------------------------------------------
// Ternary FP intrinsics: fma, fmuladd. (FP, FP, FP) -> FP.
// ---------------------------------------------------------------------
std::vector<NamedFile> ternaryFPIntrinsic(const Instruction &inst,
                                           const Rule &rule,
                                           const std::string &type) {
  if (!kTernaryFPIntrinsics.count(inst.name)) return {};
  if (isFmfRule(rule)) return {};
  const char *fp = fpMangle(type);
  if (!fp) return {};
  const std::string fn = fnName(rule, type);
  const std::string call = "@" + inst.name + "." + fp;
  std::ostringstream os;
  os << "declare " << type << " " << call << "(" << type << ", " << type
     << ", " << type << ")\n\n";
  os << "define " << type << " @" << fn << "(" << type << " %a, " << type
     << " %b, " << type << " %c) {\n";
  os << "  %r = call " << type << " " << call << "(" << type << " %a, "
     << type << " %b, " << type << " %c)\n";
  os << "  ret " << type << " %r\n";
  os << "}\n";
  return singleFile(os.str());
}

// ---------------------------------------------------------------------
// FP-to-int rounding intrinsics: lround, llround, lrint, llrint. Type
// descriptor is "<src_fp>_to_<dst_int>" (split via splitCastPair).
// Mangled name: @llvm.<name>.<dst>.<src-fp-mangle>. llround/llrint
// support only i64 destinations; lround/lrint support both i32 and i64.
// ---------------------------------------------------------------------
std::vector<NamedFile> fpToIntIntrinsic(const Instruction &inst,
                                         const Rule &rule,
                                         const std::string &type) {
  if (!kFpToIntIntrinsics.count(inst.name)) return {};
  if (isFmfRule(rule)) return {};
  std::string src, dst;
  if (!splitCastPair(type, src, dst)) return {};
  const char *fp = fpMangle(src);
  if (!fp) return {};
  if (inst.name == "llvm.llround" || inst.name == "llvm.llrint") {
    if (dst != "i64") return {};
  } else {
    if (dst != "i32" && dst != "i64") return {};
  }

  const std::string fn = fnName(rule, type);
  const std::string call = "@" + inst.name + "." + dst + "." + fp;
  std::ostringstream os;
  os << "declare " << dst << " " << call << "(" << src << ")\n\n";
  os << "define " << dst << " @" << fn << "(" << src << " %x) {\n";
  os << "  %r = call " << dst << " " << call << "(" << src << " %x)\n";
  os << "  ret " << dst << " %r\n";
  os << "}\n";
  return singleFile(os.str());
}

// ---------------------------------------------------------------------
// llvm.powi: (FP, i32) -> FP. Mangled name @llvm.powi.<fp-mangle>.i32.
// ---------------------------------------------------------------------
std::vector<NamedFile> powiIntrinsic(const Instruction &inst, const Rule &rule,
                                      const std::string &type) {
  if (inst.name != "llvm.powi") return {};
  if (isFmfRule(rule)) return {};
  const char *fp = fpMangle(type);
  if (!fp) return {};
  const std::string fn = fnName(rule, type);
  const std::string call =
      std::string("@llvm.powi.") + fp + ".i32";
  std::ostringstream os;
  os << "declare " << type << " " << call << "(" << type << ", i32)\n\n";
  os << "define " << type << " @" << fn << "(" << type << " %x, i32 %n) {\n";
  os << "  %r = call " << type << " " << call << "(" << type
     << " %x, i32 %n)\n";
  os << "  ret " << type << " %r\n";
  os << "}\n";
  return singleFile(os.str());
}

// ---------------------------------------------------------------------
// Integer vector reductions: llvm.vector.reduce.{add,mul,and,or,xor,
// smax,smin,umax,umin}. <N x intT> -> intT. Type descriptor: v<N>x<elem>.
// Mangled name: @llvm.vector.reduce.<op>.v<N><elem> (e.g. .v4i32).
// ---------------------------------------------------------------------
std::vector<NamedFile> integerVectorReduction(const Instruction &inst,
                                               const Rule &rule,
                                               const std::string &type) {
  if (!kIntegerVectorReductions.count(inst.name)) return {};
  std::string n_str, elem, vec_ty;
  if (!parseVectorDescriptor(type, n_str, elem, vec_ty)) return {};
  if (elem.empty() || elem[0] != 'i') return {};

  const std::string vec_mangle = "v" + n_str + elem;
  const std::string call = "@" + inst.name + "." + vec_mangle;
  const std::string fn = fnName(rule, type);
  std::ostringstream os;
  os << "declare " << elem << " " << call << "(" << vec_ty << ")\n\n";
  os << "define " << elem << " @" << fn << "(" << vec_ty << " %v) {\n";
  os << "  %r = call " << elem << " " << call << "(" << vec_ty << " %v)\n";
  os << "  ret " << elem << " %r\n";
  os << "}\n";
  return singleFile(os.str());
}

// ---------------------------------------------------------------------
// FP vector reductions with start value: llvm.vector.reduce.{fadd,fmul}.
// (start: FP, <N x FP>) -> FP. Mangled name uses .v<N>f32 / .v<N>f64.
// ---------------------------------------------------------------------
std::vector<NamedFile> fpVectorReduction(const Instruction &inst,
                                          const Rule &rule,
                                          const std::string &type) {
  if (!kFpVectorReductions.count(inst.name)) return {};
  if (isFmfRule(rule)) return {};
  std::string n_str, elem, vec_ty;
  if (!parseVectorDescriptor(type, n_str, elem, vec_ty)) return {};
  const char *fp = fpMangle(elem);
  if (!fp) return {};
  const std::string vec_mangle = "v" + n_str + fp;
  const std::string call = "@" + inst.name + "." + vec_mangle;
  const std::string fn = fnName(rule, type);
  std::ostringstream os;
  os << "declare " << elem << " " << call << "(" << elem << ", " << vec_ty
     << ")\n\n";
  os << "define " << elem << " @" << fn << "(" << elem << " %start, " << vec_ty
     << " %v) {\n";
  os << "  %r = call " << elem << " " << call << "(" << elem << " %start, "
     << vec_ty << " %v)\n";
  os << "  ret " << elem << " %r\n";
  os << "}\n";
  return singleFile(os.str());
}

// ---------------------------------------------------------------------
// FP min/max vector reductions: llvm.vector.reduce.{fmax,fmin,fmaximum,
// fminimum}. <N x FP> -> FP. No start value.
// ---------------------------------------------------------------------
std::vector<NamedFile> fpMinMaxVectorReduction(const Instruction &inst,
                                                const Rule &rule,
                                                const std::string &type) {
  if (!kFpMinMaxVectorReductions.count(inst.name)) return {};
  if (isFmfRule(rule)) return {};
  std::string n_str, elem, vec_ty;
  if (!parseVectorDescriptor(type, n_str, elem, vec_ty)) return {};
  const char *fp = fpMangle(elem);
  if (!fp) return {};
  const std::string vec_mangle = "v" + n_str + fp;
  const std::string call = "@" + inst.name + "." + vec_mangle;
  const std::string fn = fnName(rule, type);
  std::ostringstream os;
  os << "declare " << elem << " " << call << "(" << vec_ty << ")\n\n";
  os << "define " << elem << " @" << fn << "(" << vec_ty << " %v) {\n";
  os << "  %r = call " << elem << " " << call << "(" << vec_ty << " %v)\n";
  os << "  ret " << elem << " %r\n";
  os << "}\n";
  return singleFile(os.str());
}

// ---------------------------------------------------------------------
// Saturating fixed-point intrinsics: same shape as fixedPointIntrinsic
// (two same-typed int inputs + i32 scale immarg), but for .sat variants.
// Two scale variants per (rule, type): scale=0 and scale=1.
// ---------------------------------------------------------------------
std::vector<NamedFile> fixedPointSatIntrinsic(const Instruction &inst,
                                               const Rule &rule,
                                               const std::string &type) {
  if (!kFixedPointSatIntrinsics.count(inst.name)) return {};

  std::vector<NamedFile> out;
  for (int scale : {0, 1}) {
    const std::string suf = "scale" + std::to_string(scale);
    const std::string fn = fnName(rule, type, suf);
    const std::string call = "@" + inst.name + "." + type;
    std::ostringstream os;
    os << "declare " << type << " " << call << "(" << type << ", " << type
       << ", i32)\n\n";
    os << "define " << type << " @" << fn << "(" << type << " %a, " << type
       << " %b) {\n";
    os << "  %r = call " << type << " " << call << "(" << type << " %a, "
       << type << " %b, i32 " << scale << ")\n";
    os << "  ret " << type << " %r\n";
    os << "}\n";
    out.push_back({suf, os.str()});
  }
  return out;
}

// ---------------------------------------------------------------------
// llvm.is.fpclass: (FP, i32 immarg) -> i1. Generate three test-mask
// variants per (rule, type) — 3 (any NaN), 516 (any Inf), 783
// (NaN-or-Inf-or-Normal).
// ---------------------------------------------------------------------
std::vector<NamedFile> fpClassQuery(const Instruction &inst, const Rule &rule,
                                     const std::string &type) {
  if (inst.name != "llvm.is.fpclass") return {};
  if (isFmfRule(rule)) return {};
  const char *fp = fpMangle(type);
  if (!fp) return {};

  std::vector<NamedFile> out;
  for (int mask : {3, 516, 783}) {
    const std::string suf = "mask" + std::to_string(mask);
    const std::string fn = fnName(rule, type, suf);
    const std::string call = std::string("@") + inst.name + "." + fp;
    std::ostringstream os;
    os << "declare i1 " << call << "(" << type << ", i32 immarg)\n\n";
    os << "define i1 @" << fn << "(" << type << " %x) {\n";
    os << "  %r = call i1 " << call << "(" << type << " %x, i32 " << mask
       << ")\n";
    os << "  ret i1 %r\n";
    os << "}\n";
    out.push_back({suf, os.str()});
  }
  return out;
}

// ---------------------------------------------------------------------
// llvm.clmul: (intT, intT) -> intT. Carry-less multiply; result is the
// low N bits of the carry-less product, so the return type matches the
// operand type.
// ---------------------------------------------------------------------
std::vector<NamedFile> clmulIntrinsic(const Instruction &inst, const Rule &rule,
                                       const std::string &type) {
  if (inst.name != "llvm.clmul") return {};
  const std::string fn = fnName(rule, type);
  const std::string call = "@" + inst.name + "." + type;
  std::ostringstream os;
  os << "declare " << type << " " << call << "(" << type << ", " << type
     << ")\n\n";
  os << "define " << type << " @" << fn << "(" << type << " %a, " << type
     << " %b) {\n";
  os << "  %r = call " << type << " " << call << "(" << type << " %a, "
     << type << " %b)\n";
  os << "  ret " << type << " %r\n";
  os << "}\n";
  return singleFile(os.str());
}

// ---------------------------------------------------------------------
// llvm.ptrmask: (ptr, intty) -> ptr. The integer type matches the
// pointer-index-type size; for the default address space on x86_64 that
// is i64. We only emit the p0+i64 variant.
// ---------------------------------------------------------------------
std::vector<NamedFile> ptrmaskIntrinsic(const Instruction &inst,
                                         const Rule &rule,
                                         const std::string &type) {
  if (inst.name != "llvm.ptrmask") return {};
  if (type != "i64") return {};
  const std::string fn = fnName(rule, type);
  std::ostringstream os;
  os << "declare ptr @llvm.ptrmask.p0.i64(ptr, i64)\n\n";
  os << "define ptr @" << fn << "(ptr %p, i64 %mask) {\n";
  os << "  %r = call ptr @llvm.ptrmask.p0.i64(ptr %p, i64 %mask)\n";
  os << "  ret ptr %r\n";
  os << "}\n";
  return singleFile(os.str());
}

}  // namespace generators

// ---------------------------------------------------------------------
// Dispatch by instruction name.
// ---------------------------------------------------------------------
GeneratorFn findGenerator(const Instruction &inst) {
  const std::string &n = inst.name;
  if (generators::kSupportedBinaryOps.count(n))
    return &generators::binaryOpWithFlag;
  if (generators::kSupportedOverflowIntrinsics.count(n))
    return &generators::overflowGen;
  if (n == "trunc" || n == "zext" || n == "sext") return &generators::castOp;
  if (n == "icmp" || n == "fcmp") return &generators::comparison;
  if (n == "select") return &generators::selectOp;
  if (n == "freeze") return &generators::freezeOp;
  if (generators::kUnaryIntIntrinsics.count(n))
    return &generators::unaryIntrinsic;
  if (n == "llvm.ctlz" || n == "llvm.cttz")
    return &generators::unaryIntrinsicWithFlag;
  if (generators::kBinaryIntIntrinsics.count(n))
    return &generators::binaryIntrinsic;
  if (n == "llvm.abs") return &generators::absIntrinsic;
  if (n == "llvm.fshl" || n == "llvm.fshr")
    return &generators::ternaryIntrinsic;
  if (n == "fneg") return &generators::fnegOp;
  if (n == "getelementptr") return &generators::gepOp;
  if (n == "load" || n == "store" || n == "alloca")
    return &generators::loadStoreOp;
  if (n == "extractelement" || n == "insertelement")
    return &generators::vectorOp;
  if (generators::kFpMinMaxIntrinsics.count(n))
    return &generators::fpMinMax;
  if (generators::kComparisonIntrinsics.count(n))
    return &generators::comparisonIntrinsic;
  if (generators::kMemoryIntrinsics.count(n))
    return &generators::memoryIntrinsic;
  if (generators::kSatConversionIntrinsics.count(n))
    return &generators::satConversionIntrinsic;
  if (generators::kSatShiftIntrinsics.count(n))
    return &generators::satShiftIntrinsic;
  if (generators::kFixedPointIntrinsics.count(n))
    return &generators::fixedPointIntrinsic;
  if (generators::kUnaryFPIntrinsics.count(n))
    return &generators::unaryFPIntrinsic;
  if (generators::kBinaryFPIntrinsics.count(n))
    return &generators::binaryFPIntrinsic;
  if (generators::kTernaryFPIntrinsics.count(n))
    return &generators::ternaryFPIntrinsic;
  if (generators::kFpToIntIntrinsics.count(n))
    return &generators::fpToIntIntrinsic;
  if (n == "llvm.powi") return &generators::powiIntrinsic;
  if (generators::kIntegerVectorReductions.count(n))
    return &generators::integerVectorReduction;
  if (generators::kFpVectorReductions.count(n))
    return &generators::fpVectorReduction;
  if (generators::kFpMinMaxVectorReductions.count(n))
    return &generators::fpMinMaxVectorReduction;
  if (generators::kFixedPointSatIntrinsics.count(n))
    return &generators::fixedPointSatIntrinsic;
  if (n == "llvm.is.fpclass") return &generators::fpClassQuery;
  if (n == "llvm.clmul") return &generators::clmulIntrinsic;
  if (n == "llvm.ptrmask") return &generators::ptrmaskIntrinsic;
  return nullptr;
}

}  // namespace regatoni::litmus
