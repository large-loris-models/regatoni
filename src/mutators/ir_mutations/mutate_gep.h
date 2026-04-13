// src/mutators/ir_mutations/mutate_gep.h
#ifndef REGATONI_MUTATORS_MUTATE_GEP_H
#define REGATONI_MUTATORS_MUTATE_GEP_H

#include "src/mutators/base.h"

namespace regatoni {

// Picks a random getelementptr instruction and either toggles its
// `inbounds` flag or changes one of its constant integer indices to a
// different small constant.
class MutateGep : public Mutation {
public:
  std::string name() const override { return "mutate_gep"; }
  bool canApply(const llvm::Module &M) const override;
  bool apply(llvm::Module &M, std::mt19937 &rng) override;
};

} // namespace regatoni

#endif
