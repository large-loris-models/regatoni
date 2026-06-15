// tests/test_mutations.cc — Test that mutations work correctly
//
// For each mutation, verify:
//   1. canApply returns true on suitable IR
//   2. apply actually changes something
//   3. The resulting module is still valid LLVM IR
//   4. canApply returns false on IR that has no suitable targets

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
#include "src/mutators/ir_mutations/resize_type.h"
#include "src/mutators/ir_mutations/eliminate_undef.h"
#include "src/mutators/ir_mutations/change_constant.h"
#include "src/mutators/ir_mutations/mutate_shift_amount.h"
#include "src/mutators/ir_mutations/wrap_bitmanip.h"
#include "src/mutators/ir_mutations/arith_identity_substitution.h"
#include "src/mutators/ir_mutations/narrow_then_widen.h"
#include "src/mutators/ir_mutations/demote_intrinsic_to_expansion.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Operator.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Verifier.h"
#include "llvm/IRReader/IRReader.h"
#include "llvm/Support/SourceMgr.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/raw_ostream.h"

#include <cassert>
#include <iostream>
#include <random>
#include <string>

using namespace llvm;
using namespace regatoni;

// Helper: parse IR string into a Module
static std::unique_ptr<Module> parseIR(LLVMContext &Ctx, const char *IR) {
  SMDiagnostic Err;
  auto Buf = MemoryBuffer::getMemBuffer(IR, "test", false);
  auto M = llvm::parseIR(*Buf, Err, Ctx);
  if (!M) {
    Err.print("test", errs());
    assert(false && "Failed to parse test IR");
  }
  return M;
}

// Helper: serialize module to string for comparison
static std::string moduleToString(const Module &M) {
  std::string s;
  raw_string_ostream os(s);
  M.print(os, nullptr);
  return s;
}

// Helper: verify module is valid
static bool isValid(const Module &M) {
  std::string err;
  raw_string_ostream os(err);
  return !verifyModule(M, &os);
}

// Rich IR used by composability and chain-stress tests — hits many mutations.
static const char *kRichIR = R"(
  declare void @sink(i32)
  define i32 @callee(i32 %a, i32 %b) {
    %x = add i32 %a, %b
    ret i32 %x
  }
  define i32 @main_fn(i32 %a, i32 %b, float %f, ptr %p) {
  entry:
    %s = add i32 %a, %b
    %c = icmp slt i32 %s, %a
    br i1 %c, label %then, label %else
  then:
    %r1 = call i32 @callee(i32 %a, i32 %b)
    call void @sink(i32 %r1)
    %g = getelementptr i32, ptr %p, i64 2
    %u = add i32 %r1, undef
    br label %join
  else:
    %r2 = fadd float %f, %f
    %r3 = fptosi float %r2 to i32
    br label %join
  join:
    %phi = phi i32 [ %u, %then ], [ %r3, %else ]
    ret i32 %phi
  }
)";

// ============================================================================
// Test: SwapBinOp
// ============================================================================

static void testSwapBinOp() {
  LLVMContext Ctx;

  // IR with binary operators
  auto M = parseIR(Ctx, R"(
    define i32 @f(i32 %a, i32 %b) {
      %x = add i32 %a, %b
      %y = mul i32 %x, %b
      ret i32 %y
    }
  )");

  SwapBinOp mut;

  // Should be applicable
  assert(mut.canApply(*M) && "SwapBinOp should apply to module with binops");

  // Apply and check something changed
  std::string before = moduleToString(*M);
  std::mt19937 rng(42);
  bool changed = mut.apply(*M, rng);
  std::string after = moduleToString(*M);

  assert(changed && "SwapBinOp should change something");
  assert(before != after && "Module should differ after mutation");
  assert(isValid(*M) && "Module should still be valid after mutation");

  std::cout << "  [PASS] SwapBinOp: mutates binops, result is valid IR\n";
}

// Regression: when we swap an opcode across classes (e.g. Add with nsw/nuw
// flags to a bitwise Xor, or to a UDiv that has an `exact` flag instead),
// the replacement must not carry flags that are meaningless or illegal for
// the new opcode. We create a fresh BinaryOperator rather than mutating in
// place, so stale flags cannot leak. The `randomizeFlags` pass only sets
// flags appropriate to the new opcode class.
static void testSwapBinOpNoStaleFlags() {
  const char *kIR = R"(
    define i32 @f(i32 %a, i32 %b) {
      %x = add nsw nuw i32 %a, %b
      %y = mul nsw nuw i32 %x, %a
      %z = shl nsw nuw i32 %y, %a
      %q = sdiv exact i32 %z, %b
      %r = lshr exact i32 %q, %b
      ret i32 %r
    }
  )";
  SwapBinOp mut;
  for (int seed = 0; seed < 200; ++seed) {
    LLVMContext Ctx;
    auto M = parseIR(Ctx, kIR);
    std::mt19937 rng(seed);
    mut.apply(*M, rng);
    if (!isValid(*M)) {
      std::cerr << "  [FAIL] SwapBinOp produced invalid IR (seed " << seed
                << "):\n"
                << moduleToString(*M);
      assert(false);
    }
    // For every binop in the module, the flags present must be legal for
    // its current opcode. We check the specific case that motivated the
    // fix: flags set on an opcode that doesn't support them.
    for (auto &F : *M) {
      for (auto &BB : F) {
        for (auto &I : BB) {
          auto *BO = llvm::dyn_cast<llvm::BinaryOperator>(&I);
          if (!BO)
            continue;
          auto op = BO->getOpcode();
          bool hasNUW =
              llvm::isa<llvm::OverflowingBinaryOperator>(BO) &&
              BO->hasNoUnsignedWrap();
          bool hasNSW =
              llvm::isa<llvm::OverflowingBinaryOperator>(BO) &&
              BO->hasNoSignedWrap();
          bool hasExact =
              llvm::isa<llvm::PossiblyExactOperator>(BO) && BO->isExact();
          bool nuwNswOk = op == llvm::Instruction::Add ||
                          op == llvm::Instruction::Sub ||
                          op == llvm::Instruction::Mul ||
                          op == llvm::Instruction::Shl;
          bool exactOk = op == llvm::Instruction::UDiv ||
                         op == llvm::Instruction::SDiv ||
                         op == llvm::Instruction::LShr ||
                         op == llvm::Instruction::AShr;
          assert((!(hasNUW || hasNSW) || nuwNswOk) &&
                 "NUW/NSW on opcode that doesn't support it");
          assert((!hasExact || exactOk) &&
                 "exact flag on opcode that doesn't support it");
        }
      }
    }
  }
  std::cout
      << "  [PASS] SwapBinOp: 200 seeds with nsw/nuw/exact inputs, no stale flags\n";
}

