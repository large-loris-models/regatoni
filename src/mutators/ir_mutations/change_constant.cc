// src/mutators/ir_mutations/change_constant.cc
#include "src/mutators/ir_mutations/change_constant.h"
#include "llvm/ADT/APInt.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Instructions.h"
#include <vector>

namespace regatoni {

// We only retarget constants on these instruction kinds. Binop / icmp / select
// immediates are the bread-and-butter isel constants and carry no "immarg" /
// case-uniqueness constraints (unlike switch or some intrinsic args), so a
// ConstantInt -> ConstantInt swap of the same type is always structurally valid.
static bool isConstHolder(const llvm::Instruction &I) {
  return llvm::isa<llvm::BinaryOperator>(I) || llvm::isa<llvm::ICmpInst>(I) ||
         llvm::isa<llvm::SelectInst>(I);
}

// A ConstantInt operand we may rewrite, identified by its instruction + index.
namespace {
struct Slot {
  llvm::Instruction *I;
  unsigned op;
};
} // namespace

// Operand 1 of a shl/lshr/ashr is the shift amount — owned by
// mutate_shift_amount (which keeps it < bitwidth). Skip it here so we never
// turn a shift amount into a huge constant (>= bitwidth -> poison source ->
// "source more defined than target" oracle noise).
static bool isShiftAmountSlot(const llvm::Instruction &I, unsigned op) {
  if (op != 1)
    return false;
  if (auto *BO = llvm::dyn_cast<llvm::BinaryOperator>(&I)) {
    auto o = BO->getOpcode();
    return o == llvm::Instruction::Shl || o == llvm::Instruction::LShr ||
           o == llvm::Instruction::AShr;
  }
  return false;
}

static void collectSlots(llvm::Module &M, std::vector<Slot> &out) {
  for (auto &F : M)
    for (auto &BB : F)
      for (auto &I : BB) {
        if (!isConstHolder(I))
          continue;
        for (unsigned i = 0, e = I.getNumOperands(); i < e; ++i)
          if (llvm::isa<llvm::ConstantInt>(I.getOperand(i)) &&
              !isShiftAmountSlot(I, i))
            out.push_back({&I, i});
      }
}

// Curated "interesting" constants of width `bw`. uint64 magics are truncated to
// bw by the APInt(bw, val) ctor, which is exactly what we want (e.g. the orc.b
// SWAR magic 0x0101.. naturally narrows to 0x01 at i8).
static void interestingConstants(unsigned bw, std::mt19937 &rng,
                                 std::vector<llvm::APInt> &out) {
  using llvm::APInt;
  out.push_back(APInt(bw, 0));
  out.push_back(APInt(bw, 1));
  out.push_back(APInt::getAllOnes(bw));        // -1 / umax
  out.push_back(APInt::getSignedMinValue(bw)); // signbit 0x80..0
  out.push_back(APInt::getSignedMaxValue(bw)); // 0x7f..f
  out.push_back(APInt(bw, bw));                // the bit-width itself

  if (bw >= 2) {
    std::uniform_int_distribution<unsigned> pk(0, bw - 1);
    unsigned k = pk(rng);
    APInt p2 = APInt::getOneBitSet(bw, k); // 2^k
    out.push_back(p2);
    out.push_back(p2 - APInt(bw, 1)); // 2^k - 1  (low mask)
    out.push_back(p2 + APInt(bw, 1)); // 2^k + 1
  }

  // Byte masks + SWAR / bitmanip magics (orc.b 0x0101.., rev8/popcount helpers).
  // Build at 64 bits then narrow/widen to bw — APInt(bw,val) asserts if val
  // doesn't fit in bw bits, which these wide magics don't for i8/i16/i32.
  for (uint64_t m :
       {0xFFull, 0xFF00ull, 0xFFFFull, 0xFFFF0000ull, 0xFFFFFFFFull,
        0x0101010101010101ull, 0x8080808080808080ull, 0x0F0F0F0F0F0F0F0Full,
        0x10101ull /*65793*/, 0xAAAAAAAAAAAAAAAAull, 0x5555555555555555ull})
    out.push_back(APInt(64, m).zextOrTrunc(bw));
}

// A structured perturbation of `cur` (same width).
static llvm::APInt perturb(const llvm::APInt &cur, std::mt19937 &rng) {
  unsigned bw = cur.getBitWidth();
  llvm::APInt one(bw, 1);
  std::uniform_int_distribution<unsigned> pick(0, 4);
  switch (pick(rng)) {
  case 0:
    return cur + one;
  case 1:
    return cur - one;
  case 2:
    return -cur; // negate
  case 3: {
    llvm::APInt c = cur;
    std::uniform_int_distribution<unsigned> pb(0, bw - 1);
    c.flipBit(pb(rng));
    return c;
  }
  default:
    return cur.rotl(8 % bw); // byte rotate (no-op only when bw | 8)
  }
}

bool ChangeConstant::canApply(const llvm::Module &M) const {
  for (const auto &F : M)
    for (const auto &BB : F)
      for (const auto &I : BB) {
        if (!isConstHolder(I))
          continue;
        for (unsigned i = 0, e = I.getNumOperands(); i < e; ++i)
          if (llvm::isa<llvm::ConstantInt>(I.getOperand(i)) &&
              !isShiftAmountSlot(I, i))
            return true;
      }
  return false;
}

bool ChangeConstant::apply(llvm::Module &M, std::mt19937 &rng) {
  std::vector<Slot> slots;
  collectSlots(M, slots);
  if (slots.empty())
    return false;

  std::uniform_int_distribution<size_t> pickSlot(0, slots.size() - 1);
  Slot s = slots[pickSlot(rng)];
  auto *CI = llvm::cast<llvm::ConstantInt>(s.I->getOperand(s.op));
  llvm::APInt cur = CI->getValue();
  unsigned bw = cur.getBitWidth();

  // 60% curated pool, 40% perturbation. Retry a few times to land a value that
  // actually differs from the current one (small types collapse the pool).
  std::bernoulli_distribution usePool(0.6);
  for (int attempt = 0; attempt < 8; ++attempt) {
    llvm::APInt nv(bw, 0);
    if (usePool(rng)) {
      std::vector<llvm::APInt> pool;
      interestingConstants(bw, rng, pool);
      std::uniform_int_distribution<size_t> pickV(0, pool.size() - 1);
      nv = pool[pickV(rng)];
    } else {
      nv = perturb(cur, rng);
    }
    if (nv != cur) {
      s.I->setOperand(s.op,
                      llvm::ConstantInt::get(CI->getType()->getContext(),
                                             nv));
      return true;
    }
  }
  return false;
}

} // namespace regatoni
