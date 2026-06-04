// spec-inject: in-memory index of litmus .ll files.
//
// LitmusLibrary loads every .ll file in a directory, parses each into
// its own Module inside a private LLVMContext, captures the signature
// of each defined function as a canonical string, and stores each
// module as bitcode bytes for thread-safe re-deserialization into a
// host's LLVMContext at injection time.
//
// The bitcode-bytes design (rather than holding live Modules) lets
// multiple worker threads clone litmus modules into their own host
// contexts concurrently without sharing mutable state on the
// library's own context.

#pragma once

#include "llvm/IR/Module.h"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

namespace spec_inject {

struct LitmusEntry {
  std::string source_path;             // input .ll path (for provenance)
  std::string short_name;              // sanitized basename without .ll
  std::string fn_name;                 // the function name to call
  std::string return_type_str;         // canonical Type::print
  std::vector<std::string> param_type_strs;
  std::vector<std::uint8_t> bitcode;   // self-contained module bitcode
};

class LitmusLibrary {
public:
  LitmusLibrary();
  ~LitmusLibrary();

  // Parse every *.ll file under `dir` (non-recursive). Returns true if
  // the directory was readable; per-file failures are tallied but do
  // not stop the load.
  bool loadDir(const std::filesystem::path &dir);

  // Look up litmus whose return type matches `return_ty_str` exactly
  // (canonical string) and whose every parameter type appears in
  // `available_types`. Returns pointers into entries_ (stable for the
  // library's lifetime).
  std::vector<const LitmusEntry *> getCompatible(
      const std::string &return_ty_str,
      const std::set<std::string> &available_types) const;

  // Materialize one litmus's module into a fresh LLVMContext by
  // round-tripping its bitcode. Returns nullptr on parse failure.
  std::unique_ptr<llvm::Module> cloneInto(const LitmusEntry &e,
                                          llvm::LLVMContext &target_ctx) const;

  size_t numEntries() const { return entries_.size(); }
  size_t numFilesSeen() const { return files_seen_; }
  size_t numSkippedParse() const { return skipped_parse_; }
  size_t numSkippedNoDef() const { return skipped_no_def_; }
  size_t numSkippedVoid() const { return skipped_void_; }
  size_t numSkippedAggOrVec() const { return skipped_agg_or_vec_; }

  // All canonical return-type strings present in the library.
  std::vector<std::string> returnTypeStrings() const;

private:
  // Helper: returns the canonical print of Type, with leading/trailing
  // whitespace stripped (matches the in-injector type-string convention).
  static std::string canonical(llvm::Type *T);

  std::vector<LitmusEntry> entries_;
  // index: return_type_str -> indices into entries_
  std::unordered_map<std::string, std::vector<size_t>> by_return_;
  // The library's own context exists only during load. After load we
  // hold only bitcode bytes per entry, so the context can be released
  // before injection begins.
  size_t files_seen_ = 0;
  size_t skipped_parse_ = 0;
  size_t skipped_no_def_ = 0;
  size_t skipped_void_ = 0;
  size_t skipped_agg_or_vec_ = 0;
};

}  // namespace spec_inject
