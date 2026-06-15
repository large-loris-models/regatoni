// src/mutators/ir_mutations/narrow_then_widen.h
#ifndef REGATONI_MUTATORS_NARROW_THEN_WIDEN_H
#define REGATONI_MUTATORS_NARROW_THEN_WIDEN_H

#include "src/mutators/base.h"

namespace regatoni {

// Rewrites a wide (>32-bit) integer binop as sext(op(trunc a, trunc b)) in i32,
// i.e. manufactures the `sext (i32 op)` shape that drives RISC-V's 32-bit-on-
// 64-bit-register selection (addw/subw/sllw/mulw/divw...) and the redundant
// sign-extension elimination (SExtWRemoval / hasAllWUsers). That path is a
// classic SelectionDAG isel hotspot and diverges between dagisel and gisel.
class NarrowThenWiden : public Mutation {
public:
  std::string name() const override { return "narrow_then_widen"; }
  bool canApply(const llvm::Module &M) const override;
  bool apply(llvm::Module &M, std::mt19937 &rng) override;
};

} // namespace regatoni

#endif
