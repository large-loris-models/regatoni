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

  // Default: any requested scalar integer type. Used by binary ops,
  // overflow intrinsics, comparisons, select, freeze, and the various
  // unary/binary/ternary integer intrinsics.
  return intsOnly(requested);
}

}  // namespace regatoni::litmus
