// src/mutators/ir_mutations/shuffle_instructions.h
#ifndef REGATONI_MUTATORS_SHUFFLE_INSTRUCTIONS_H
#define REGATONI_MUTATORS_SHUFFLE_INSTRUCTIONS_H

#include "src/mutators/base.h"

namespace regatoni {

// Picks two independent, adjacent instructions in a basic block and
// reorders them (moves the second before the first). "Independent"
// means the later one does not directly use the earlier one's result
// and neither reads or writes memory.
class ShuffleInstructions : public Mutation {
public:
  std::string name() const override { return "shuffle_instructions"; }
  bool canApply(const llvm::Module &M) const override;
  bool apply(llvm::Module &M, std::mt19937 &rng) override;
};

} // namespace regatoni

#endif
