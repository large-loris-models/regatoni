#include "src/spec-inject/injector.h"

#include "llvm/IR/Argument.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Dominators.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/GlobalValue.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Type.h"
#include "llvm/IR/Verifier.h"
#include "llvm/IRReader/IRReader.h"
#include "llvm/Linker/Linker.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/SourceMgr.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Transforms/Utils/Cloning.h"
#include "llvm/Transforms/Utils/ValueMapper.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace spec_inject {

namespace fs = std::filesystem;
using namespace llvm;

void mergeStats(InjectorStats &d, const InjectorStats &s) {
  d.hosts_seen           += s.hosts_seen;
  d.hosts_parsed         += s.hosts_parsed;
  d.hosts_parse_failed   += s.hosts_parse_failed;
  d.fns_seen             += s.fns_seen;
  d.fns_skipped_no_values += s.fns_skipped_no_values;
  d.attempts             += s.attempts;
  d.written              += s.written;
  d.skipped_no_compat    += s.skipped_no_compat;
  d.skipped_no_anchor    += s.skipped_no_anchor;
  d.skipped_no_use       += s.skipped_no_use;
  d.link_failed          += s.link_failed;
  d.verify_failed        += s.verify_failed;
  d.io_failed            += s.io_failed;
  for (auto &kv : s.written_by_litmus)       d.written_by_litmus[kv.first]       += kv.second;
  for (auto &kv : s.verify_fail_by_litmus)   d.verify_fail_by_litmus[kv.first]   += kv.second;
}

namespace {

// Type filters.

// A "scalar-or-ptr" type that we want to consider as an available
// value or as a litmus parameter source. Vectors / aggregates are
// excluded; the litmus library already excludes vector / aggregate
// return types, so injection into such slots is out of scope here.
bool isUsableType(Type *T) {
  if (T->isVoidTy()) return false;
  if (T->isLabelTy() || T->isTokenTy() || T->isMetadataTy()) return false;
  if (T->isStructTy() || T->isArrayTy() || T->isVectorTy()) return false;
  return T->isIntegerTy() || T->isFloatingPointTy() || T->isPointerTy();
}

std::string canonicalType(Type *T) {
  std::string s;
  raw_string_ostream os(s);
  T->print(os);
  os.flush();
  size_t a = 0, b = s.size();
  while (a < b && (s[a] == ' ' || s[a] == '\t')) ++a;
  while (b > a && (s[b - 1] == ' ' || s[b - 1] == '\t')) --b;
  return s.substr(a, b - a);
}

struct ValueSlot {
  Value *val;          // Function arg or value-producing instruction
  Instruction *def;    // null iff val is an Argument
};

// Insertion point candidate for an anchor: the instruction immediately
// after `def`, or (if def is a PHI) the first non-PHI in def's BB, or
// (if def is null = arg) the first non-PHI of the entry block.
Instruction *firstInsertPoint(Function &F, Instruction *def) {
  if (!def) {
    BasicBlock &BB = F.getEntryBlock();
    return &*BB.getFirstNonPHIIt();
  }
  if (isa<PHINode>(def)) {
    return &*def->getParent()->getFirstNonPHIIt();
  }
  Instruction *next = def->getNextNode();
  if (next) return next;
  // def is the terminator of its block — we can't insert after it.
  // Caller treats nullptr as "no valid insertion point here".
  return nullptr;
}

// Does V dominate the insertion point IP?
// IP is itself an Instruction (we insert BEFORE IP). For domination,
// a value defined by an instruction D dominates IP iff
// DT.dominates(D, IP); Arguments always dominate everything.
bool dominatesIP(Value *V, Instruction *IP, const DominatorTree &DT) {
  if (isa<Argument>(V)) return true;
  if (auto *I = dyn_cast<Instruction>(V)) return DT.dominates(I, IP);
  if (isa<Constant>(V)) return true;
  return false;
}

}  // namespace

Injector::Injector(const LitmusLibrary &lib, InjectorOpts opts)
    : lib_(lib), opts_(opts) {}

