// ir_feature_tuples.h — single source of truth for IR "context tuples".
//
// A context tuple is, per instruction:
//   opcode|intrinsic [flags] result-type-class <sorted operand def-kinds>
// the data-flow analog of edge-coverage. Used in two places that MUST agree:
//   - tools/ir_features.cc        (offline rarity/distillation analysis)
//   - src/harness/codegen_fuzz_target.cc (online Centipede steering features)
// Header-only so both link it without drift.

#ifndef REGATONI_IR_FEATURE_TUPLES_H
#define REGATONI_IR_FEATURE_TUPLES_H

#include "llvm/IR/Constants.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/InstrTypes.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Intrinsics.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Operator.h"
#include "llvm/ADT/StringRef.h"

#include <cstdint>
#include <set>
#include <string>

namespace regatoni {

inline std::string ftTypeClass(llvm::Type *T) {
  if (auto *IT = llvm::dyn_cast<llvm::IntegerType>(T)) {
    unsigned w = IT->getBitWidth();
    switch (w) {
    case 1: case 8: case 16: case 32: case 64: case 128:
      return "i" + std::to_string(w);
    default: return "iODD";
    }
  }
  if (T->isPointerTy()) return "ptr";
  if (T->isFloatingPointTy()) return "fp";
  if (T->isVectorTy()) return "vec";
  if (T->isVoidTy()) return "void";
  return "other";
}

inline std::string ftOperandKind(llvm::Value *V) {
  if (auto *CI = llvm::dyn_cast<llvm::ConstantInt>(V)) {
    if (CI->isMinusOne()) return "cNeg1";
    if (CI->isZero()) return "c0";
    if (CI->isOne()) return "c1";
    return "cInt";
  }
  if (llvm::isa<llvm::PoisonValue>(V)) return "cPoison";
  if (llvm::isa<llvm::UndefValue>(V)) return "cUndef";
  if (llvm::isa<llvm::ConstantPointerNull>(V)) return "cNull";
  if (llvm::isa<llvm::Argument>(V)) return "arg";
  if (llvm::isa<llvm::GlobalValue>(V)) return "global";
  if (auto *I = llvm::dyn_cast<llvm::Instruction>(V)) return I->getOpcodeName();
  if (llvm::isa<llvm::Constant>(V)) return "const";
  return "val";
}

inline std::string ftOpcode(llvm::Instruction &I) {
  if (auto *II = llvm::dyn_cast<llvm::IntrinsicInst>(&I))
    return ("call:" + llvm::Intrinsic::getBaseName(II->getIntrinsicID())).str();
  if (auto *CB = llvm::dyn_cast<llvm::CallBase>(&I)) {
    if (llvm::Function *F = CB->getCalledFunction())
      return ("call:" + F->getName()).str();
    return "call";
  }
  return I.getOpcodeName();
}

inline void ftCollectFlags(llvm::Instruction &I, std::set<std::string> &fl) {
  if (auto *OB = llvm::dyn_cast<llvm::OverflowingBinaryOperator>(&I)) {
    if (OB->hasNoUnsignedWrap()) fl.insert("nuw");
    if (OB->hasNoSignedWrap()) fl.insert("nsw");
  }
  if (auto *PE = llvm::dyn_cast<llvm::PossiblyExactOperator>(&I))
    if (PE->isExact()) fl.insert("exact");
  if (auto *PD = llvm::dyn_cast<llvm::PossiblyDisjointInst>(&I))
    if (PD->isDisjoint()) fl.insert("disjoint");
  if (auto *NN = llvm::dyn_cast<llvm::PossiblyNonNegInst>(&I))
    if (NN->hasNonNeg()) fl.insert("nneg");
  if (auto *GEP = llvm::dyn_cast<llvm::GEPOperator>(&I))
    if (GEP->isInBounds()) fl.insert("inbounds");
  if (auto *IC = llvm::dyn_cast<llvm::ICmpInst>(&I))
    if (IC->hasSameSign()) fl.insert("samesign");
  if (llvm::isa<llvm::FPMathOperator>(&I) && I.getFastMathFlags().any())
    fl.insert("fmf");
}

inline std::string ftTuple(llvm::Instruction &I) {
  llvm::Type *T = I.getType();
  if (T->isVoidTy() && I.getNumOperands() > 0 &&
      !llvm::isa<llvm::BasicBlock>(I.getOperand(0)))
    T = I.getOperand(0)->getType();

  std::set<std::string> flags, kinds;
  ftCollectFlags(I, flags);
  if (auto *CB = llvm::dyn_cast<llvm::CallBase>(&I)) {
    for (llvm::Value *A : CB->args()) kinds.insert(ftOperandKind(A));
  } else {
    for (llvm::Value *Op : I.operands()) {
      if (llvm::isa<llvm::BasicBlock>(Op)) continue;
      kinds.insert(ftOperandKind(Op));
    }
  }
  std::string s = ftOpcode(I) + "[";
  bool first = true;
  for (auto &f : flags) { s += (first ? "" : "+") + f; first = false; }
  if (first) s += "-";
  s += "] " + ftTypeClass(T) + " <";
  first = true;
  for (auto &k : kinds) { s += (first ? "" : ",") + k; first = false; }
  s += ">";
  return s;
}

// Visit every (deduped) context tuple in a function.
template <class Fn> inline void ftForEachTuple(llvm::Function &F, Fn cb) {
  std::set<std::string> seen;
  for (llvm::BasicBlock &BB : F)
    for (llvm::Instruction &I : BB) {
      std::string t = ftTuple(I);
      if (seen.insert(t).second) cb(t);
    }
}

inline uint32_t ftHash32(const std::string &s) {
  uint64_t h = 1469598103934665603ULL;     // FNV-1a 64
  for (unsigned char c : s) { h ^= c; h *= 1099511628211ULL; }
  uint32_t f = static_cast<uint32_t>(h ^ (h >> 32));
  return f ? f : 1;                          // never 0 (Centipede ignores 0)
}

// Emit packed Centipede user features (domain<<32 | hash32) for each distinct
// context tuple across the module. Returns count written (<= cap). Dedups.
inline size_t ftEmitFeatureHashes(const llvm::Module &M, uint64_t *out,
                                  size_t cap, uint32_t domain = 0) {
  std::set<uint32_t> seen;
  size_t n = 0;
  for (const llvm::Function &CF : M) {
    if (CF.isDeclaration()) continue;
    llvm::Function &F = const_cast<llvm::Function &>(CF);
    ftForEachTuple(F, [&](const std::string &t) {
      if (n >= cap) return;
      uint32_t f = ftHash32(t);
      if (seen.insert(f).second)
        out[n++] = (static_cast<uint64_t>(domain) << 32) | f;
    });
    if (n >= cap) break;
  }
  return n;
}

} // namespace regatoni

#endif // REGATONI_IR_FEATURE_TUPLES_H
