// src/mutators/ir_mutations/swap_binop.cc
#include "src/mutators/ir_mutations/swap_binop.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Operator.h"
#include <vector>

namespace regatoni {

static llvm::Instruction::BinaryOps swapOpcode(llvm::Instruction::BinaryOps Op,
                                                std::mt19937 &rng) {
  using BO = llvm::Instruction::BinaryOps;

  static const BO intArith[] = {BO::Add, BO::Sub, BO::Mul, BO::SDiv, BO::UDiv,
                                BO::SRem, BO::URem};
  static const BO bitwise[] = {BO::And, BO::Or, BO::Xor};
  static const BO shifts[] = {BO::Shl, BO::LShr, BO::AShr};
  static const BO fpArith[] = {BO::FAdd, BO::FSub, BO::FMul, BO::FDiv,
                                BO::FRem};

  auto pickDifferent = [&](const BO *arr, size_t len) -> BO {
    std::uniform_int_distribution<size_t> dist(0, len - 1);
    BO result;
    do {
      result = arr[dist(rng)];
    } while (result == Op && len > 1);
    return result;
  };

  for (auto op : intArith)
    if (op == Op) return pickDifferent(intArith, 7);
  for (auto op : bitwise)
    if (op == Op) return pickDifferent(bitwise, 3);
  for (auto op : shifts)
    if (op == Op) return pickDifferent(shifts, 3);
  for (auto op : fpArith)
    if (op == Op) return pickDifferent(fpArith, 5);

  return Op;
}

// Set flags on a freshly-created BinaryOperator based on its opcode class.
// Mirrors alive-mutate's resetMathFlags dispatch
// (tools/mutator-utils/mutator_helper.cpp:810-843):
//   Add/Sub/Mul/Shl           → random NUW, NSW
//   UDiv/SDiv/LShr/AShr       → random exact
//   FAdd/FSub/FMul/FDiv/FRem  → random FastMathFlags
//   URem/SRem/And/Or/Xor      → no flags to set
//
// `BinaryOperator::Create` yields an instruction with all flags cleared,
// so no stale flags can leak from the old opcode. This function only
// *adds* diversity, never resets incompatible state.
static void randomizeFlags(llvm::BinaryOperator *inst, std::mt19937 &rng) {
  using BO = llvm::Instruction::BinaryOps;
  std::bernoulli_distribution coin(0.5);
  auto op = inst->getOpcode();
  switch (op) {
  case BO::Add:
  case BO::Sub:
  case BO::Mul:
  case BO::Shl:
    inst->setHasNoSignedWrap(coin(rng));
    inst->setHasNoUnsignedWrap(coin(rng));
    break;
  case BO::UDiv:
  case BO::SDiv:
  case BO::LShr:
  case BO::AShr:
    inst->setIsExact(coin(rng));
    break;
  case BO::FAdd:
  case BO::FSub:
  case BO::FMul:
  case BO::FDiv:
  case BO::FRem:
    if (llvm::isa<llvm::FPMathOperator>(inst)) {
      llvm::FastMathFlags flags;
      flags.setAllowContract(coin(rng));
      flags.setAllowReassoc(coin(rng));
      flags.setAllowReciprocal(coin(rng));
      flags.setApproxFunc(coin(rng));
      flags.setNoInfs(coin(rng));
      flags.setNoNaNs(coin(rng));
      flags.setNoSignedZeros(coin(rng));
      inst->setFastMathFlags(flags);
    }
    break;
  default:
    break;
  }
}

bool SwapBinOp::canApply(const llvm::Module &M) const {
  for (const auto &F : M)
    for (const auto &BB : F)
      for (const auto &I : BB)
        if (llvm::isa<llvm::BinaryOperator>(I))
          return true;
  return false;
}

bool SwapBinOp::apply(llvm::Module &M, std::mt19937 &rng) {
  std::vector<llvm::BinaryOperator *> binops;
  for (auto &F : M)
    for (auto &BB : F)
      for (auto &I : BB)
        if (auto *BO = llvm::dyn_cast<llvm::BinaryOperator>(&I))
          binops.push_back(BO);

  if (binops.empty())
    return false;

  std::uniform_int_distribution<size_t> dist(0, binops.size() - 1);
  auto *target = binops[dist(rng)];

  auto oldOp = static_cast<llvm::Instruction::BinaryOps>(target->getOpcode());
  auto newOp = swapOpcode(oldOp, rng);

  if (newOp == oldOp)
    return false;

  // Create replacement instruction. A fresh BinaryOperator has no flags,
  // so the old op's flags (e.g. nsw on Add when swapping to UDiv) cannot
  // leak into the new instruction. We then randomize flags appropriate
  // to the *new* opcode for mutation diversity.
  auto *newInst = llvm::BinaryOperator::Create(
      newOp, target->getOperand(0), target->getOperand(1), "", target->getIterator());
  newInst->takeName(target);
  randomizeFlags(newInst, rng);
  target->replaceAllUsesWith(newInst);
  target->eraseFromParent();

  return true;
}

} // namespace regatoni
