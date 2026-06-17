// src/mutators/corpus_index.cc
#include "src/mutators/corpus_index.h"

#include "llvm/ADT/SmallString.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DiagnosticInfo.h"
#include "llvm/IR/GlobalValue.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IRReader/IRReader.h"
#include "llvm/Support/SourceMgr.h"
#include "llvm/Support/raw_ostream.h"

#include <cstdio>
#include <fstream>
#include <system_error>

namespace regatoni {

CorpusIndex &CorpusIndex::instance() {
  static CorpusIndex inst;
  return inst;
}

// Recursively check whether V is, or transitively references via
// ConstantExpr / ConstantAggregate operands, any GlobalValue other than Self.
static bool containsExternalGlobal(const llvm::Value *V,
                                   const llvm::Function *Self) {
  if (auto *GV = llvm::dyn_cast<llvm::GlobalValue>(V))
    return GV != Self;
  if (auto *C = llvm::dyn_cast<llvm::Constant>(V)) {
    for (const llvm::Use &U : C->operands())
      if (containsExternalGlobal(U.get(), Self))
        return true;
  }
  return false;
}

static bool isSelfContained(const llvm::Function &F) {
  for (const auto &BB : F)
    for (const auto &I : BB)
      for (const llvm::Use &U : I.operands())
        if (containsExternalGlobal(U.get(), &F))
          return false;
  return true;
}

static std::string canonicalSigKey(llvm::FunctionType *FT) {
  llvm::SmallString<128> Buf;
  llvm::raw_svector_ostream OS(Buf);
  FT->print(OS);
  return std::string(Buf.str());
}

static size_t readVmHwmKb() {
  std::ifstream in("/proc/self/status");
  std::string line;
  while (std::getline(in, line)) {
    if (line.compare(0, 6, "VmHWM:") == 0) {
      size_t pos = 6;
      while (pos < line.size() && (line[pos] == ' ' || line[pos] == '\t'))
        ++pos;
      size_t end = pos;
      while (end < line.size() && line[end] >= '0' && line[end] <= '9')
        ++end;
      if (end > pos)
        return std::stoul(line.substr(pos, end - pos));
    }
  }
  return 0;
}

void CorpusIndex::build(const std::filesystem::path &corpus_dir) {
  if (built_ && built_from_ == corpus_dir)
    return;

  size_t parsed = 0;
  size_t failed = 0;

  // Many seeds carry stale debug-info metadata that triggers per-file
  // "invalid debug info version" warnings on parse. Hard parse errors come
  // back via the SMDiagnostic return path, not this handler, so swallowing
  // diagnostics here only drops the noise.
  ctx_.setDiagnosticHandlerCallBack([](const llvm::DiagnosticInfo *, void *) {},
                                    nullptr);

  std::error_code ec;
  std::filesystem::directory_iterator it(corpus_dir, ec);
  if (ec) {
    fprintf(stderr,
            "[corpus_index] failed to open corpus dir '%s': %s\n",
            corpus_dir.c_str(), ec.message().c_str());
    built_ = true;
    built_from_ = corpus_dir;
    return;
  }

  for (; it != std::filesystem::directory_iterator(); it.increment(ec)) {
    if (ec)
      break;
    const auto &entry = *it;
    if (!entry.is_regular_file())
      continue;
    if (entry.path().extension() != ".ll")
      continue;

    llvm::SMDiagnostic err;
    std::unique_ptr<llvm::Module> M =
        llvm::parseIRFile(entry.path().string(), err, ctx_);
    if (!M) {
      ++failed;
      continue;
    }

    for (llvm::Function &F : *M) {
      if (F.isDeclaration() || F.isIntrinsic())
        continue;
      if (!isSelfContained(F))
        continue;
      std::string key = canonicalSigKey(F.getFunctionType());
      functions_by_sig_[std::move(key)].push_back(&F);
      ++num_functions_;
    }

    library_modules_.push_back(std::move(M));
    ++parsed;
  }

  num_modules_ = parsed;
  built_ = true;
  built_from_ = corpus_dir;

  size_t rss_kb = readVmHwmKb();
  size_t rss_mib = rss_kb / 1024;

  fprintf(stderr,
          "[corpus_index] indexed %zu modules, %zu self-contained functions, "
          "%zu distinct signatures, %zu MiB peak RSS\n",
          num_modules_, num_functions_, functions_by_sig_.size(), rss_mib);
  if (failed) {
    fprintf(stderr, "[corpus_index] %zu files failed to parse\n", failed);
  }
}

bool CorpusIndex::has_matching(llvm::FunctionType *sig) const {
  std::string key = canonicalSigKey(sig);
  auto it = functions_by_sig_.find(key);
  return it != functions_by_sig_.end() && !it->second.empty();
}

llvm::Function *CorpusIndex::sample_matching(llvm::FunctionType *sig,
                                             std::mt19937 &rng) const {
  std::string key = canonicalSigKey(sig);
  auto it = functions_by_sig_.find(key);
  if (it == functions_by_sig_.end() || it->second.empty())
    return nullptr;
  std::uniform_int_distribution<size_t> pick(0, it->second.size() - 1);
  return it->second[pick(rng)];
}

llvm::Function *CorpusIndex::sample_any(std::mt19937 &rng) const {
  if (functions_by_sig_.empty())
    return nullptr;
  // Pick a random signature bucket, then a random function within it.
  std::uniform_int_distribution<size_t> pickBucket(0,
                                                   functions_by_sig_.size() - 1);
  auto it = functions_by_sig_.begin();
  std::advance(it, pickBucket(rng));
  if (it->second.empty())
    return nullptr;
  std::uniform_int_distribution<size_t> pickFn(0, it->second.size() - 1);
  return it->second[pickFn(rng)];
}

} // namespace regatoni
