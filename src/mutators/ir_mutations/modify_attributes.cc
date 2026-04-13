// src/mutators/ir_mutations/modify_attributes.cc
#include "src/mutators/ir_mutations/modify_attributes.h"
#include "llvm/IR/Argument.h"
#include "llvm/IR/Attributes.h"
#include "llvm/IR/Function.h"
#include <vector>

namespace regatoni {

namespace {
struct Slot {
  llvm::Function *F;
  int argIdx; // -1 means return value
};
} // namespace

static bool isIntegerNonBool(llvm::Type *t) {
  return t->isIntegerTy() && !t->isIntegerTy(1);
}

static void collectSlots(llvm::Module &M, std::vector<Slot> &out) {
  for (auto &F : M) {
    if (F.isDeclaration())
      continue;
    if (isIntegerNonBool(F.getReturnType()))
      out.push_back({&F, -1});
    for (auto &arg : F.args())
      if (isIntegerNonBool(arg.getType()))
        out.push_back({&F, (int)arg.getArgNo()});
  }
}

bool ModifyAttributes::canApply(const llvm::Module &M) const {
  for (const auto &F : M) {
    if (F.isDeclaration())
      continue;
    if (isIntegerNonBool(F.getReturnType()))
      return true;
    for (const auto &arg : F.args())
      if (isIntegerNonBool(arg.getType()))
        return true;
  }
  return false;
}

bool ModifyAttributes::apply(llvm::Module &M, std::mt19937 &rng) {
  std::vector<Slot> slots;
  collectSlots(M, slots);
  if (slots.empty())
    return false;

  std::uniform_int_distribution<size_t> pick(0, slots.size() - 1);
  auto slot = slots[pick(rng)];
  auto *F = slot.F;

  using AK = llvm::Attribute::AttrKind;

  auto pickDifferent = [&](bool curS, bool curZ) -> int {
    // Current state encoded as 0=none, 1=sext, 2=zext.
    int cur = curS ? 1 : (curZ ? 2 : 0);
    int options[2];
    int n = 0;
    for (int c : {0, 1, 2})
      if (c != cur)
        options[n++] = c;
    std::uniform_int_distribution<int> pickC(0, n - 1);
    return options[pickC(rng)];
  };

  if (slot.argIdx < 0) {
    bool hadS = F->hasRetAttribute(AK::SExt);
    bool hadZ = F->hasRetAttribute(AK::ZExt);
    int choice = pickDifferent(hadS, hadZ);
    if (hadS) F->removeRetAttr(AK::SExt);
    if (hadZ) F->removeRetAttr(AK::ZExt);
    if (choice == 1) F->addRetAttr(AK::SExt);
    else if (choice == 2) F->addRetAttr(AK::ZExt);
    return true;
  } else {
    unsigned i = (unsigned)slot.argIdx;
    bool hadS = F->hasParamAttribute(i, AK::SExt);
    bool hadZ = F->hasParamAttribute(i, AK::ZExt);
    int choice = pickDifferent(hadS, hadZ);
    if (hadS) F->removeParamAttr(i, AK::SExt);
    if (hadZ) F->removeParamAttr(i, AK::ZExt);
    if (choice == 1) F->addParamAttr(i, AK::SExt);
    else if (choice == 2) F->addParamAttr(i, AK::ZExt);
    return true;
  }
}

} // namespace regatoni
