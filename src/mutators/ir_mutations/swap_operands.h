// src/mutators/ir_mutations/swap_operands.h
#ifndef REGATONI_MUTATORS_SWAP_OPERANDS_H
#define REGATONI_MUTATORS_SWAP_OPERANDS_H

#include "src/mutators/base.h"

namespace regatoni {

// Swaps the left and right operands of a random binary operator or
// cmp instruction.
class SwapOperands : public Mutation {
public:
  std::string name() const override { return "swap_operands"; }
  bool canApply(const llvm::Module &M) const override;
  bool apply(llvm::Module &M, std::mt19937 &rng) override;
};

} // namespace regatoni

#endif
