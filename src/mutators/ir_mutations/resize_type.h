// src/mutators/ir_mutations/resize_type.h
#ifndef REGATONI_MUTATORS_RESIZE_TYPE_H
#define REGATONI_MUTATORS_RESIZE_TYPE_H

#include "src/mutators/base.h"

namespace regatoni {

// Picks a random integer or floating-point binary operator and performs
// the operation at a different bit width, inserting extend/truncate
// casts so the surrounding IR continues to typecheck. E.g., `add i32`
// becomes sext-to-i64, add i64, trunc-to-i32.
class ResizeType : public Mutation {
public:
  std::string name() const override { return "resize_type"; }
  bool canApply(const llvm::Module &M) const override;
  bool apply(llvm::Module &M, std::mt19937 &rng) override;
};

} // namespace regatoni

#endif
