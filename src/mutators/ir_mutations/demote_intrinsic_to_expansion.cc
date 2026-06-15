// src/mutators/ir_mutations/demote_intrinsic_to_expansion.cc
#include "src/mutators/ir_mutations/demote_intrinsic_to_expansion.h"
#include "llvm/ADT/APInt.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/Intrinsics.h"
#include <vector>

namespace regatoni {

// ctpop: SWAR popcount expansion. Requires bw a multiple of 8 (8/16/32/64).
static bool ctpopOK(unsigned bw) {
  return bw == 8 || bw == 16 || bw == 32 || bw == 64;
}
// bswap: byte-reverse. Requires bw a multiple of 16 (the intrinsic's own rule).
static bool bswapOK(unsigned bw) { return bw == 16 || bw == 32 || bw == 64; }

static llvm::IntrinsicInst *matchTarget(llvm::Instruction &I) {
  auto *II = llvm::dyn_cast<llvm::IntrinsicInst>(&I);
  if (!II)
    return nullptr;
  auto id = II->getIntrinsicID();
  auto *ty = llvm::dyn_cast<llvm::IntegerType>(II->getType());
  if (!ty)
    return nullptr;
  unsigned bw = ty->getBitWidth();
  if (id == llvm::Intrinsic::ctpop && ctpopOK(bw))
    return II;
  if (id == llvm::Intrinsic::bswap && bswapOK(bw))
    return II;
  return nullptr;
}

// SWAR popcount of `v` (width bw, multiple of 8).
static llvm::Value *swarCtpop(llvm::IRBuilder<> &B, llvm::Value *v,
                              llvm::IntegerType *ty) {
  unsigned bw = ty->getBitWidth();
  auto C = [&](uint64_t pat) {
    return llvm::ConstantInt::get(ty->getContext(),
                                  llvm::APInt(64, pat).zextOrTrunc(bw));
  };
  llvm::Value *m1 = C(0x5555555555555555ull), *m2 = C(0x3333333333333333ull),
              *m4 = C(0x0F0F0F0F0F0F0F0Full), *h01 = C(0x0101010101010101ull);
  // v = v - ((v >> 1) & m1)
  v = B.CreateSub(v, B.CreateAnd(B.CreateLShr(v, llvm::ConstantInt::get(ty, 1)), m1));
  // v = (v & m2) + ((v >> 2) & m2)
  v = B.CreateAdd(B.CreateAnd(v, m2),
                  B.CreateAnd(B.CreateLShr(v, llvm::ConstantInt::get(ty, 2)), m2));
  // v = (v + (v >> 4)) & m4
  v = B.CreateAnd(B.CreateAdd(v, B.CreateLShr(v, llvm::ConstantInt::get(ty, 4))), m4);
  // result = (v * h01) >> (bw - 8)
  return B.CreateLShr(B.CreateMul(v, h01), llvm::ConstantInt::get(ty, bw - 8));
}

// Byte-reverse of `v` (width bw, multiple of 8) via shift/mask/or.
static llvm::Value *byteReverse(llvm::IRBuilder<> &B, llvm::Value *v,
                                llvm::IntegerType *ty) {
  unsigned bw = ty->getBitWidth();
  unsigned nbytes = bw / 8;
  auto ff = llvm::ConstantInt::get(ty->getContext(),
                                   llvm::APInt(bw, 0xFF));
  llvm::Value *res = llvm::ConstantInt::get(ty, 0);
  for (unsigned i = 0; i < nbytes; ++i) {
    llvm::Value *byte =
        B.CreateAnd(B.CreateLShr(v, llvm::ConstantInt::get(ty, 8 * i)), ff);
    llvm::Value *placed =
        B.CreateShl(byte, llvm::ConstantInt::get(ty, 8 * (nbytes - 1 - i)));
    res = B.CreateOr(res, placed);
  }
  return res;
}

bool DemoteIntrinsicToExpansion::canApply(const llvm::Module &M) const {
  for (const auto &F : M)
    for (const auto &BB : F)
      for (auto &I : BB)
        if (matchTarget(const_cast<llvm::Instruction &>(I)))
          return true;
  return false;
}

bool DemoteIntrinsicToExpansion::apply(llvm::Module &M, std::mt19937 &rng) {
  std::vector<llvm::IntrinsicInst *> targets;
  for (auto &F : M)
    for (auto &BB : F)
      for (auto &I : BB)
        if (auto *II = matchTarget(I))
          targets.push_back(II);
  if (targets.empty())
    return false;

  std::uniform_int_distribution<size_t> pick(0, targets.size() - 1);
  auto *II = targets[pick(rng)];
  auto *ty = llvm::cast<llvm::IntegerType>(II->getType());
  llvm::Value *arg = II->getArgOperand(0);

  llvm::IRBuilder<> B(II);
  llvm::Value *exp = (II->getIntrinsicID() == llvm::Intrinsic::ctpop)
                         ? swarCtpop(B, arg, ty)
                         : byteReverse(B, arg, ty);
  II->replaceAllUsesWith(exp);
  II->eraseFromParent();
  return true;
}

} // namespace regatoni
