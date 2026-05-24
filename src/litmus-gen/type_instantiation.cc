#include "src/litmus-gen/type_instantiation.h"

#include <set>

namespace regatoni::litmus {

namespace {

// Integer type widths in bits, keyed by name.
int widthOf(const std::string &t) {
  if (t == "i1") return 1;
  if (t == "i8") return 8;
  if (t == "i16") return 16;
  if (t == "i32") return 32;
  if (t == "i64") return 64;
  return -1;
}

std::vector<std::string> intsOnly(const std::vector<std::string> &requested) {
  std::vector<std::string> out;
  for (const auto &t : requested)
    if (widthOf(t) > 0) out.push_back(t);
  return out;
}

// Specific (src, dst) pairs requested by the brief. We hard-code these
// instead of cross-producting requested types because the brief is
// prescriptive and we want predictable, small per-cast file counts.
const std::vector<std::string> kTruncPairs = {
    "i32_to_i16", "i32_to_i8", "i64_to_i32", "i16_to_i8",
};
const std::vector<std::string> kExtendPairs = {
    "i16_to_i32", "i8_to_i32", "i32_to_i64", "i8_to_i16",
};

// FP / pointer / aggregate / byte / vector conversions are out of V1
// scope. Listed explicitly so a new conversion in the JSON gets a clean
// "[skip]" rather than wrong-IR output.
const std::set<std::string> kSkippedConversion = {
    "fptoui", "fptosi", "uitofp", "sitofp", "fptrunc", "fpext",
    "ptrtoint", "inttoptr", "bitcast",
};

}  // namespace

std::vector<std::string>
typesForInstruction(const Instruction &inst,
                    const std::vector<std::string> &requested) {
  const std::string &name = inst.name;

  if (name == "trunc") return kTruncPairs;
  if (name == "zext" || name == "sext") return kExtendPairs;
  if (kSkippedConversion.count(name)) return {};

  // FP comparisons not in V1 scope.
  if (name == "fcmp") return {};

  // bswap requires bitwidth % 16 == 0.
  if (name == "llvm.bswap") {
    std::vector<std::string> out;
    for (const auto &t : intsOnly(requested))
      if (widthOf(t) % 16 == 0) out.push_back(t);
    return out;
  }

  // fneg: scalar float / double only.
  if (name == "fneg") return {"float", "double"};

  // FP min/max intrinsic family: scalar float / double.
  if (name == "llvm.minnum" || name == "llvm.maxnum" ||
      name == "llvm.minimum" || name == "llvm.maximum" ||
      name == "llvm.minimumnum" || name == "llvm.maximumnum")
    return {"float", "double"};

  // Three-way comparison intrinsics: i32 or i64 inputs, i8 result.
  // Type descriptor "<in>_to_<out>" is split by generators::splitCastPair.
  if (name == "llvm.scmp" || name == "llvm.ucmp")
    return {"i32_to_i8", "i64_to_i8"};

  // Memory intrinsics: length type only (dest/src are always ptr).
  if (name == "llvm.memcpy" || name == "llvm.memmove" ||
      name == "llvm.memset")
    return {"i64"};

  // Saturating FP-to-int conversions: float/double source, i32/i64 dest.
  // Brief: "float->i32, double->i64 (and reverse widths)".
  if (name == "llvm.fptoui.sat" || name == "llvm.fptosi.sat")
    return {"float_to_i32", "double_to_i64", "float_to_i64",
            "double_to_i32"};

  // extractelement / insertelement: hard-coded fixed-length vector shapes.
  // Descriptor format is v<N>x<elem> (see generators::vectorOp).
  if (name == "extractelement" || name == "insertelement")
    return {"v4xi32", "v2xi64"};

  // Unary / binary / ternary FP intrinsics, FP class query, powi:
  // scalar float / double.
  if (name == "llvm.sqrt" || name == "llvm.sin" || name == "llvm.cos" ||
      name == "llvm.exp" || name == "llvm.exp2" || name == "llvm.log" ||
      name == "llvm.log10" || name == "llvm.log2" || name == "llvm.fabs" ||
      name == "llvm.floor" || name == "llvm.ceil" || name == "llvm.trunc" ||
      name == "llvm.round" || name == "llvm.rint" || name == "llvm.nearbyint" ||
      name == "llvm.roundeven" || name == "llvm.canonicalize" ||
      name == "llvm.pow" || name == "llvm.copysign" || name == "llvm.fma" ||
      name == "llvm.fmuladd" || name == "llvm.powi" ||
      name == "llvm.is.fpclass")
    return {"float", "double"};

  // FP-to-int rounding intrinsics: <src_fp>_to_<dst_int>. The generator
  // filters out invalid (dst != i64) combinations for llround / llrint.
  if (name == "llvm.lround" || name == "llvm.lrint")
    return {"float_to_i32", "float_to_i64", "double_to_i32",
            "double_to_i64"};
  if (name == "llvm.llround" || name == "llvm.llrint")
    return {"float_to_i64", "double_to_i64"};

  // Integer vector reductions: <N x intT> -> intT.
  if (name == "llvm.vector.reduce.add" ||
      name == "llvm.vector.reduce.mul" ||
      name == "llvm.vector.reduce.and" ||
      name == "llvm.vector.reduce.or" ||
      name == "llvm.vector.reduce.xor" ||
      name == "llvm.vector.reduce.smax" ||
      name == "llvm.vector.reduce.smin" ||
      name == "llvm.vector.reduce.umax" ||
      name == "llvm.vector.reduce.umin")
    return {"v4xi32", "v2xi64"};

  // FP vector reductions (start-value variant and min/max).
  if (name == "llvm.vector.reduce.fadd" ||
      name == "llvm.vector.reduce.fmul" ||
      name == "llvm.vector.reduce.fmax" ||
      name == "llvm.vector.reduce.fmin" ||
      name == "llvm.vector.reduce.fmaximum" ||
      name == "llvm.vector.reduce.fminimum")
    return {"v4xfloat", "v2xdouble"};

  // Saturating fixed-point variants: same scalar-int spread as the
  // non-saturating siblings.
  if (name == "llvm.smul.fix.sat" || name == "llvm.umul.fix.sat" ||
      name == "llvm.sdiv.fix.sat" || name == "llvm.udiv.fix.sat")
    return intsOnly(requested);

  // Carry-less multiply: result type matches operand type. Any scalar int.
  if (name == "llvm.clmul") return intsOnly(requested);

  // ptrmask: only the default-address-space + i64-index variant for now.
  if (name == "llvm.ptrmask") return {"i64"};

  // getelementptr: element types i8 / i32 / i64 (one index, i64 typed).
  if (name == "getelementptr") return {"i8", "i32", "i64"};

  // load / store / alloca: alloca + store + load round-trip over i8/i32/i64.
  if (name == "load" || name == "store" || name == "alloca")
    return {"i8", "i32", "i64"};

  // Default: any requested scalar integer type. Used by binary ops,
  // overflow intrinsics, comparisons, select, freeze, and the various
  // unary/binary/ternary integer intrinsics.
  return intsOnly(requested);
}

}  // namespace regatoni::litmus
