// src/mutators/ir_mutations/wrap_bitmanip.h
#ifndef REGATONI_MUTATORS_WRAP_BITMANIP_H
#define REGATONI_MUTATORS_WRAP_BITMANIP_H

#include "src/mutators/base.h"

namespace regatoni {

// Picks an existing integer value %v (width 8/16/32/64) and rewrites its uses to
// flow through a randomly chosen bit-manipulation idiom of the same type:
//   @llvm.bitreverse/bswap/ctpop/ctlz/cttz, a funnel-shift rotate (fshl/fshr),
//   or a pure-IR idiom (x & -x, x & (x-1), (x>>k) ^ x).
//
// These ARE the Zbb / orc.b / rev8 / clz patterns the RISC-V backend selects —
// but applied to arbitrary fuzzer values rather than the canonical shapes the
// regression tests already cover. Uses only generic @llvm.* intrinsics, so the
// result stays inside the clean scalar-integer class the oracle accepts.
class WrapBitmanip : public Mutation {
public:
  std::string name() const override { return "wrap_bitmanip"; }
  bool canApply(const llvm::Module &M) const override;
  bool apply(llvm::Module &M, std::mt19937 &rng) override;
};

} // namespace regatoni

#endif
