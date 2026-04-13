// src/mutators/ir_mutations/eliminate_undef.h
#ifndef REGATONI_MUTATORS_ELIMINATE_UNDEF_H
#define REGATONI_MUTATORS_ELIMINATE_UNDEF_H

#include "src/mutators/base.h"

namespace regatoni {

// Finds a random use of `undef` or `poison` as an operand of a regular
// instruction and replaces it with a concrete value of the same type
// (a constant, an argument, or a dominating value).
class EliminateUndef : public Mutation {
public:
  std::string name() const override { return "eliminate_undef"; }
  bool canApply(const llvm::Module &M) const override;
  bool apply(llvm::Module &M, std::mt19937 &rng) override;
};

} // namespace regatoni

#endif