static void testSwapBinOpNoTargets() {
  LLVMContext Ctx;

  // IR with no binary operators
  auto M = parseIR(Ctx, R"(
    define void @f() {
      ret void
    }
  )");

  SwapBinOp mut;
  assert(!mut.canApply(*M) && "SwapBinOp should not apply to empty function");

  std::cout << "  [PASS] SwapBinOp: correctly rejects module with no binops\n";
}

// ============================================================================
// Test: SwapCmpPredicate
// ============================================================================

static void testSwapCmpPredicate() {
  LLVMContext Ctx;

  auto M = parseIR(Ctx, R"(
    define i1 @f(i32 %a, i32 %b, float %x, float %y) {
      %c1 = icmp slt i32 %a, %b
      %c2 = fcmp oeq float %x, %y
      %c3 = and i1 %c1, %c2
      ret i1 %c3
    }
  )");

  SwapCmpPredicate mut;
  assert(mut.canApply(*M) && "SwapCmpPredicate should apply to module with cmp");

  std::string before = moduleToString(*M);
  std::mt19937 rng(7);
  bool changed = mut.apply(*M, rng);
  std::string after = moduleToString(*M);

  assert(changed && "SwapCmpPredicate should change something");
  assert(before != after && "Module should differ after mutation");
  assert(isValid(*M) && "Module should still be valid after mutation");

  std::cout << "  [PASS] SwapCmpPredicate: mutates cmp, result is valid IR\n";
}

static void testSwapCmpPredicateNoTargets() {
  LLVMContext Ctx;

  auto M = parseIR(Ctx, R"(
    define i32 @f(i32 %a, i32 %b) {
      %x = add i32 %a, %b
      ret i32 %x
    }
  )");

  SwapCmpPredicate mut;
  assert(!mut.canApply(*M) &&
         "SwapCmpPredicate should not apply without cmp");

  std::cout
      << "  [PASS] SwapCmpPredicate: correctly rejects module with no cmp\n";
}

// ============================================================================
// Test: ModifyFlags
// ============================================================================

static void testModifyFlags() {
  LLVMContext Ctx;

  auto M = parseIR(Ctx, R"(
    define float @f(i32 %a, i32 %b, float %x, float %y) {
      %s = add i32 %a, %b
      %d = sdiv i32 %a, %b
      %f1 = fadd float %x, %y
      %f2 = sitofp i32 %s to float
      %f3 = fmul float %f1, %f2
      ret float %f3
    }
  )");

  ModifyFlags mut;
  assert(mut.canApply(*M) && "ModifyFlags should apply");

  bool anyChanged = false;
  for (int seed = 0; seed < 20; ++seed) {
    std::mt19937 rng(seed);
    if (mut.apply(*M, rng))
      anyChanged = true;
    assert(isValid(*M) && "Module should still be valid after mutation");
  }
  assert(anyChanged && "ModifyFlags should change something across seeds");

  std::cout << "  [PASS] ModifyFlags: mutates flags, result is valid IR\n";
}

static void testModifyFlagsNoTargets() {
  LLVMContext Ctx;

  auto M = parseIR(Ctx, R"(
    define void @f() {
      ret void
    }
  )");

  ModifyFlags mut;
  assert(!mut.canApply(*M) &&
         "ModifyFlags should not apply without eligible instructions");

  std::cout << "  [PASS] ModifyFlags: rejects module with no flaggable ops\n";
}

// ============================================================================
// Test: SwapOperands
// ============================================================================

static void testSwapOperands() {
  LLVMContext Ctx;

  auto M = parseIR(Ctx, R"(
    define i1 @f(i32 %a, i32 %b) {
      %s = sub i32 %a, %b
      %c = icmp slt i32 %s, %a
      ret i1 %c
    }
  )");

  SwapOperands mut;
  assert(mut.canApply(*M) && "SwapOperands should apply");

  std::string before = moduleToString(*M);
  std::mt19937 rng(3);
  bool changed = mut.apply(*M, rng);
  std::string after = moduleToString(*M);

  assert(changed && "SwapOperands should change something");
  assert(before != after && "Module should differ after mutation");
  assert(isValid(*M) && "Module should still be valid after mutation");

  std::cout << "  [PASS] SwapOperands: swaps operands, result is valid IR\n";
}

static void testSwapOperandsNoTargets() {
  LLVMContext Ctx;

  auto M = parseIR(Ctx, R"(
    define void @f() {
      ret void
    }
  )");

  SwapOperands mut;
  assert(!mut.canApply(*M) && "SwapOperands should not apply without targets");

  std::cout << "  [PASS] SwapOperands: rejects module with no targets\n";
}

// ============================================================================
// Test: ReplaceOperand
// ============================================================================

static void testReplaceOperand() {
  LLVMContext Ctx;

  auto M = parseIR(Ctx, R"(
    define i32 @f(i32 %a, i32 %b, i32 %c) {
      %x = add i32 %a, %b
      %y = mul i32 %x, %c
      ret i32 %y
    }
  )");

  ReplaceOperand mut;
  assert(mut.canApply(*M) && "ReplaceOperand should apply");

  std::string before = moduleToString(*M);
  std::mt19937 rng(11);
  bool changed = mut.apply(*M, rng);
  std::string after = moduleToString(*M);

  assert(changed && "ReplaceOperand should change something");
  assert(before != after && "Module should differ after mutation");
  assert(isValid(*M) && "Module should still be valid after mutation");

  std::cout << "  [PASS] ReplaceOperand: replaces operand, result is valid IR\n";
}

static void testReplaceOperandNoTargets() {
  LLVMContext Ctx;

  auto M = parseIR(Ctx, R"(
    define void @f() {
      ret void
    }
  )");

  ReplaceOperand mut;
  assert(!mut.canApply(*M) &&
         "ReplaceOperand should not apply without targets");

  std::cout << "  [PASS] ReplaceOperand: rejects module with no targets\n";
}

