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
#include "src/mutators/ir_mutations/modify_attributes.h"
#include "src/mutators/ir_mutations/mutate_gep.h"
#include "src/mutators/ir_mutations/resize_type.h"
#include "src/mutators/ir_mutations/mutate_unary.h"
#include "src/mutators/ir_mutations/eliminate_undef.h"
#include "llvm/IR/LLVMContext.h"
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
// ============================================================================

static void testInlineCall() {
  LLVMContext Ctx;

  auto M = parseIR(Ctx, R"(
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
  assert(mut.canApply(*M) && "InlineCall should apply");

  std::string before = moduleToString(*M);
  std::mt19937 rng(1);
  bool changed = mut.apply(*M, rng);
  std::string after = moduleToString(*M);

  assert(changed && "InlineCall should inline");
  assert(before != after && "Module should differ after inlining");
  assert(isValid(*M) && "Module should still be valid after inlining");

  std::cout << "  [PASS] InlineCall: inlines call, result is valid IR\n";
}

static void testInlineCallNoTargets() {
  LLVMContext Ctx;

  auto M = parseIR(Ctx, R"(
    declare i32 @extern_only(i32)
    define i32 @f(i32 %a) {
      %r = call i32 @extern_only(i32 %a)
      ret i32 %r
    }
  )");

  InlineCall mut;
  assert(!mut.canApply(*M) &&
         "InlineCall should not apply without inlineable callees");

  std::cout
      << "  [PASS] InlineCall: rejects module with only external callees\n";
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
// Test: ModifyAttributes
// ============================================================================

static void testModifyAttributes() {
  LLVMContext Ctx;

  auto M = parseIR(Ctx, R"(
    define i32 @f(i32 %a, i32 %b) {
      %x = add i32 %a, %b
      ret i32 %x
    }
  )");

  ModifyAttributes mut;
  assert(mut.canApply(*M) && "ModifyAttributes should apply");

  bool anyChanged = false;
  for (int seed = 0; seed < 30; ++seed) {
    LLVMContext C2;
    auto M2 = parseIR(C2, R"(
      define i32 @f(i32 %a, i32 %b) {
        %x = add i32 %a, %b
        ret i32 %x
      }
    )");
    std::string before = moduleToString(*M2);
    std::mt19937 rng(seed);
    mut.apply(*M2, rng);
    std::string after = moduleToString(*M2);
    if (before != after)
      anyChanged = true;
    assert(isValid(*M2) && "Module should still be valid after mutation");
  }
  assert(anyChanged && "ModifyAttributes should change something across seeds");

  std::cout << "  [PASS] ModifyAttributes: toggles attrs, result is valid IR\n";
}

static void testModifyAttributesNoTargets() {
  LLVMContext Ctx;

  auto M = parseIR(Ctx, R"(
    define void @f(float %a) {
      ret void
    }
  )");

  ModifyAttributes mut;
  assert(!mut.canApply(*M) && "ModifyAttributes should not apply");

  std::cout
      << "  [PASS] ModifyAttributes: rejects module with no integer slots\n";
}

// ============================================================================
// Test: MutateGep
// ============================================================================

static void testMutateGep() {
  LLVMContext Ctx;

  auto M = parseIR(Ctx, R"(
    define ptr @f(ptr %p, i64 %i) {
      %q = getelementptr inbounds i32, ptr %p, i64 %i
      %r = getelementptr inbounds [4 x i32], ptr %q, i64 0, i64 2
      ret ptr %r
    }
  )");

  MutateGep mut;
  assert(mut.canApply(*M) && "MutateGep should apply");

  bool anyChanged = false;
  for (int seed = 0; seed < 30; ++seed) {
    LLVMContext C2;
    auto M2 = parseIR(C2, R"(
      define ptr @f(ptr %p, i64 %i) {
        %q = getelementptr inbounds i32, ptr %p, i64 %i
        %r = getelementptr inbounds [4 x i32], ptr %q, i64 0, i64 2
        ret ptr %r
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
  assert(anyChanged && "MutateGep should change something across seeds");

  std::cout << "  [PASS] MutateGep: mutates gep, result is valid IR\n";
}

static void testMutateGepNarrowIndex() {
  // Regression: narrow-width constant index (e.g. i8 at its max) must not
  // trigger APInt bit-width assertion when the mutation adds/subtracts a delta.
  for (int seed = 0; seed < 30; ++seed) {
    LLVMContext Ctx;
    auto M = parseIR(Ctx, R"(
      define ptr @f(ptr %p) {
        %q = getelementptr i32, ptr %p, i8 127
        %r = getelementptr i32, ptr %q, i8 -128
        ret ptr %r
      }
    )");
    MutateGep mut;
    assert(mut.canApply(*M));
    std::mt19937 rng(seed);
    mut.apply(*M, rng);
    assert(isValid(*M) && "Module should be valid after narrow-index mutation");
  }
  std::cout << "  [PASS] MutateGep: narrow-width index does not overflow\n";
}

static void testMutateGepNoTargets() {
  LLVMContext Ctx;

  auto M = parseIR(Ctx, R"(
    define i32 @f(i32 %a) {
      ret i32 %a
    }
  )");

  MutateGep mut;
  assert(!mut.canApply(*M) && "MutateGep should not apply without gep");

  std::cout << "  [PASS] MutateGep: rejects module with no gep\n";
}

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
// Test: MutateUnary
// ============================================================================

static void testMutateUnary() {
  LLVMContext Ctx;

  auto M = parseIR(Ctx, R"(
    define float @f(float %x, float %y) {
      %r = fadd float %x, %y
      ret float %r
    }
  )");

  MutateUnary mut;
  assert(mut.canApply(*M) && "MutateUnary should apply");

  std::string before = moduleToString(*M);
  std::mt19937 rng(5);
  bool changed = mut.apply(*M, rng);
  std::string after = moduleToString(*M);

  assert(changed && "MutateUnary should change something");
  assert(before != after && "Module should differ after mutation");
  assert(isValid(*M) && "Module should still be valid after mutation");

  std::cout << "  [PASS] MutateUnary: replaces with fneg, result is valid IR\n";
}

static void testMutateUnaryNoTargets() {
  LLVMContext Ctx;

  auto M = parseIR(Ctx, R"(
    define i32 @f(i32 %a, i32 %b) {
      %r = add i32 %a, %b
      ret i32 %r
    }
  )");

  MutateUnary mut;
  assert(!mut.canApply(*M) && "MutateUnary should not apply");

  std::cout << "  [PASS] MutateUnary: rejects module with no FP binops\n";
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
  testSwapBinOpNoTargets();
  testSwapCmpPredicate();
  testSwapCmpPredicateNoTargets();
  testModifyFlags();
  testModifyFlagsNoTargets();
  testSwapOperands();
  testSwapOperandsNoTargets();
  testReplaceOperand();
  testReplaceOperandNoTargets();
  testShuffleInstructions();
  testShuffleInstructionsNoTargets();
  testMoveInstruction();
  testMoveInstructionNoTargets();
  testInlineCall();
  testInlineCallNoTargets();
  testRemoveVoidCall();
  testRemoveVoidCallNoTargets();
  testModifyAttributes();
  testModifyAttributesNoTargets();
  testMutateGep();
  testMutateGepNarrowIndex();
  testMutateGepNoTargets();
  testResizeType();
  testResizeTypeNoTargets();
  testMutateUnary();
  testMutateUnaryNoTargets();
  testEliminateUndef();
  testEliminateUndefNoTargets();
  testRegistry();

  std::cout << "\nAll tests passed.\n";
  return 0;
}