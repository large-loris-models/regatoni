// src/mutators/ir_mutations/move_instruction.h
#ifndef REGATONI_MUTATORS_MOVE_INSTRUCTION_H
#define REGATONI_MUTATORS_MOVE_INSTRUCTION_H

#include "src/mutators/base.h"

namespace regatoni {

// Picks a random non-memory, non-terminator, non-phi instruction and
// moves it to a different legal position within its basic block
// (respecting dominance: it stays after its operand-defining instructions
// and before its users in the same block).
class MoveInstruction : public Mutation {
public:
  std::string name() const override { return "move_instruction"; }
  bool canApply(const llvm::Module &M) const override;
  bool apply(llvm::Module &M, std::mt19937 &rng) override;
};

} // namespace regatoni

#endif
