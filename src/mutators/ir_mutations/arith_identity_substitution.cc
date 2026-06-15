// src/mutators/ir_mutations/arith_identity_substitution.cc
#include "src/mutators/ir_mutations/arith_identity_substitution.h"
#include "llvm/ADT/APInt.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Instructions.h"
#include <vector>

namespace regatoni {

namespace {
enum Kind {
  MUL_POW2_TO_SHL, // mul x, 2^k -> shl x, k
  SHL_TO_MUL,      // shl x, k   -> mul x, 2^k
  ADD_SELF_TO_SHL, // add x, x   -> shl x, 1
  MULNEG1_TO_SUB,  // mul x, -1  -> sub 0, x
  SUB0_TO_MULNEG1, // sub 0, x   -> mul x, -1
};
struct Cand {
  llvm::BinaryOperator *I;
  Kind kind;
};
} // namespace

// Classify whether `BO` matches one of our rewrite patterns; if so set `out`.
static bool classify(llvm::BinaryOperator *BO, Kind &out) {
  auto op = BO->getOpcode();
  auto *ty = BO->getType();
  if (!ty->isIntegerTy())
    return false;
  unsigned bw = ty->getIntegerBitWidth();
  llvm::Value *L = BO->getOperand(0), *R = BO->getOperand(1);
  auto *RC = llvm::dyn_cast<llvm::ConstantInt>(R);

  switch (op) {
  case llvm::Instruction::Mul:
    if (RC && RC->getValue().isNegative() && RC->getValue().isAllOnes()) {
      out = MULNEG1_TO_SUB; // mul x, -1
      return true;
    }
    if (RC && RC->getValue().ugt(1) && RC->getValue().isPowerOf2()) {
      out = MUL_POW2_TO_SHL; // mul x, 2^k (k>=1)
      return true;
    }
    return false;
  case llvm::Instruction::Shl:
    // k must be a constant strictly < bw so 2^k is representable / in-range.
    if (RC && RC->getValue().ult(bw) && !RC->isZero()) {
      out = SHL_TO_MUL;
      return true;
    }
    return false;
  case llvm::Instruction::Add:
    if (L == R) {
      out = ADD_SELF_TO_SHL; // add x, x
      return true;
    }
    return false;
  case llvm::Instruction::Sub:
    if (auto *LC = llvm::dyn_cast<llvm::ConstantInt>(L))
      if (LC->isZero()) {
        out = SUB0_TO_MULNEG1; // sub 0, x
        return true;
      }
    return false;
  default:
    return false;
  }
}

static void collect(llvm::Module &M, std::vector<Cand> &out) {
  for (auto &F : M)
    for (auto &BB : F)
      for (auto &I : BB)
        if (auto *BO = llvm::dyn_cast<llvm::BinaryOperator>(&I)) {
          Kind k;
          if (classify(BO, k))
            out.push_back({BO, k});
        }
}

bool ArithIdentitySubstitution::canApply(const llvm::Module &M) const {
  for (const auto &F : M)
    for (const auto &BB : F)
      for (const auto &I : BB)
        if (auto *BO =
                llvm::dyn_cast<llvm::BinaryOperator>(const_cast<llvm::Instruction *>(&I))) {
          Kind k;
          if (classify(BO, k))
            return true;
        }
  return false;
}

bool ArithIdentitySubstitution::apply(llvm::Module &M, std::mt19937 &rng) {
  std::vector<Cand> cands;
  collect(M, cands);
  if (cands.empty())
    return false;

  std::uniform_int_distribution<size_t> pick(0, cands.size() - 1);
  Cand c = cands[pick(rng)];
  auto *BO = c.I;
  auto *ty = llvm::cast<llvm::IntegerType>(BO->getType());
  unsigned bw = ty->getBitWidth();
  llvm::IRBuilder<> B(BO);
  llvm::Value *L = BO->getOperand(0), *R = BO->getOperand(1);
  llvm::Value *nv = nullptr;

  switch (c.kind) {
  case MUL_POW2_TO_SHL: {
    unsigned k = llvm::cast<llvm::ConstantInt>(R)->getValue().logBase2();
    nv = B.CreateShl(L, llvm::ConstantInt::get(ty, k));
    break;
  }
  case SHL_TO_MUL: {
    uint64_t k = llvm::cast<llvm::ConstantInt>(R)->getValue().getZExtValue();
    nv = B.CreateMul(L, llvm::ConstantInt::get(ty->getContext(),
                                               llvm::APInt::getOneBitSet(bw, k)));
    break;
  }
  case ADD_SELF_TO_SHL:
    nv = B.CreateShl(L, llvm::ConstantInt::get(ty, 1));
    break;
  case MULNEG1_TO_SUB:
    nv = B.CreateSub(llvm::ConstantInt::get(ty, 0), L);
    break;
  case SUB0_TO_MULNEG1:
    nv = B.CreateMul(R, llvm::ConstantInt::getSigned(ty, -1));
    break;
  }
  if (!nv)
    return false;
  BO->replaceAllUsesWith(nv);
  BO->eraseFromParent();
  return true;
}

} // namespace regatoni