// Regression: dominance must be honored across basic blocks. The old
// implementation only collected candidates from the target's own BB, so
// cross-BB hazards couldn't appear. The new implementation widens the
// pool to every dominating value in the function, which is only safe if
// we actually filter by DominatorTree. If we accidentally admit a value
// from a non-dominating BB (a later-function-position BB, say), the
// result is a forward reference that fails verifyModule and hangs
// opt -O2 on the fuzzing harness.
static void testReplaceOperandDominanceMultiBB() {
  const char *kIR = R"(
    define i32 @f(i32 %a, i32 %b) {
    entry:
      %ea = add i32 %a, %b
      %ec = icmp slt i32 %ea, %a
      br i1 %ec, label %then, label %else
    then:
      %ta = mul i32 %ea, %a
      %tc = icmp eq i32 %ta, %ea
      br label %join
    else:
      %elA = sub i32 %a, %b
      %elC = icmp ne i32 %elA, %b
      br label %join
    join:
      %ja = phi i32 [ %ta, %then ], [ %elA, %else ]
      %jc = icmp sgt i32 %ja, %a
      %jr = add i32 %ja, %ea
      ret i32 %jr
    }
  )";

  ReplaceOperand mut;
  for (int seed = 0; seed < 200; ++seed) {
    LLVMContext Ctx;
    auto M = parseIR(Ctx, kIR);
    std::mt19937 rng(seed);
    mut.apply(*M, rng);
    if (!isValid(*M)) {
      std::cerr << "  [FAIL] ReplaceOperand produced invalid IR (seed "
                << seed << "):\n"
                << moduleToString(*M);
      assert(false && "ReplaceOperand created invalid IR across BBs");
    }
  }

  std::cout
      << "  [PASS] ReplaceOperand: 200 seeds on multi-BB IR, all valid\n";
}

// Stress: repeated ReplaceOperand on multi-BB IR must not create forward
// references. verifyModule catches use-before-def.
static void testReplaceOperandChainStress() {
  const char *kIR = R"(
    define i32 @g(i32 %a, i32 %b, i32 %c) {
    e:
      %ea = add i32 %a, %b
      %eb = add i32 %b, %c
      %ec = icmp slt i32 %ea, %eb
      br i1 %ec, label %L, label %R
    L:
      %la = mul i32 %ea, %a
      %lb = mul i32 %eb, %b
      %lc = icmp ne i32 %la, %lb
      br label %J
    R:
      %ra = sub i32 %ea, %eb
      %rb = xor i32 %ea, %eb
      %rc = icmp eq i32 %ra, %rb
      br label %J
    J:
      %ja = phi i32 [ %la, %L ], [ %ra, %R ]
      %jb = phi i32 [ %lb, %L ], [ %rb, %R ]
      %jr = add i32 %ja, %jb
      ret i32 %jr
    }
  )";
  ReplaceOperand mut;
  for (int trial = 0; trial < 20; ++trial) {
    LLVMContext Ctx;
    auto M = parseIR(Ctx, kIR);
    std::mt19937 rng(trial);
    for (int step = 0; step < 25; ++step) {
      mut.apply(*M, rng);
      assert(isValid(*M) &&
             "ReplaceOperand chain should never produce invalid IR");
    }
  }
  std::cout
      << "  [PASS] ReplaceOperand: 20 trials x 25 chained ops, all valid\n";
}

// The new implementation must accept candidates from *dominating* BBs, not
// just the target's own BB. In the IR below, no instruction in `then`
// originally references anything defined in `entry`. If the old
// (same-BB-only) logic were still in effect, the mutation could never
// introduce an `entry`-defined operand in a `then` instruction. The new
// DominatorTree-based selection must sometimes do so.
static void testReplaceOperandUsesDominatingBB() {
  const char *kIR = R"(
    define i32 @f(i32 %a, i32 %b) {
    entry:
      %ea = add i32 %a, %b
      br label %then
    then:
      %ta = mul i32 %a, %b
      %tb = add i32 %ta, %ta
      ret i32 %tb
    }
  )";

  // Confirm the starting IR has no `then` operand defined in `entry`.
  {
    LLVMContext Ctx;
    auto M0 = parseIR(Ctx, kIR);
    auto *F0 = M0->getFunction("f");
    for (auto &BB : *F0) {
      if (BB.getName() != "then")
        continue;
      for (auto &I : BB) {
        for (unsigned i = 0, e = I.getNumOperands(); i < e; ++i) {
          auto *op = llvm::dyn_cast<llvm::Instruction>(I.getOperand(i));
          assert(!(op && op->getParent() != &BB) &&
                 "baseline IR must have no cross-BB operands in `then`");
        }
      }
    }
  }

  ReplaceOperand mut;
  bool sawCrossBB = false;
  for (int seed = 0; seed < 400 && !sawCrossBB; ++seed) {
    LLVMContext Ctx;
    auto M = parseIR(Ctx, kIR);
    std::mt19937 rng(seed);
    mut.apply(*M, rng);
    assert(isValid(*M));
    auto *F = M->getFunction("f");
    for (auto &BB : *F) {
      if (BB.getName() != "then")
        continue;
      for (auto &I : BB) {
        for (unsigned i = 0, e = I.getNumOperands(); i < e; ++i) {
          auto *op = llvm::dyn_cast<llvm::Instruction>(I.getOperand(i));
          if (op && op->getParent() != &BB &&
              op->getParent()->getName() == "entry") {
            sawCrossBB = true;
          }
        }
      }
    }
  }
  assert(sawCrossBB &&
         "ReplaceOperand should pick cross-BB dominating values");
  std::cout << "  [PASS] ReplaceOperand: draws from dominating BBs\n";
}

// ============================================================================
// Test: ShuffleInstructions
// ============================================================================

