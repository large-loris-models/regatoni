// src/mutators/ir_mutations/swap_binop.h
#ifndef REGATONI_MUTATORS_SWAP_BINOP_H
#define REGATONI_MUTATORS_SWAP_BINOP_H

#include "src/mutators/base.h"

namespace regatoni {

// Picks a random binary operator and swaps it for a compatible one.
// e.g., add → sub, mul → sdiv, and → or, shl → lshr
class SwapBinOp : public Mutation {
public:
  std::string name() const override { return "swap_binop"; }
  bool canApply(const llvm::Module &M) const override;
  bool apply(llvm::Module &M, std::mt19937 &rng) override;
};

} // namespace regatoni

#endif