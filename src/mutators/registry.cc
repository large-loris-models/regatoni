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
#include "src/mutators/ir_mutations/modify_attributes.h"
#include "src/mutators/ir_mutations/mutate_gep.h"
#include "src/mutators/ir_mutations/resize_type.h"
#include "src/mutators/ir_mutations/mutate_unary.h"
#include "src/mutators/ir_mutations/eliminate_undef.h"
// Add new mutation headers here as you implement them:
// #include "src/mutators/ir_mutations/change_constant.h"
// #include "src/mutators/alive_mutations/modify_flags.h"

#include <algorithm>
#include <cstdio>

namespace regatoni {

void MutationRegistry::add(std::unique_ptr<Mutation> m) {
  mutations_.push_back(std::move(m));
}

std::string MutationRegistry::applyRandom(llvm::Module &M, std::mt19937 &rng) {
  // Collect applicable mutations
  std::vector<Mutation *> applicable;
  for (auto &m : mutations_)
    if (m->canApply(M))
      applicable.push_back(m.get());

  if (applicable.empty())
    return "";

  // Equal weight for now — pick uniformly at random
  std::uniform_int_distribution<size_t> dist(0, applicable.size() - 1);
  auto *chosen = applicable[dist(rng)];

  fprintf(stderr, "applyRandom: chose mutation: %s\n", chosen->name().c_str());
  fflush(stderr);

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
    reg.add(std::make_unique<ModifyAttributes>());
    reg.add(std::make_unique<MutateGep>());
    reg.add(std::make_unique<ResizeType>());
    reg.add(std::make_unique<MutateUnary>());
    reg.add(std::make_unique<EliminateUndef>());
    // reg.add(std::make_unique<ChangeConstant>());
    // reg.add(std::make_unique<ModifyFlags>());
    initialized = true;
  }
  return reg;
}

} // namespace regatoni