static void testShuffleInstructions() {
  LLVMContext Ctx;

  // Two independent adds, then a dependent use.
  auto M = parseIR(Ctx, R"(
    define i32 @f(i32 %a, i32 %b, i32 %c, i32 %d) {
      %x = add i32 %a, %b
      %y = add i32 %c, %d
      %z = add i32 %x, %y
      ret i32 %z
    }
  )");

  ShuffleInstructions mut;
  assert(mut.canApply(*M) && "ShuffleInstructions should apply");

  std::string before = moduleToString(*M);
  std::mt19937 rng(2);
  bool changed = mut.apply(*M, rng);
  std::string after = moduleToString(*M);

  assert(changed && "ShuffleInstructions should change something");
  assert(before != after && "Module should differ after mutation");
  assert(isValid(*M) && "Module should still be valid after mutation");

  std::cout << "  [PASS] ShuffleInstructions: reorders, result is valid IR\n";
}

static void testShuffleInstructionsNoTargets() {
  LLVMContext Ctx;

  // Only a single instruction plus terminator — no pair to shuffle.
  auto M = parseIR(Ctx, R"(
    define i32 @f(i32 %a) {
      ret i32 %a
    }
  )");

  ShuffleInstructions mut;
  assert(!mut.canApply(*M) &&
         "ShuffleInstructions should not apply without adjacent pairs");

  std::cout
      << "  [PASS] ShuffleInstructions: rejects module without pairs\n";
}

// ============================================================================
// Test: MoveInstruction
// ============================================================================

static void testMoveInstruction() {
  LLVMContext Ctx;

  // Several independent instructions so the valid range has slack.
  auto M = parseIR(Ctx, R"(
    define i32 @f(i32 %a, i32 %b, i32 %c, i32 %d) {
      %x = add i32 %a, %b
      %y = sub i32 %c, %d
      %z = mul i32 %a, %c
      %w = xor i32 %b, %d
      %r = add i32 %x, %y
      ret i32 %r
    }
  )");

  MoveInstruction mut;
  assert(mut.canApply(*M) && "MoveInstruction should apply");

  bool anyChanged = false;
  for (int seed = 0; seed < 30; ++seed) {
    LLVMContext C2;
    auto M2 = parseIR(C2, R"(
      define i32 @f(i32 %a, i32 %b, i32 %c, i32 %d) {
        %x = add i32 %a, %b
        %y = sub i32 %c, %d
        %z = mul i32 %a, %c
        %w = xor i32 %b, %d
        %r = add i32 %x, %y
        ret i32 %r
      }
    )");
    std::string before = moduleToString(*M2);
    std::mt19937 rng(seed);
    if (mut.apply(*M2, rng)) {
      std::string after = moduleToString(*M2);
      if (before != after)
        anyChanged = true;
    }
    assert(isValid(*M2) && "Module should still be valid after mutation");
  }
  assert(anyChanged && "MoveInstruction should change something across seeds");

  std::cout << "  [PASS] MoveInstruction: moves within BB, result valid\n";
}

// Regression: moves must never break SSA. Run many seeds against a
// multi-BB function so the post-move DominatorTree verification path
// gets exercised alongside the lo/hi bounds.
static void testMoveInstructionDominanceMultiBB() {
  const char *kIR = R"(
    define i32 @f(i32 %a, i32 %b, i32 %c) {
    entry:
      %x = add i32 %a, %b
      %y = sub i32 %c, %a
      %z = mul i32 %x, %y
      %w = xor i32 %z, %b
      %cmp = icmp slt i32 %w, %x
      br i1 %cmp, label %L, label %R
    L:
      %la = add i32 %x, %z
      %lb = mul i32 %la, %y
      %lc = sub i32 %lb, %w
      br label %J
    R:
      %ra = add i32 %y, %z
      %rb = mul i32 %ra, %x
      br label %J
    J:
      %phi = phi i32 [ %lc, %L ], [ %rb, %R ]
      ret i32 %phi
    }
  )";
  MoveInstruction mut;
  for (int seed = 0; seed < 200; ++seed) {
    LLVMContext Ctx;
    auto M = parseIR(Ctx, kIR);
    std::mt19937 rng(seed);
    mut.apply(*M, rng);
    if (!isValid(*M)) {
      std::cerr << "  [FAIL] MoveInstruction broke SSA (seed " << seed
                << "):\n"
                << moduleToString(*M);
      assert(false && "MoveInstruction produced invalid IR");
    }
  }
  std::cout << "  [PASS] MoveInstruction: 200 seeds on multi-BB IR, all valid\n";
}

static void testMoveInstructionChainStress() {
  const char *kIR = R"(
    define i32 @g(i32 %a, i32 %b, i32 %c, i32 %d) {
    e:
      %x = add i32 %a, %b
      %y = sub i32 %c, %d
      %z = mul i32 %x, %y
      %w = xor i32 %z, %a
      %cmp = icmp slt i32 %w, %x
      br i1 %cmp, label %L, label %R
    L:
      %la = add i32 %x, %z
      %lb = mul i32 %la, %y
      br label %J
    R:
      %ra = sub i32 %y, %z
      %rb = xor i32 %ra, %x
      br label %J
    J:
      %phi = phi i32 [ %lb, %L ], [ %rb, %R ]
      %tail = add i32 %phi, %x
      ret i32 %tail
    }
  )";
  MoveInstruction mut;
  for (int trial = 0; trial < 20; ++trial) {
    LLVMContext Ctx;
    auto M = parseIR(Ctx, kIR);
    std::mt19937 rng(trial);
    for (int step = 0; step < 40; ++step) {
      mut.apply(*M, rng);
      assert(isValid(*M) &&
             "MoveInstruction chain should never produce invalid IR");
    }
  }
  std::cout
      << "  [PASS] MoveInstruction: 20 trials x 40 chained moves, all valid\n";
}

static void testMoveInstructionNoTargets() {
  LLVMContext Ctx;

  auto M = parseIR(Ctx, R"(
    define void @f() {
      ret void
    }
  )");

  MoveInstruction mut;
  assert(!mut.canApply(*M) && "MoveInstruction should not apply");

  std::cout << "  [PASS] MoveInstruction: rejects module with no targets\n";
}

// ============================================================================
// Test: InlineCall
//
// InlineCall now splices a corpus function body into a callee *declaration*
// and inlines it; with no CorpusIndex built (as in this unit test process), it
// has nothing to splice and canApply is false. Full splice coverage —
// building a real CorpusIndex and asserting a successful inline — lives in
// src/mutators/tests/inline_call_test.cc.
// ============================================================================

