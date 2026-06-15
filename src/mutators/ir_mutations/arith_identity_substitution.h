// src/mutators/ir_mutations/arith_identity_substitution.h
#ifndef REGATONI_MUTATORS_ARITH_IDENTITY_SUBSTITUTION_H
#define REGATONI_MUTATORS_ARITH_IDENTITY_SUBSTITUTION_H

#include "src/mutators/base.h"

namespace regatoni {

// Rewrites an integer op into a semantically-equivalent *different shape* that
// the SelectionDAG and GlobalISel canonicalizers normalize differently:
//   mul x, 2^k  <->  shl x, k          add x, x  ->  shl x, 1
//   mul x, -1   <->  sub 0, x
// Manufactures the dagisel/gisel canonicalization divergence where isel bugs
// live (the confirmed nuw bug sat in exactly this mul<->sub zone). Wrap/exact
// flags are intentionally dropped on the rewrite — we want the shape difference,
// not to re-trigger the known no-wrap-flag bug family.
class ArithIdentitySubstitution : public Mutation {
public:
  std::string name() const override { return "arith_identity_substitution"; }
  bool canApply(const llvm::Module &M) const override;
  bool apply(llvm::Module &M, std::mt19937 &rng) override;
};

} // namespace regatoni

#endif
