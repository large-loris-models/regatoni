// src/mutators/ir_mutations/mutate_shift_amount.h
#ifndef REGATONI_MUTATORS_MUTATE_SHIFT_AMOUNT_H
#define REGATONI_MUTATORS_MUTATE_SHIFT_AMOUNT_H

#include "src/mutators/base.h"

namespace regatoni {

// Drives the shift amount of a shl/lshr/ashr (or the rotate amount of a
// funnel-shift intrinsic fshl/fshr) to a *boundary* value:
//   {0, 1, 7, 8, 15, 16, 23, 24, 31, 32, bw/2, bw-1, bw, bw+1}.
// Byte multiples (8/16/24) are exactly the amounts that trigger byte-swap /
// orc.b / rev8 selection; bw-1 / bw / bw+1 sit on the UB-edge (shift >= width is
// poison) and the rotate-idiom boundaries that the combiners reason hardest
// about. A specialization of change_constant aimed at the one operand where
// boundary values matter most.
class MutateShiftAmount : public Mutation {
public:
  std::string name() const override { return "mutate_shift_amount"; }
  bool canApply(const llvm::Module &M) const override;
  bool apply(llvm::Module &M, std::mt19937 &rng) override;
};

} // namespace regatoni

#endif
