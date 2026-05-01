// src/mutators/registry.h — Central mutation registry
#ifndef REGATONI_MUTATORS_REGISTRY_H
#define REGATONI_MUTATORS_REGISTRY_H

#include "src/mutators/base.h"
#include <memory>
#include <random>
#include <vector>

namespace regatoni {

class MutationRegistry {
public:
  // Register a mutation. Call this in registry.cc for each mutation.
  void add(std::unique_ptr<Mutation> m);

  // Pick a random applicable mutation and apply it.
  // Returns the name of the mutation applied, or "" if none applied.
  // If selected_idx_out is non-null, it receives the registry index of the
  // mutator that was *selected* (regardless of whether apply succeeded), or
  // -1 if no mutator was applicable.
  std::string applyRandom(llvm::Module &M, std::mt19937 &rng,
                          int *selected_idx_out = nullptr);

  // Get all registered mutations
  const std::vector<std::unique_ptr<Mutation>> &all() const { return mutations_; }

  // Singleton
  static MutationRegistry &instance();

private:
  std::vector<std::unique_ptr<Mutation>> mutations_;
};

} // namespace regatoni

#endif