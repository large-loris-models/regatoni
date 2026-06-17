// src/mutators/corpus_index.h
#ifndef REGATONI_MUTATORS_CORPUS_INDEX_H
#define REGATONI_MUTATORS_CORPUS_INDEX_H

#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"

#include <filesystem>
#include <memory>
#include <random>
#include <string>
#include <unordered_map>
#include <vector>

namespace regatoni {

class CorpusIndex {
public:
  static CorpusIndex &instance();

  // Build from a corpus directory. Idempotent — calling again with the
  // same directory is a no-op. Calls llvm::parseIRFile on each .ll file
  // into a private LLVMContext.
  void build(const std::filesystem::path &corpus_dir);

  // Pick a function from the library matching the signature, or nullptr.
  // The returned Function* lives in the library context — caller is
  // responsible for cloning into the destination context.
  llvm::Function *sample_matching(llvm::FunctionType *sig,
                                  std::mt19937 &rng) const;

  // For canApply: cheap predicate.
  bool has_matching(llvm::FunctionType *sig) const;

  // Sample any indexed function regardless of signature (for gadget-graft
  // crossover, which transplants a value sub-DAG rather than a whole call).
  // Lives in the library context — caller must clone into the destination.
  llvm::Function *sample_any(std::mt19937 &rng) const;

  llvm::LLVMContext &context() { return ctx_; }

  size_t num_modules() const { return num_modules_; }
  size_t num_functions() const { return num_functions_; }
  size_t num_signatures() const { return functions_by_sig_.size(); }

private:
  CorpusIndex() = default;
  llvm::LLVMContext ctx_;
  std::vector<std::unique_ptr<llvm::Module>> library_modules_;
  // Key: FunctionType::print into a SmallString. Types are interned per-context
  // so pointer equality won't work across modules.
  std::unordered_map<std::string, std::vector<llvm::Function *>>
      functions_by_sig_;
  size_t num_modules_ = 0;
  size_t num_functions_ = 0;
  bool built_ = false;
  std::filesystem::path built_from_;
};

} // namespace regatoni

#endif
