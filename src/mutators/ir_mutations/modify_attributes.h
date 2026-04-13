// src/mutators/ir_mutations/modify_attributes.h
#ifndef REGATONI_MUTATORS_MODIFY_ATTRIBUTES_H
#define REGATONI_MUTATORS_MODIFY_ATTRIBUTES_H

#include "src/mutators/base.h"

namespace regatoni {

// Picks a random function and toggles sext/zext on a random integer
// parameter or on the integer return value.
class ModifyAttributes : public Mutation {
public:
  std::string name() const override { return "modify_attributes"; }
  bool canApply(const llvm::Module &M) const override;
  bool apply(llvm::Module &M, std::mt19937 &rng) override;
};

} // namespace regatoni

#endif