static void testInlineCall() {
  LLVMContext Ctx;

  // Defined callee: no longer a splice target (only declarations are).
  auto M1 = parseIR(Ctx, R"(
    define i32 @callee(i32 %a, i32 %b) {
      %x = add i32 %a, %b
      ret i32 %x
    }
    define i32 @caller(i32 %x, i32 %y) {
      %r = call i32 @callee(i32 %x, i32 %y)
      ret i32 %r
    }
  )");
  InlineCall mut;
  assert(!mut.canApply(*M1) &&
         "InlineCall should not target a defined callee");

  // Call to a declaration, but no CorpusIndex built ⇒ no signature match.
  auto M2 = parseIR(Ctx, R"(
    declare i32 @extern_only(i32)
    define i32 @f(i32 %a) {
      %r = call i32 @extern_only(i32 %a)
      ret i32 %r
    }
  )");
  assert(!mut.canApply(*M2) &&
         "InlineCall should not apply without a corpus match");

  std::cout << "  [PASS] InlineCall: no-op without a built CorpusIndex "
               "(see src/mutators/tests/inline_call_test.cc)\n";
}

// ============================================================================
// Test: RemoveVoidCall
// ============================================================================

static void testRemoveVoidCall() {
  LLVMContext Ctx;

  auto M = parseIR(Ctx, R"(
    declare void @sink(i32)
    define void @f(i32 %a) {
      call void @sink(i32 %a)
      ret void
    }
  )");

  RemoveVoidCall mut;
  assert(mut.canApply(*M) && "RemoveVoidCall should apply");

  std::mt19937 rng(0);
  bool changed = mut.apply(*M, rng);

  assert(changed && "RemoveVoidCall should remove the call");
  assert(isValid(*M) && "Module should still be valid after removal");
  assert(!mut.canApply(*M) && "No more void calls left");

  std::cout << "  [PASS] RemoveVoidCall: removes call, result is valid IR\n";
}

static void testRemoveVoidCallNoTargets() {
  LLVMContext Ctx;

  auto M = parseIR(Ctx, R"(
    define i32 @f(i32 %a) {
      ret i32 %a
    }
  )");

  RemoveVoidCall mut;
  assert(!mut.canApply(*M) && "RemoveVoidCall should not apply");

  std::cout << "  [PASS] RemoveVoidCall: rejects module with no void calls\n";
}

// ============================================================================
// Test: MutateGep (REMOVED — pointer-only, pruned from the integer campaign)
// ============================================================================

// ============================================================================
// Test: ResizeType
// ============================================================================

static void testResizeType() {
  LLVMContext Ctx;

  auto M = parseIR(Ctx, R"(
    define i32 @f(i32 %a, i32 %b, float %x, float %y) {
      %s = add i32 %a, %b
      %f = fadd float %x, %y
      %i = fptosi float %f to i32
      %r = add i32 %s, %i
      ret i32 %r
    }
  )");

  ResizeType mut;
  assert(mut.canApply(*M) && "ResizeType should apply");

  std::string before = moduleToString(*M);
  std::mt19937 rng(4);
  bool changed = mut.apply(*M, rng);
  std::string after = moduleToString(*M);

  assert(changed && "ResizeType should change something");
  assert(before != after && "Module should differ after mutation");
  assert(isValid(*M) && "Module should still be valid after mutation");

  std::cout << "  [PASS] ResizeType: resizes binop, result is valid IR\n";
}

static void testResizeTypeNoTargets() {
  LLVMContext Ctx;

  auto M = parseIR(Ctx, R"(
    define void @f() {
      ret void
    }
  )");

  ResizeType mut;
  assert(!mut.canApply(*M) && "ResizeType should not apply");

  std::cout << "  [PASS] ResizeType: rejects module with no targets\n";
}

// ============================================================================
// Test: EliminateUndef
// ============================================================================

static void testEliminateUndef() {
  LLVMContext Ctx;

  auto M = parseIR(Ctx, R"(
    define i32 @f(i32 %a) {
      %x = add i32 %a, undef
      %y = mul i32 %x, poison
      ret i32 %y
    }
  )");

  EliminateUndef mut;
  assert(mut.canApply(*M) && "EliminateUndef should apply");

  std::string before = moduleToString(*M);
  std::mt19937 rng(9);
  bool changed = mut.apply(*M, rng);
  std::string after = moduleToString(*M);

  assert(changed && "EliminateUndef should change something");
  assert(before != after && "Module should differ after mutation");
  assert(isValid(*M) && "Module should still be valid after mutation");

  std::cout
      << "  [PASS] EliminateUndef: replaces undef, result is valid IR\n";
}

// Regression: EliminateUndef shares the candidate-selection logic with
// ReplaceOperand and previously had the same "earlier in same BB"
// approximation. On multi-BB IR with undef operands, the replacement
// must still honor dominance.
static void testEliminateUndefDominanceMultiBB() {
  const char *kIR = R"(
    define i32 @f(i32 %a, i32 %b) {
    entry:
      %ea = add i32 %a, %b
      br label %L
    L:
      %lu = add i32 %ea, undef
      %lv = mul i32 %lu, %a
      %lw = sub i32 %lv, undef
      br label %J
    J:
      %jp = phi i32 [ %lw, %L ]
      %jr = add i32 %jp, %ea
      ret i32 %jr
    }
  )";
  EliminateUndef mut;
  for (int seed = 0; seed < 200; ++seed) {
    LLVMContext Ctx;
    auto M = parseIR(Ctx, kIR);
    std::mt19937 rng(seed);
    mut.apply(*M, rng);
    if (!isValid(*M)) {
      std::cerr << "  [FAIL] EliminateUndef produced invalid IR (seed "
                << seed << "):\n"
                << moduleToString(*M);
      assert(false);
    }
  }
  std::cout
      << "  [PASS] EliminateUndef: 200 seeds on multi-BB IR, all valid\n";
}

static void testEliminateUndefNoTargets() {
  LLVMContext Ctx;

  auto M = parseIR(Ctx, R"(
    define i32 @f(i32 %a, i32 %b) {
      %x = add i32 %a, %b
      ret i32 %x
    }
  )");

  EliminateUndef mut;
  assert(!mut.canApply(*M) && "EliminateUndef should not apply");

  std::cout << "  [PASS] EliminateUndef: rejects module with no undef\n";
}

// ============================================================================
// Test: ChangeConstant
// ============================================================================

