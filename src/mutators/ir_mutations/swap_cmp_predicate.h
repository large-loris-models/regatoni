// src/mutators/ir_mutations/swap_cmp_predicate.h
#ifndef REGATONI_MUTATORS_SWAP_CMP_PREDICATE_H
#define REGATONI_MUTATORS_SWAP_CMP_PREDICATE_H

#include "src/mutators/base.h"

namespace regatoni {

// Picks a random icmp/fcmp instruction and changes its predicate to a
// different compatible one. e.g., eq↔ne, slt↔sgt, oeq↔une.
class SwapCmpPredicate : public Mutation {
public:
  std::string name() const override { return "swap_cmp_predicate"; }
  bool canApply(const llvm::Module &M) const override;
  bool apply(llvm::Module &M, std::mt19937 &rng) override;
};

} // namespace regatoni

#endif
