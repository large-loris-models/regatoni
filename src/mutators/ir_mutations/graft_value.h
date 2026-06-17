// src/mutators/ir_mutations/graft_value.h — gadget-graft crossover.
//
// Transplants a pure scalar-integer value sub-DAG ("gadget") from a corpus donor
// into the host function's dataflow: clones the donor's entry-block integer
// computation to the front of a host block (wiring its inputs to host args /
// constants), then feeds one resulting value into a host instruction operand
// (with a trunc/sext/zext adapter if widths differ). The result is a single
// self-contained function with NO residual calls — sound for intraprocedural
// codegen testing — that now carries co-occurring features of both parents
// (a step toward df=0 *combination* cells that local mutation can't reach).
#ifndef REGATONI_MUTATORS_GRAFT_VALUE_H
#define REGATONI_MUTATORS_GRAFT_VALUE_H

#include "src/mutators/base.h"

namespace llvm { class Function; }

namespace regatoni {

// Core graft: clone a pure-integer value sub-DAG from Donor into Host's dataflow
// and feed one grafted value into a host operand. Host and Donor must live in the
// SAME LLVMContext (they may be in different Modules). Returns true if grafted.
// Used by the GraftValue mutation (donor = seed library) and by the native
// LLVMFuzzerCustomCrossOver hook (donor = the other evolved parent).
bool graftGadgetInto(llvm::Function &Host, llvm::Function &Donor,
                     std::mt19937 &rng);

class GraftValue : public Mutation {
public:
  std::string name() const override { return "graft_value"; }
  bool canApply(const llvm::Module &M) const override;
  bool apply(llvm::Module &M, std::mt19937 &rng) override;
};

} // namespace regatoni

#endif // REGATONI_MUTATORS_GRAFT_VALUE_H
