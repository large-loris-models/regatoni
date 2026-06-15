// src/mutators/ir_mutations/demote_intrinsic_to_expansion.h
#ifndef REGATONI_MUTATORS_DEMOTE_INTRINSIC_TO_EXPANSION_H
#define REGATONI_MUTATORS_DEMOTE_INTRINSIC_TO_EXPANSION_H

#include "src/mutators/base.h"

namespace regatoni {

// Replaces a @llvm.ctpop / @llvm.bswap call with its bit-twiddle expansion
// (SWAR popcount / byte-reverse shift-or). This tests whether the backend
// *re-recognizes* the idiom and selects the Zbb instruction (cpop / rev8 /
// orc.b family) — the kind of multi-instruction pattern-match where SelectionDAG
// bugs hide. Safe regardless of whether the expansion equals the intrinsic: the
// oracle checks that the backend correctly compiles the *expansion* (the new
// source of truth), not that the expansion equals ctpop/bswap.
class DemoteIntrinsicToExpansion : public Mutation {
public:
  std::string name() const override { return "demote_intrinsic_to_expansion"; }
  bool canApply(const llvm::Module &M) const override;
  bool apply(llvm::Module &M, std::mt19937 &rng) override;
};

} // namespace regatoni

#endif
