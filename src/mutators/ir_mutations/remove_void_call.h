// src/mutators/ir_mutations/remove_void_call.h
#ifndef REGATONI_MUTATORS_REMOVE_VOID_CALL_H
#define REGATONI_MUTATORS_REMOVE_VOID_CALL_H

#include "src/mutators/base.h"

namespace regatoni {

// Picks a random void-returning CallInst and erases it.
class RemoveVoidCall : public Mutation {
public:
  std::string name() const override { return "remove_void_call"; }
  bool canApply(const llvm::Module &M) const override;
  bool apply(llvm::Module &M, std::mt19937 &rng) override;
};

} // namespace regatoni

#endif
