#include "src/spec-inject/litmus_library.h"

#include "llvm/ADT/SmallString.h"
#include "llvm/Bitcode/BitcodeWriter.h"
#include "llvm/IR/DiagnosticInfo.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Type.h"
#include "llvm/IR/Verifier.h"
#include "llvm/IRReader/IRReader.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/SourceMgr.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Bitcode/BitcodeReader.h"
#include "llvm/Support/Error.h"

#include <algorithm>
#include <cstdio>
#include <system_error>

namespace spec_inject {

namespace fs = std::filesystem;
using namespace llvm;

LitmusLibrary::LitmusLibrary() = default;
LitmusLibrary::~LitmusLibrary() = default;

std::string LitmusLibrary::canonical(Type *T) {
  std::string s;
  raw_string_ostream os(s);
  T->print(os);
  os.flush();
  // Trim.
  size_t a = 0, b = s.size();
  while (a < b && (s[a] == ' ' || s[a] == '\t')) ++a;
  while (b > a && (s[b - 1] == ' ' || s[b - 1] == '\t')) --b;
  return s.substr(a, b - a);
}

static bool isAggregateOrVector(Type *T) {
  return T->isStructTy() || T->isArrayTy() || T->isVectorTy();
}

bool LitmusLibrary::loadDir(const fs::path &dir) {
  std::error_code ec;
  fs::directory_iterator it(dir, ec);
  if (ec) {
    std::fprintf(stderr,
                 "[litmus-library] cannot open '%s': %s\n",
                 dir.c_str(), ec.message().c_str());
    return false;
  }

  // Per-load context. After this function returns, the context is
  // released; we keep only bitcode bytes per entry.
  LLVMContext Ctx;
  // Many litmus inputs carry "invalid debug info version" warnings as
  // soft diagnostics; swallow them.
  Ctx.setDiagnosticHandlerCallBack(
      [](const DiagnosticInfo *, void *) {}, nullptr);

  std::vector<fs::path> paths;
  for (; it != fs::directory_iterator(); it.increment(ec)) {
    if (ec) break;
    const auto &entry = *it;
    if (!entry.is_regular_file()) continue;
    if (entry.path().extension() != ".ll") continue;
    paths.push_back(entry.path());
  }
  std::sort(paths.begin(), paths.end());

  for (const auto &p : paths) {
    ++files_seen_;
    SMDiagnostic Err;
    std::unique_ptr<Module> M = parseIRFile(p.string(), Err, Ctx);
    if (!M) { ++skipped_parse_; continue; }

    // Pick the (single) defined function. Litmus modules emit exactly
    // one definition by convention; if there are more, take the first
    // non-intrinsic non-declaration.
    Function *F = nullptr;
    for (Function &Fn : *M) {
      if (Fn.isDeclaration() || Fn.isIntrinsic()) continue;
      F = &Fn;
      break;
    }
    if (!F) { ++skipped_no_def_; continue; }

    Type *RetTy = F->getReturnType();
    if (RetTy->isVoidTy()) { ++skipped_void_; continue; }
    if (isAggregateOrVector(RetTy)) { ++skipped_agg_or_vec_; continue; }

    if (verifyModule(*M, &errs())) { ++skipped_parse_; continue; }

    LitmusEntry e;
    e.source_path = p.string();
    e.short_name  = p.stem().string();
    e.fn_name     = F->getName().str();
    e.return_type_str = canonical(RetTy);
    e.param_type_strs.reserve(F->arg_size());
    for (auto &A : F->args())
      e.param_type_strs.push_back(canonical(A.getType()));

    // Serialize the whole module (so intrinsic declarations and any
    // module-scope state travel with the function).
    SmallString<4096> Buf;
    {
      raw_svector_ostream OS(Buf);
      WriteBitcodeToFile(*M, OS);
    }
    e.bitcode.assign(Buf.data(), Buf.data() + Buf.size());

    size_t idx = entries_.size();
    by_return_[e.return_type_str].push_back(idx);
    entries_.push_back(std::move(e));
  }
  return true;
}

std::vector<const LitmusEntry *> LitmusLibrary::getCompatible(
    const std::string &return_ty_str,
    const std::set<std::string> &available_types) const {
  std::vector<const LitmusEntry *> out;
  auto it = by_return_.find(return_ty_str);
  if (it == by_return_.end()) return out;
  out.reserve(it->second.size());
  for (size_t i : it->second) {
    const LitmusEntry &e = entries_[i];
    bool ok = true;
    for (const std::string &p : e.param_type_strs) {
      if (!available_types.count(p)) { ok = false; break; }
    }
    if (ok) out.push_back(&e);
  }
  return out;
}

std::unique_ptr<Module> LitmusLibrary::cloneInto(
    const LitmusEntry &e, LLVMContext &target_ctx) const {
  auto Buf = MemoryBuffer::getMemBuffer(
      StringRef(reinterpret_cast<const char *>(e.bitcode.data()),
                e.bitcode.size()),
      e.short_name, /*RequiresNullTerminator=*/false);
  auto MOrErr = parseBitcodeFile(Buf->getMemBufferRef(), target_ctx);
  if (!MOrErr) {
    consumeError(MOrErr.takeError());
    return nullptr;
  }
  return std::move(*MOrErr);
}

std::vector<std::string> LitmusLibrary::returnTypeStrings() const {
  std::vector<std::string> out;
  out.reserve(by_return_.size());
  for (auto &kv : by_return_) out.push_back(kv.first);
  std::sort(out.begin(), out.end());
  return out;
}

}  // namespace spec_inject
