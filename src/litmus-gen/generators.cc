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
  return nullptr;
}

}  // namespace regatoni::litmus
