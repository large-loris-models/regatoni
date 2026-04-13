// src/mutators/base.h — Mutation interface for regatoni
//
// Every mutation is a self-contained class that operates on an llvm::Module.
// To add a new mutation:
//   1. Create a .h/.cc file in src/mutators/ir_mutations/ or alive_mutations/
//   2. Inherit from Mutation
//   3. Register it in registry.cc

#ifndef REGATONI_MUTATORS_BASE_H
#define REGATONI_MUTATORS_BASE_H

#include "llvm/IR/Module.h"
#include <random>
#include <string>

namespace regatoni {

class Mutation {
public:
  virtual ~Mutation() = default;

  // Human-readable name (e.g., "swap_binop", "change_constant")
  virtual std::string name() const = 0;

  // Can this mutation be applied to this module?
  // Return false if the module doesn't have the right structure
  // (e.g., no binary ops to swap).
  virtual bool canApply(const llvm::Module &M) const = 0;

  // Apply the mutation in-place. Returns true if something was changed.
  // The rng should be used for all randomness so mutations are reproducible.
  virtual bool apply(llvm::Module &M, std::mt19937 &rng) = 0;
};

} // namespace regatoni

#endif // REGATONI_MUTATORS_BASE_H