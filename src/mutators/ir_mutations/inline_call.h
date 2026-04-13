// src/mutators/ir_mutations/inline_call.h
#ifndef REGATONI_MUTATORS_INLINE_CALL_H
#define REGATONI_MUTATORS_INLINE_CALL_H

#include "src/mutators/base.h"

namespace regatoni {

// Picks a random direct call site whose callee is a function with a
// body and inlines it via llvm::InlineFunction.
class InlineCall : public Mutation {
public:
  std::string name() const override { return "inline_call"; }
  bool canApply(const llvm::Module &M) const override;
  bool apply(llvm::Module &M, std::mt19937 &rng) override;
};

} // namespace regatoni

#endif