static void testChangeConstant() {
  LLVMContext Ctx;
  auto M = parseIR(Ctx, R"(
    define i32 @f(i32 %a) {
      %x = add i32 %a, 5
      %c = icmp slt i32 %x, 100
      %s = select i1 %c, i32 %x, i32 7
      ret i32 %s
    }
  )");
  ChangeConstant mut;
  assert(mut.canApply(*M) && "ChangeConstant should apply with int constants");
  std::string before = moduleToString(*M);
  std::mt19937 rng(13);
  bool changed = mut.apply(*M, rng);
  assert(changed && "ChangeConstant should change a constant");
  assert(before != moduleToString(*M) && "Module should differ");
  assert(isValid(*M) && "Module should still be valid after mutation");
  std::cout << "  [PASS] ChangeConstant: rewrites a constant, result is valid IR\n";
}

// Stress: many seeds over constant-rich IR (incl. narrow i8 + SWAR magic
// 65793). Every outcome must verify — APInt width handling must be correct.
static void testChangeConstantStress() {
  const char *kIR = R"(
    define i32 @f(i32 %a, i8 %b) {
      %x = add i32 %a, 5
      %y = mul i32 %x, 65793
      %c = icmp ult i32 %y, 255
      %s = select i1 %c, i32 %y, i32 -1
      %t = and i8 %b, 15
      %z = add i32 %s, 0
      ret i32 %z
    }
  )";
  ChangeConstant mut;
  for (int seed = 0; seed < 300; ++seed) {
    LLVMContext Ctx;
    auto M = parseIR(Ctx, kIR);
    std::mt19937 rng(seed);
    mut.apply(*M, rng);
    if (!isValid(*M)) {
      std::cerr << "  [FAIL] ChangeConstant invalid IR (seed " << seed << "):\n"
                << moduleToString(*M);
      assert(false);
    }
  }
  std::cout << "  [PASS] ChangeConstant: 300 seeds (incl. i8), all valid\n";
}

static void testChangeConstantNoTargets() {
  LLVMContext Ctx;
  auto M = parseIR(Ctx, R"(
    define i32 @f(i32 %a, i32 %b) {
      %x = add i32 %a, %b
      ret i32 %x
    }
  )");
  ChangeConstant mut;
  assert(!mut.canApply(*M) &&
         "ChangeConstant should not apply without integer constants");
  std::cout << "  [PASS] ChangeConstant: rejects module with no int constants\n";
}

// ============================================================================
// Test: MutateShiftAmount
// ============================================================================

static void testMutateShiftAmount() {
  MutateShiftAmount mut;
  const char *kIR = R"(
    define i32 @f(i32 %a, i32 %b) {
      %x = shl i32 %a, %b
      %y = lshr i32 %x, 3
      %z = ashr i32 %y, %a
      ret i32 %z
    }
  )";
  {
    LLVMContext Ctx;
    auto M = parseIR(Ctx, kIR);
    assert(mut.canApply(*M) && "MutateShiftAmount should apply to shifts");
  }
  bool anyChanged = false;
  for (int seed = 0; seed < 40; ++seed) {
    LLVMContext Ctx;
    auto M = parseIR(Ctx, kIR);
    std::string before = moduleToString(*M);
    std::mt19937 rng(seed);
    if (mut.apply(*M, rng) && moduleToString(*M) != before)
      anyChanged = true;
    assert(isValid(*M) && "Module should still be valid after mutation");
  }
  assert(anyChanged && "MutateShiftAmount should change a shift amount");
  std::cout << "  [PASS] MutateShiftAmount: sets boundary amounts, result valid\n";
}

static void testMutateShiftAmountFunnel() {
  LLVMContext Ctx;
  auto M = parseIR(Ctx, R"(
    declare i32 @llvm.fshl.i32(i32, i32, i32)
    define i32 @g(i32 %a, i32 %b, i32 %c) {
      %r = call i32 @llvm.fshl.i32(i32 %a, i32 %b, i32 %c)
      ret i32 %r
    }
  )");
  MutateShiftAmount mut;
  assert(mut.canApply(*M) && "should apply to funnel-shift intrinsic");
  std::mt19937 rng(1);
  bool changed = mut.apply(*M, rng);
  assert(changed && "should set the rotate amount");
  assert(isValid(*M) && "Module should still be valid after mutation");
  std::cout << "  [PASS] MutateShiftAmount: handles fshl rotate amount\n";
}

static void testMutateShiftAmountNoTargets() {
  LLVMContext Ctx;
  auto M = parseIR(Ctx, R"(
    define i32 @f(i32 %a, i32 %b) {
      %x = add i32 %a, %b
      ret i32 %x
    }
  )");
  MutateShiftAmount mut;
  assert(!mut.canApply(*M) && "should not apply without shifts");
  std::cout << "  [PASS] MutateShiftAmount: rejects module with no shifts\n";
}

// ============================================================================
// Test: WrapBitmanip
// ============================================================================

static void testWrapBitmanip() {
  LLVMContext Ctx;
  auto M = parseIR(Ctx, R"(
    define i32 @f(i32 %a, i32 %b) {
      %x = add i32 %a, %b
      %y = mul i32 %x, %b
      ret i32 %y
    }
  )");
  WrapBitmanip mut;
  assert(mut.canApply(*M) && "WrapBitmanip should apply to int instructions");
  std::string before = moduleToString(*M);
  std::mt19937 rng(2);
  bool changed = mut.apply(*M, rng);
  assert(changed && "WrapBitmanip should wrap a value");
  assert(before != moduleToString(*M) && "Module should differ");
  assert(isValid(*M) && "Module must verify (no self-reference / broken SSA)");
  std::cout << "  [PASS] WrapBitmanip: wraps a value in a bitmanip idiom, valid\n";
}

