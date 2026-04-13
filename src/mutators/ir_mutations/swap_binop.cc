// src/mutators/ir_mutations/swap_binop.cc
#include "src/mutators/ir_mutations/swap_binop.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/IRBuilder.h"
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

  // Create replacement instruction
  auto *newInst = llvm::BinaryOperator::Create(
      newOp, target->getOperand(0), target->getOperand(1), "", target->getIterator());
  newInst->takeName(target);
  target->replaceAllUsesWith(newInst);
  target->eraseFromParent();

  return true;
}

} // namespace regatoni