void Injector::processFile(const std::string &host_path,
                           const std::string &out_dir,
                           std::mt19937 &rng,
                           InjectorStats &stats) {
  ++stats.hosts_seen;

  LLVMContext Ctx;
  Ctx.setDiagnosticHandlerCallBack([](const DiagnosticInfo *, void *) {},
                                   nullptr);
  SMDiagnostic Err;
  std::unique_ptr<Module> M = parseIRFile(host_path, Err, Ctx);
  if (!M) { ++stats.hosts_parse_failed; return; }
  ++stats.hosts_parsed;

  fs::path hpath(host_path);
  std::string host_stem = hpath.stem().string();

  for (Function &F : *M) {
    if (F.isDeclaration()) continue;
    ++stats.fns_seen;

    // 1. Collect SSA values (args + non-void instruction results).
    std::vector<ValueSlot> slots;
    for (Argument &A : F.args()) {
      if (!isUsableType(A.getType())) continue;
      slots.push_back({&A, nullptr});
    }
    for (BasicBlock &BB : F) {
      for (Instruction &I : BB) {
        if (!isUsableType(I.getType())) continue;
        slots.push_back({&I, &I});
      }
    }
    if (slots.empty()) { ++stats.fns_skipped_no_values; continue; }

    // 2. Type buckets + available-types set.
    std::set<std::string> available_types;
    std::map<std::string, std::vector<ValueSlot *>> by_type;
    for (ValueSlot &v : slots) {
      std::string ts = canonicalType(v.val->getType());
      available_types.insert(ts);
      by_type[ts].push_back(&v);
    }

    // 3. Find candidate litmus tests. We require:
    //    - litmus return type R matches some available host type.
    //    - every litmus param type appears in available_types.
    //    - litmus param[0] type == return type R, so the call's return
    //      can RAUW a use of the same-typed source value.
    std::vector<const LitmusEntry *> candidates;
    for (const std::string &rt : available_types) {
      auto comp = lib_.getCompatible(rt, available_types);
      for (const LitmusEntry *e : comp) {
        if (e->param_type_strs.empty()) continue;
        if (e->param_type_strs[0] != e->return_type_str) continue;
        candidates.push_back(e);
      }
    }
    if (candidates.empty()) { ++stats.skipped_no_compat; continue; }

    // 4. Sample up to N distinct litmus tests.
    std::shuffle(candidates.begin(), candidates.end(), rng);
    size_t limit = std::min(candidates.size(), opts_.max_injections_per_fn);

    DominatorTree DT(F);

    for (size_t k = 0; k < limit; ++k) {
      const LitmusEntry *L = candidates[k];
      ++stats.attempts;

      // 5. Iterate over anchor candidates (values of L's return type)
      //    until we find one that also yields a valid downstream use
      //    slot. Without this loop, an anchor whose only user is a
      //    phi (skipped for RAUW) wastes the attempt.
      auto &bucket = by_type[L->return_type_str];
      std::vector<ValueSlot *> shuffled_bucket = bucket;
      std::shuffle(shuffled_bucket.begin(), shuffled_bucket.end(), rng);

      ValueSlot *vf = nullptr;
      Instruction *IP = nullptr;       // insertion point in ORIGINAL F
      std::vector<Value *> orig_args;  // args for the new call
      Use *picked_use = nullptr;       // use slot to RAUW

      enum class Reason { Ok, NoAnchor, NoArg, NoUse };
      Reason last_reason = Reason::NoAnchor;

      for (ValueSlot *cand : shuffled_bucket) {
        Instruction *ip = firstInsertPoint(F, cand->def);
        if (!ip) { last_reason = Reason::NoAnchor; continue; }

        // For each extra param (i >= 1) pick a dominating value.
        std::vector<Value *> args;
        args.push_back(cand->val);
        bool args_ok = true;
        for (size_t i = 1; i < L->param_type_strs.size(); ++i) {
          const std::string &pt = L->param_type_strs[i];
          auto bit = by_type.find(pt);
          if (bit == by_type.end()) { args_ok = false; break; }
          std::vector<ValueSlot *> cands = bit->second;
          std::shuffle(cands.begin(), cands.end(), rng);
          Value *chosen = nullptr;
          for (ValueSlot *c : cands) {
            if (dominatesIP(c->val, ip, DT)) { chosen = c->val; break; }
          }
          if (!chosen) { args_ok = false; break; }
          args.push_back(chosen);
        }
        if (!args_ok) { last_reason = Reason::NoArg; continue; }

        // Find a non-phi use of cand->val that the call (at ip) will
        // dominate. PHI users are skipped: the predecessor-edge
        // dominance dance is more complex than the v1 cost/benefit
        // justifies.
        Use *u_pick = nullptr;
        std::vector<Use *> all_uses;
        for (Use &U : cand->val->uses()) all_uses.push_back(&U);
        std::shuffle(all_uses.begin(), all_uses.end(), rng);
        for (Use *u : all_uses) {
          auto *UI = dyn_cast<Instruction>(u->getUser());
          if (!UI) continue;
          if (UI->getParent()->getParent() != &F) continue;
          if (isa<PHINode>(UI)) continue;
          if (!DT.dominates(ip, UI)) continue;
          u_pick = u;
          break;
        }
        if (!u_pick) { last_reason = Reason::NoUse; continue; }

        // All three sub-decisions made — commit and exit the anchor loop.
        vf = cand;
        IP = ip;
        orig_args = std::move(args);
        picked_use = u_pick;
        last_reason = Reason::Ok;
        break;
      }

      if (last_reason == Reason::NoAnchor) { ++stats.skipped_no_anchor; continue; }
      if (last_reason == Reason::NoArg)    { ++stats.skipped_no_anchor; continue; }
      if (last_reason == Reason::NoUse)    { ++stats.skipped_no_use;    continue; }

      // 8. Clone the host module. The clone gets its own VMap that
      //    maps original IR values to their clones.
      ValueToValueMapTy VMap;
      std::unique_ptr<Module> CM = CloneModule(*M, VMap);
      if (!CM) { ++stats.link_failed; continue; }

      // Look up clone-side counterparts. Function args / instructions
      // / functions are all in VMap.
      Instruction *IP_clone = cast<Instruction>(VMap[IP]);
      std::vector<Value *> clone_args;
      clone_args.reserve(orig_args.size());
      bool args_lookup_ok = true;
      for (Value *V : orig_args) {
        auto it = VMap.find(V);
        if (it == VMap.end()) { args_lookup_ok = false; break; }
        clone_args.push_back(it->second);
      }
      if (!args_lookup_ok) { ++stats.link_failed; continue; }

      Instruction *user_clone = cast<Instruction>(VMap[picked_use->getUser()]);
      unsigned operand_no = picked_use->getOperandNo();

      // 9. Materialize the litmus module into the clone's context and
      //    Linker::linkModules. Keep the source function at its
      //    original (external) linkage during link so the linker
      //    actually carries it across; we switch to InternalLinkage
      //    after the call is created so the inliner can fold the body.
      std::unique_ptr<Module> LM = lib_.cloneInto(*L, CM->getContext());
      if (!LM) { ++stats.link_failed; continue; }
      Function *LFsrc = LM->getFunction(L->fn_name);
      if (!LFsrc) { ++stats.link_failed; continue; }

      // Defensive: if CM already has a function with the same name as
      // the litmus, rename in the source so the link emits a fresh
      // name (avoids OverrideFromSrc-style collisions on later attempts).
      if (CM->getFunction(L->fn_name)) {
        std::string newname = "__spec_inject_" + L->fn_name;
        while (CM->getFunction(newname))
          newname += "_x";
        LFsrc->setName(newname);
      }
      std::string lit_name_after_link = LFsrc->getName().str();

      if (Linker::linkModules(*CM, std::move(LM), Linker::Flags::None)) {
        ++stats.link_failed;
        continue;
      }
      Function *LF = CM->getFunction(lit_name_after_link);
      if (!LF) { ++stats.link_failed; continue; }

      // 10. Insert the call at IP_clone (before IP_clone, which puts
      //     it immediately AFTER the anchor's def-position) and RAUW
      //     the picked use.
      IRBuilder<> B(IP_clone);
      // sanity check arg types match the litmus function's parameter
      // types in the linked module (paranoid; signatures should match
      // by construction, but link could have changed something).
      FunctionType *FT = LF->getFunctionType();
      if (FT->getNumParams() != clone_args.size()) {
        ++stats.link_failed;
        continue;
      }
      bool type_mismatch = false;
      for (unsigned i = 0; i < clone_args.size(); ++i) {
        if (clone_args[i]->getType() != FT->getParamType(i)) {
          type_mismatch = true; break;
        }
      }
      if (type_mismatch) { ++stats.link_failed; continue; }
      CallInst *call = B.CreateCall(LF, clone_args, "lit");

      // RAUW the chosen use slot.
      if (user_clone->getType() == call->getType() ||
          user_clone->getOperand(operand_no)->getType() == call->getType()) {
        user_clone->setOperand(operand_no, call);
      } else {
        ++stats.link_failed;
        continue;
      }

      // Now that the call references LF, switch LF to InternalLinkage.
      // Safe: there is exactly one in-module reference and no external
      // callers. The inliner is then free to fold the body.
      LF->setLinkage(GlobalValue::InternalLinkage);

      // 11. Verify the modified clone.
      std::string verr;
      raw_string_ostream errOS(verr);
      if (verifyModule(*CM, &errOS)) {
        ++stats.verify_failed;
        ++stats.verify_fail_by_litmus[L->short_name];
        continue;
      }

      // 12. Write the output file.
      std::ostringstream name_ss;
      name_ss << host_stem << "__inject_" << L->short_name << ".ll";
      fs::path out_path = fs::path(out_dir) / name_ss.str();
      std::error_code ec;
      raw_fd_ostream os(out_path.string(), ec);
      if (ec) { ++stats.io_failed; continue; }
      CM->print(os, nullptr);
      os.close();
      ++stats.written;
      ++stats.written_by_litmus[L->short_name];
    }
  }
}

}  // namespace spec_inject
