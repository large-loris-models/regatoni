// src/mutators/ir_mutations/replace_operand.h
#ifndef REGATONI_MUTATORS_REPLACE_OPERAND_H
#define REGATONI_MUTATORS_REPLACE_OPERAND_H

#include "src/mutators/base.h"

namespace regatoni {

// Picks a random binary/cmp instruction and replaces one of its operands
// with a different value of the same type: a function argument, a
// dominating instruction (earlier in the same basic block), or a simple
// constant (0, 1, -1, 0.0, 1.0).
class ReplaceOperand : public Mutation {
public:
  std::string name() const override { return "replace_operand"; }
  bool canApply(const llvm::Module &M) const override;
  bool apply(llvm::Module &M, std::mt19937 &rng) override;
};

} // namespace regatoni

#endif
