// src/mutators/ir_mutations/wrap_bitmanip.cc
#include "src/mutators/ir_mutations/wrap_bitmanip.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Intrinsics.h"
#include <vector>

namespace regatoni {

// A wrappable value: a non-PHI, non-terminator integer instruction of a width
// the RISC-V bitmanip patterns care about, that actually has a use to rewire.
static bool isWrappable(const llvm::Instruction &I) {
  if (llvm::isa<llvm::PHINode>(I) || I.isTerminator())
    return false;
  auto *ty = llvm::dyn_cast<llvm::IntegerType>(I.getType());
  if (!ty)
    return false;
  unsigned bw = ty->getBitWidth();
  if (!(bw == 8 || bw == 16 || bw == 32 || bw == 64))
    return false;
  return !I.use_empty();
}

bool WrapBitmanip::canApply(const llvm::Module &M) const {
  for (const auto &F : M)
    for (const auto &BB : F)
      for (const auto &I : BB)
        if (isWrappable(I))
          return true;
  return false;
}

bool WrapBitmanip::apply(llvm::Module &M, std::mt19937 &rng) {
  std::vector<llvm::Instruction *> targets;
  for (auto &F : M)
    for (auto &BB : F)
      for (auto &I : BB)
        if (isWrappable(I))
          targets.push_back(&I);
  if (targets.empty())
    return false;

  std::uniform_int_distribution<size_t> pickT(0, targets.size() - 1);
  llvm::Instruction *V = targets[pickT(rng)];
  auto *ty = llvm::cast<llvm::IntegerType>(V->getType());
  unsigned bw = ty->getBitWidth();

  // Capture V's existing uses BEFORE we build the idiom, so the idiom's own
  // references to V (and any intermediate instrs) are not rewired into a cycle.
  std::vector<llvm::Use *> uses;
  for (llvm::Use &U : V->uses())
    uses.push_back(&U);

  // Insert right after V (V is non-PHI, so getNextNode() is in the body).
  llvm::IRBuilder<> B(V->getNextNode());

  enum Kind { BITREV, CTPOP, CTLZ, CTTZ, ROTL, LOWBIT, CLRLOW, XORSHR, BSWAP };
  std::vector<Kind> kinds = {BITREV, CTPOP, CTLZ, CTTZ, ROTL, LOWBIT, CLRLOW, XORSHR};
  if (bw % 16 == 0)
    kinds.push_back(BSWAP); // bswap is only defined for multiple-of-16 widths
  std::uniform_int_distribution<size_t> pickK(0, kinds.size() - 1);

  std::uniform_int_distribution<unsigned> pickAmt(1, bw - 1);
  llvm::Value *wrapped = nullptr;
  switch (kinds[pickK(rng)]) {
  case BITREV:
    wrapped = B.CreateUnaryIntrinsic(llvm::Intrinsic::bitreverse, V);
    break;
  case BSWAP:
    wrapped = B.CreateUnaryIntrinsic(llvm::Intrinsic::bswap, V);
    break;
  case CTPOP:
    wrapped = B.CreateUnaryIntrinsic(llvm::Intrinsic::ctpop, V);
    break;
  case CTLZ:
    wrapped = B.CreateIntrinsic(llvm::Intrinsic::ctlz, {ty}, {V, B.getFalse()});
    break;
  case CTTZ:
    wrapped = B.CreateIntrinsic(llvm::Intrinsic::cttz, {ty}, {V, B.getFalse()});
    break;
  case ROTL: {
    llvm::Value *amt = llvm::ConstantInt::get(ty, pickAmt(rng));
    wrapped = B.CreateIntrinsic(llvm::Intrinsic::fshl, {ty}, {V, V, amt});
    break;
  }
  case LOWBIT: // x & -x  (isolate lowest set bit)
    wrapped = B.CreateAnd(V, B.CreateNeg(V));
    break;
  case CLRLOW: // x & (x-1) (clear lowest set bit)
    wrapped = B.CreateAnd(V, B.CreateSub(V, llvm::ConstantInt::get(ty, 1)));
    break;
  case XORSHR: { // (x >> k) ^ x
    llvm::Value *sh = B.CreateLShr(V, llvm::ConstantInt::get(ty, pickAmt(rng)));
    wrapped = B.CreateXor(sh, V);
    break;
  }
  }

  if (!wrapped)
    return false;

  // Rewire V's original uses to the wrapped value. The idiom keeps using V.
  for (llvm::Use *U : uses)
    U->set(wrapped);
  return true;
}

} // namespace regatoni
