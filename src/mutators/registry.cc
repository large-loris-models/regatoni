// src/mutators/registry.cc
#include "src/mutators/registry.h"
#include "src/mutators/ir_mutations/swap_binop.h"
#include "src/mutators/ir_mutations/swap_cmp_predicate.h"
#include "src/mutators/ir_mutations/modify_flags.h"
#include "src/mutators/ir_mutations/swap_operands.h"
#include "src/mutators/ir_mutations/replace_operand.h"
#include "src/mutators/ir_mutations/shuffle_instructions.h"
#include "src/mutators/ir_mutations/move_instruction.h"
#include "src/mutators/ir_mutations/inline_call.h"
#include "src/mutators/ir_mutations/remove_void_call.h"
#include "src/mutators/ir_mutations/resize_type.h"
#include "src/mutators/ir_mutations/eliminate_undef.h"
// Scalar-integer / RISC-V isel-focused mutations:
#include "src/mutators/ir_mutations/change_constant.h"
#include "src/mutators/ir_mutations/mutate_shift_amount.h"
#include "src/mutators/ir_mutations/wrap_bitmanip.h"
// Pattern-selection mutations (SDAG-leaning / canonicalization divergence):
#include "src/mutators/ir_mutations/arith_identity_substitution.h"
#include "src/mutators/ir_mutations/narrow_then_widen.h"
#include "src/mutators/ir_mutations/demote_intrinsic_to_expansion.h"
// Crossover (global search): graft a value sub-DAG from a corpus donor.
#include "src/mutators/ir_mutations/graft_value.h"
// Pruned for the scalar-integer backend-isel campaign (2026-06-13):
//   modify_attributes — injected interrupt/CC attrs -> backend-TV false positives
//   mutate_gep        — pointer-only; never applies to the integer corpus
//   mutate_unary      — FP-only (fneg); never applies to the integer corpus

#include <algorithm>

namespace regatoni {

void MutationRegistry::add(std::unique_ptr<Mutation> m) {
  mutations_.push_back(std::move(m));
}

std::string MutationRegistry::applyRandom(llvm::Module &M, std::mt19937 &rng,
                                          int *selected_idx_out) {
  // Collect applicable mutations
  std::vector<Mutation *> applicable;
  for (auto &m : mutations_)
    if (m->canApply(M))
      applicable.push_back(m.get());

  if (applicable.empty()) {
    if (selected_idx_out) *selected_idx_out = -1;
    return "";
  }

  // Equal weight for now — pick uniformly at random
  std::uniform_int_distribution<size_t> dist(0, applicable.size() - 1);
  auto *chosen = applicable[dist(rng)];

  if (selected_idx_out) {
    *selected_idx_out = -1;
    for (size_t i = 0; i < mutations_.size(); ++i) {
      if (mutations_[i].get() == chosen) {
        *selected_idx_out = static_cast<int>(i);
        break;
      }
    }
  }

  if (chosen->apply(M, rng))
    return chosen->name();

  return "";
}

MutationRegistry &MutationRegistry::instance() {
  static MutationRegistry reg;
  static bool initialized = false;
  if (!initialized) {
    // Register all mutations here
    reg.add(std::make_unique<SwapBinOp>());
    reg.add(std::make_unique<SwapCmpPredicate>());
    reg.add(std::make_unique<ModifyFlags>());
    reg.add(std::make_unique<SwapOperands>());
    reg.add(std::make_unique<ReplaceOperand>());
    reg.add(std::make_unique<ShuffleInstructions>());
    reg.add(std::make_unique<MoveInstruction>());
    reg.add(std::make_unique<InlineCall>());
    reg.add(std::make_unique<RemoveVoidCall>());
    reg.add(std::make_unique<ResizeType>());
    reg.add(std::make_unique<EliminateUndef>());
    reg.add(std::make_unique<ChangeConstant>());
    reg.add(std::make_unique<MutateShiftAmount>());
    reg.add(std::make_unique<WrapBitmanip>());
    reg.add(std::make_unique<ArithIdentitySubstitution>());
    reg.add(std::make_unique<NarrowThenWiden>());
    reg.add(std::make_unique<DemoteIntrinsicToExpansion>());
    reg.add(std::make_unique<GraftValue>());
    initialized = true;
  }
  return reg;
}

} // namespace regatoni