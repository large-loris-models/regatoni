// rewrite-seeds: predicate evaluation.
//
// `findMatches(F, R)` walks F and returns every location at which R's
// match predicates hold conjunctively. The transform engine then clones
// the module once per location and applies R.transform.

#pragma once

#include "src/rewrite-seeds/spec.h"

#include "llvm/IR/Function.h"
#include "llvm/IR/Instruction.h"

#include <vector>

namespace rewrite_seeds {

struct MatchLocation {
  enum Kind { kFunction, kInstruction, kParameter };
  Kind kind;

  const llvm::Instruction *inst = nullptr;  // for kInstruction
  unsigned param_idx = 0;                   // for kParameter
  int      operand_idx = -1;                // for kInstruction when match
                                            //   pinned an operand
};

std::vector<MatchLocation> findMatches(const llvm::Function &F,
                                       const Rewrite &R);

// Exposed for unit-test reach: true iff `flag` is currently set on `I`.
bool instHasFlag(const llvm::Instruction &I, const std::string &flag);

}  // namespace rewrite_seeds
