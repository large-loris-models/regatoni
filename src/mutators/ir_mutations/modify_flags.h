// src/mutators/ir_mutations/modify_flags.h
#ifndef REGATONI_MUTATORS_MODIFY_FLAGS_H
#define REGATONI_MUTATORS_MODIFY_FLAGS_H

#include "src/mutators/base.h"

namespace regatoni {

// Toggles poison-generating and fast-math flags on a random eligible
// instruction: nuw/nsw on OverflowingBinaryOperator, exact on
// PossiblyExactOperator, and FastMathFlags on FPMathOperator.
class ModifyFlags : public Mutation {
public:
  std::string name() const override { return "modify_flags"; }
  bool canApply(const llvm::Module &M) const override;
  bool apply(llvm::Module &M, std::mt19937 &rng) override;
};

} // namespace regatoni

#endif
