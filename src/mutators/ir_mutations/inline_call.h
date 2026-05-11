// src/mutators/ir_mutations/inline_call.h
#ifndef REGATONI_MUTATORS_INLINE_CALL_H
#define REGATONI_MUTATORS_INLINE_CALL_H

#include "src/mutators/base.h"

namespace regatoni {

// Picks a random direct call site whose callee is a function declaration with
// a signature match in the corpus library (see CorpusIndex). Splices a
// matching corpus function body into the declaration, sets it internal, and
// inlines the call via llvm::InlineFunction.
class InlineCall : public Mutation {
public:
  std::string name() const override { return "inline_call"; }
  bool canApply(const llvm::Module &M) const override;
  bool apply(llvm::Module &M, std::mt19937 &rng) override;
};

} // namespace regatoni

#endif