// Stress: i8/i16/i32/i64 values exercise every idiom branch — bswap (only on
// multiple-of-16 widths), funnel-rotate, and the pure-IR idioms. The use-
// rewiring must never create a self-reference, so every result must verify.
static void testWrapBitmanipStress() {
  const char *kIR = R"(
    define i64 @f(i8 %a, i16 %b, i32 %c, i64 %d) {
      %x8  = add i8 %a, 1
      %x16 = add i16 %b, 1
      %x32 = add i32 %c, 1
      %x64 = add i64 %d, 1
      %u8  = zext i8 %x8 to i64
      %u16 = zext i16 %x16 to i64
      %u32 = zext i32 %x32 to i64
      %s1  = add i64 %u8, %u16
      %s2  = add i64 %u32, %x64
      %r   = add i64 %s1, %s2
      ret i64 %r
    }
  )";
  WrapBitmanip mut;
  for (int seed = 0; seed < 400; ++seed) {
    LLVMContext Ctx;
    auto M = parseIR(Ctx, kIR);
    std::mt19937 rng(seed);
    mut.apply(*M, rng);
    if (!isValid(*M)) {
      std::cerr << "  [FAIL] WrapBitmanip invalid IR (seed " << seed << "):\n"
                << moduleToString(*M);
      assert(false);
    }
  }
  std::cout << "  [PASS] WrapBitmanip: 400 seeds across i8/i16/i32/i64, all valid\n";
}

static void testWrapBitmanipNoTargets() {
  LLVMContext Ctx;
  auto M = parseIR(Ctx, R"(
    define i32 @f(i32 %a) {
      ret i32 %a
    }
  )");
  WrapBitmanip mut;
  assert(!mut.canApply(*M) &&
         "WrapBitmanip should not apply with no wrappable instructions");
  std::cout << "  [PASS] WrapBitmanip: rejects module with no wrappable values\n";
}

// ============================================================================
// Test: ArithIdentitySubstitution
// ============================================================================

static void testArithIdentitySubstitution() {
  // Each line matches one rewrite pattern; many seeds must all stay valid and
  // (collectively) change something.
  const char *kIR = R"(
    define i32 @f(i32 %x) {
      %a = mul i32 %x, 8
      %b = shl i32 %a, 3
      %c = add i32 %b, %b
      %d = mul i32 %c, -1
      %e = sub i32 0, %d
      ret i32 %e
    }
  )";
  ArithIdentitySubstitution mut;
  {
    LLVMContext Ctx; auto M = parseIR(Ctx, kIR);
    assert(mut.canApply(*M) && "should apply to mul/shl/add-self/sub-0 forms");
  }
  bool anyChanged = false;
  for (int seed = 0; seed < 60; ++seed) {
    LLVMContext Ctx; auto M = parseIR(Ctx, kIR);
    std::string before = moduleToString(*M);
    std::mt19937 rng(seed);
    if (mut.apply(*M, rng) && moduleToString(*M) != before) anyChanged = true;
    assert(isValid(*M) && "result must be valid IR");
  }
  assert(anyChanged && "should rewrite a form across seeds");
  std::cout << "  [PASS] ArithIdentitySubstitution: rewrites equivalent forms, valid\n";
}

static void testArithIdentityNoTargets() {
  LLVMContext Ctx;
  auto M = parseIR(Ctx, R"(
    define i32 @f(i32 %a, i32 %b) {
      %x = mul i32 %a, 3
      %y = and i32 %x, %b
      ret i32 %y
    }
  )");
  ArithIdentitySubstitution mut; // mul by 3 (non-pow2), and -> no rewrite
  assert(!mut.canApply(*M) && "should not apply without a recognized form");
  std::cout << "  [PASS] ArithIdentitySubstitution: rejects non-matching IR\n";
}

// ============================================================================
// Test: NarrowThenWiden
// ============================================================================

static void testNarrowThenWiden() {
  LLVMContext Ctx;
  auto M = parseIR(Ctx, R"(
    define i64 @f(i64 %a, i64 %b) {
      %x = add i64 %a, %b
      %y = mul i64 %x, %b
      ret i64 %y
    }
  )");
  NarrowThenWiden mut;
  assert(mut.canApply(*M) && "should apply to >32-bit binops");
  std::string before = moduleToString(*M);
  std::mt19937 rng(1);
  bool changed = mut.apply(*M, rng);
  assert(changed && "should rewrite a wide binop");
  assert(before != moduleToString(*M) && "module should differ");
  assert(isValid(*M) && "result must be valid IR (trunc/op/sext)");
  std::cout << "  [PASS] NarrowThenWiden: sext(op(trunc,trunc)), valid\n";
}

static void testNarrowThenWidenNoTargets() {
  LLVMContext Ctx;
  auto M = parseIR(Ctx, R"(
    define i32 @f(i32 %a, i32 %b) {
      %x = add i32 %a, %b
      ret i32 %x
    }
  )");
  NarrowThenWiden mut; // i32 is not > 32 bits
  assert(!mut.canApply(*M) && "should not apply to <=32-bit binops");
  std::cout << "  [PASS] NarrowThenWiden: rejects <=32-bit binops\n";
}

// ============================================================================
// Test: DemoteIntrinsicToExpansion
// ============================================================================

static void testDemoteIntrinsicToExpansion() {
  DemoteIntrinsicToExpansion mut;
  const char *kIR = R"(
    declare i32 @llvm.ctpop.i32(i32)
    declare i64 @llvm.bswap.i64(i64)
    declare i16 @llvm.bswap.i16(i16)
    define i64 @f(i32 %a, i64 %b, i16 %c) {
      %p = call i32 @llvm.ctpop.i32(i32 %a)
      %s = call i64 @llvm.bswap.i64(i64 %b)
      %t = call i16 @llvm.bswap.i16(i16 %c)
      %pz = zext i32 %p to i64
      %tz = zext i16 %t to i64
      %r1 = add i64 %s, %pz
      %r2 = add i64 %r1, %tz
      ret i64 %r2
    }
  )";
  {
    LLVMContext Ctx; auto M = parseIR(Ctx, kIR);
    assert(mut.canApply(*M) && "should apply to ctpop/bswap calls");
  }
  // Apply repeatedly across seeds; every expansion must verify, and after
  // enough applications all three intrinsic calls are gone.
  bool anyChanged = false;
  for (int seed = 0; seed < 40; ++seed) {
    LLVMContext Ctx; auto M = parseIR(Ctx, kIR);
    std::mt19937 rng(seed);
    if (mut.apply(*M, rng)) anyChanged = true;
    assert(isValid(*M) && "expansion must be valid IR");
  }
  assert(anyChanged && "should expand an intrinsic");
  // Drain all of them on one module.
  {
    LLVMContext Ctx; auto M = parseIR(Ctx, kIR);
    std::mt19937 rng(0);
    for (int i = 0; i < 10 && mut.canApply(*M); ++i) mut.apply(*M, rng);
    assert(!mut.canApply(*M) && "all ctpop/bswap should be expanded");
    assert(isValid(*M));
  }
  std::cout << "  [PASS] DemoteIntrinsicToExpansion: expands ctpop/bswap, valid\n";
}

