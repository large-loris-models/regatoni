// src/mutators/ir_mutations/change_constant.h
#ifndef REGATONI_MUTATORS_CHANGE_CONSTANT_H
#define REGATONI_MUTATORS_CHANGE_CONSTANT_H

#include "src/mutators/base.h"

namespace regatoni {

// Replaces an integer ConstantInt operand of a binop / icmp / select with a
// *different* constant of the same type, drawn either from a curated pool of
// "interesting" values (0, 1, -1, signbit, 2^k, 2^k±1, byte/SWAR masks such as
// 0x0101.., min/max, the bit-width itself) or from a structured perturbation of
// the current value (±1, negate, single-bit flip, rotate-by-8).
//
// Rationale: instruction selection and the GlobalISel/SelectionDAG combiners are
// overwhelmingly *constant-keyed* (mul x,2^k -> shl, and x,mask, orc.b/rev8/clmul
// SWAR magics, off-by-one near 2^k pattern guards). Our other mutations reshuffle
// existing structure but never manufacture these triggers — this one does. The
// confirmed `mul nuw x,-1` bug needed exactly the constant -1.
class ChangeConstant : public Mutation {
public:
  std::string name() const override { return "change_constant"; }
  bool canApply(const llvm::Module &M) const override;
  bool apply(llvm::Module &M, std::mt19937 &rng) override;
};

} // namespace regatoni

#endif
