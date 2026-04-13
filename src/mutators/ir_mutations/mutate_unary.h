// src/mutators/ir_mutations/mutate_unary.h
#ifndef REGATONI_MUTATORS_MUTATE_UNARY_H
#define REGATONI_MUTATORS_MUTATE_UNARY_H

#include "src/mutators/base.h"

namespace regatoni {

// Picks a random floating-point binary operator and replaces it with
// `fneg` applied to one of its operands.
class MutateUnary : public Mutation {
public:
  std::string name() const override { return "mutate_unary"; }
  bool canApply(const llvm::Module &M) const override;
  bool apply(llvm::Module &M, std::mt19937 &rng) override;
};

} // namespace regatoni

#endif
