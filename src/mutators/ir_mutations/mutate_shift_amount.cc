// src/mutators/ir_mutations/mutate_shift_amount.cc
#include "src/mutators/ir_mutations/mutate_shift_amount.h"
#include "llvm/ADT/APInt.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/Intrinsics.h"
#include <vector>

namespace regatoni {

// (instruction, operand index of the shift/rotate amount).
namespace {
struct Slot {
  llvm::Instruction *I;
  unsigned op;
};
} // namespace

static bool isFunnelShift(const llvm::Instruction &I) {
  if (auto *II = llvm::dyn_cast<llvm::IntrinsicInst>(&I)) {
    auto id = II->getIntrinsicID();
    return id == llvm::Intrinsic::fshl || id == llvm::Intrinsic::fshr;
  }
  return false;
}

static void collectSlots(llvm::Module &M, std::vector<Slot> &out) {
  for (auto &F : M)
    for (auto &BB : F)
      for (auto &I : BB) {
        if (auto *BO = llvm::dyn_cast<llvm::BinaryOperator>(&I)) {
          auto op = BO->getOpcode();
          if (op == llvm::Instruction::Shl || op == llvm::Instruction::LShr ||
              op == llvm::Instruction::AShr)
            out.push_back({&I, 1}); // amount is operand 1
        } else if (isFunnelShift(I)) {
          out.push_back({&I, 2}); // fshl/fshr(a, b, amt) — amt is arg 2
        }
      }
}

bool MutateShiftAmount::canApply(const llvm::Module &M) const {
  for (const auto &F : M)
    for (const auto &BB : F)
      for (const auto &I : BB) {
        if (auto *BO = llvm::dyn_cast<llvm::BinaryOperator>(&I)) {
          auto op = BO->getOpcode();
          if (op == llvm::Instruction::Shl || op == llvm::Instruction::LShr ||
              op == llvm::Instruction::AShr)
            return true;
        } else if (isFunnelShift(I)) {
          return true;
        }
      }
  return false;
}

bool MutateShiftAmount::apply(llvm::Module &M, std::mt19937 &rng) {
  std::vector<Slot> slots;
  collectSlots(M, slots);
  if (slots.empty())
    return false;

  std::uniform_int_distribution<size_t> pickSlot(0, slots.size() - 1);
  Slot s = slots[pickSlot(rng)];
  llvm::Value *amt = s.I->getOperand(s.op);
  auto *ty = llvm::dyn_cast<llvm::IntegerType>(amt->getType());
  if (!ty)
    return false; // scalar integer only (vectors are filtered upstream anyway)
  unsigned bw = ty->getBitWidth();

  // Boundary amounts, kept strictly < bw: a shift amount >= bitwidth is poison
  // (and for funnel shifts it's just amt mod bw), which only floods the oracle
  // with "source more defined than target" refinement noise — not isel signal.
  // bw-1 (the max valid shift) is kept as the interesting in-range edge.
  uint64_t raw[] = {0, 1, 7, 8, 15, 16, 23, 24, 31, 32, bw / 2, bw - 1};
  std::vector<uint64_t> cand;
  for (uint64_t c : raw)
    if (c < bw)
      cand.push_back(c);
  if (cand.empty())
    return false; // bw <= 1: no distinct in-range amount worth setting

  // Current value (if constant) so we can guarantee a real change.
  bool haveCur = false;
  llvm::APInt cur(bw, 0);
  if (auto *CI = llvm::dyn_cast<llvm::ConstantInt>(amt)) {
    cur = CI->getValue();
    haveCur = true;
  }

  std::uniform_int_distribution<size_t> pick(0, cand.size() - 1);
  for (int attempt = 0; attempt < 12; ++attempt) {
    llvm::APInt nv = llvm::APInt(64, cand[pick(rng)]).zextOrTrunc(bw);
    if (haveCur && nv == cur)
      continue;
    s.I->setOperand(s.op, llvm::ConstantInt::get(ty->getContext(), nv));
    return true;
  }
  return false;
}

} // namespace regatoni