static void testDemoteIntrinsicNoTargets() {
  LLVMContext Ctx;
  auto M = parseIR(Ctx, R"(
    define i32 @f(i32 %a, i32 %b) {
      %x = add i32 %a, %b
      ret i32 %x
    }
  )");
  DemoteIntrinsicToExpansion mut;
  assert(!mut.canApply(*M) && "should not apply without ctpop/bswap");
  std::cout << "  [PASS] DemoteIntrinsicToExpansion: rejects IR with no targets\n";
}

// ============================================================================
// Test: Composability — apply every ordered pair of mutations
// ============================================================================

static void testMutationComposability() {
  auto &reg = MutationRegistry::instance();
  const auto &muts = reg.all();
  const size_t N = muts.size();
  assert(N > 0 && "Registry should have mutations");

  const unsigned seeds[] = {0, 1, 2};
  const size_t numSeeds = sizeof(seeds) / sizeof(seeds[0]);

  size_t total = 0;
  size_t valid = 0;
  std::vector<std::string> failures;

  for (size_t i = 0; i < N; ++i) {
    for (size_t j = 0; j < N; ++j) {
      for (size_t s = 0; s < numSeeds; ++s) {
        LLVMContext Ctx;
        auto M = parseIR(Ctx, kRichIR);
        std::mt19937 rng(seeds[s]);
        (void)muts[i]->apply(*M, rng);
        (void)muts[j]->apply(*M, rng);
        ++total;
        if (isValid(*M)) {
          ++valid;
        } else {
          failures.push_back(muts[i]->name() + " -> " + muts[j]->name() +
                             " (seed " + std::to_string(seeds[s]) + ")");
        }
      }
    }
  }

  for (const auto &f : failures)
    std::cerr << "  [WARN] Composability failure: " << f << "\n";

  assert(valid * 10 >= total * 9 &&
         "At least 90% of mutation pairs should produce valid IR");

  std::cout << "  [PASS] Composability: " << N << "x" << N << "x" << numSeeds
            << " mutation pairs, " << valid << "/" << total
            << " produced valid IR\n";
}

// ============================================================================
// Test: Chain stress — apply 20 random mutations in sequence
// ============================================================================

static void testMutationChainStress() {
  auto &reg = MutationRegistry::instance();
  const int kTrials = 10;
  const int kChainLen = 20;

  for (int trial = 0; trial < kTrials; ++trial) {
    LLVMContext Ctx;
    auto M = parseIR(Ctx, kRichIR);
    std::mt19937 rng(trial);
    std::vector<std::string> chain;
    for (int step = 0; step < kChainLen; ++step) {
      std::string applied = reg.applyRandom(*M, rng);
      if (!applied.empty())
        chain.push_back(applied);
    }
    if (!isValid(*M)) {
      std::cerr << "  [FAIL] ChainStress trial " << trial
                << " produced invalid IR after sequence:\n";
      for (size_t i = 0; i < chain.size(); ++i)
        std::cerr << "    " << i << ": " << chain[i] << "\n";
      assert(false && "Mutation chain produced invalid IR");
    }
  }

  std::cout << "  [PASS] ChainStress: " << kTrials << " trials x " << kChainLen
            << " mutations, all produced valid IR\n";
}

// ============================================================================
// Test: Registry
// ============================================================================

static void testRegistry() {
  LLVMContext Ctx;

  auto M = parseIR(Ctx, R"(
    define i32 @f(i32 %a, i32 %b) {
      %x = add i32 %a, %b
      ret i32 %x
    }
  )");

  auto &reg = MutationRegistry::instance();
  assert(!reg.all().empty() && "Registry should have mutations");

  // Some mutations may randomly no-op on a given seed; try a few.
  std::mt19937 rng(123);
  std::string applied;
  for (int tries = 0; tries < 20 && applied.empty(); ++tries)
    applied = reg.applyRandom(*M, rng);
  assert(!applied.empty() && "Registry should apply a mutation");
  assert(isValid(*M) && "Module should be valid after registry mutation");

  std::cout << "  [PASS] Registry: applied '" << applied << "', result valid\n";
}

// ============================================================================
// Main
// ============================================================================

int main() {
  std::cout << "=== Regatoni mutation tests ===\n";

  testSwapBinOp();
  testSwapBinOpNoStaleFlags();
  testSwapBinOpNoTargets();
  testSwapCmpPredicate();
  testSwapCmpPredicateNoTargets();
  testModifyFlags();
  testModifyFlagsNoTargets();
  testSwapOperands();
  testSwapOperandsNoTargets();
  testReplaceOperand();
  testReplaceOperandNoTargets();
  testReplaceOperandDominanceMultiBB();
  testReplaceOperandChainStress();
  testReplaceOperandUsesDominatingBB();
  testShuffleInstructions();
  testShuffleInstructionsNoTargets();
  testMoveInstruction();
  testMoveInstructionDominanceMultiBB();
  testMoveInstructionChainStress();
  testMoveInstructionNoTargets();
  testInlineCall();
  testRemoveVoidCall();
  testRemoveVoidCallNoTargets();
  testResizeType();
  testResizeTypeNoTargets();
  testEliminateUndef();
  testEliminateUndefDominanceMultiBB();
  testEliminateUndefNoTargets();
  testChangeConstant();
  testChangeConstantStress();
  testChangeConstantNoTargets();
  testMutateShiftAmount();
  testMutateShiftAmountFunnel();
  testMutateShiftAmountNoTargets();
  testWrapBitmanip();
  testWrapBitmanipStress();
  testWrapBitmanipNoTargets();
  testArithIdentitySubstitution();
  testArithIdentityNoTargets();
  testNarrowThenWiden();
  testNarrowThenWidenNoTargets();
  testDemoteIntrinsicToExpansion();
  testDemoteIntrinsicNoTargets();
  testMutationComposability();
  testMutationChainStress();
  testRegistry();

  std::cout << "\nAll tests passed.\n";
  return 0;
}