// spec-mutate — specification-driven IR rewriter.
//
// Reads a JSON spec (--spec rewrites.json) describing (match, transform)
// pairs and applies each rewrite to every matching location in every .ll
// under --input-dir. Each match produces one cloned, verified variant
// written to --output-dir.
//
// The engine is fixed; new rewrite types are added by editing the spec.

#include "deps/nlohmann-json/json.hpp"

#include "src/spec-mutate/match_engine.h"
#include "src/spec-mutate/spec.h"
#include "src/spec-mutate/transform_engine.h"

#include "llvm/IR/Function.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Verifier.h"
#include "llvm/IRReader/IRReader.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/SourceMgr.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Transforms/Utils/Cloning.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <random>
#include <set>
#include <sstream>
#include <string>
#include <thread>
#include <unordered_set>
#include <vector>

namespace fs = std::filesystem;
using nlohmann::json;
using namespace spec_mutate;

namespace {

// ----------------------------------------------------------------------
// Stats.
// ----------------------------------------------------------------------

struct Stats {
  size_t files_seen = 0;
  size_t files_parsed = 0;
  size_t files_failed_parse = 0;
  size_t fns_seen = 0;
  size_t variants_attempted = 0;
  size_t variants_written = 0;
  size_t variants_verify_failed = 0;
  size_t variants_skipped_noop = 0;
  size_t variants_io_failed = 0;
  std::map<std::string, unsigned long> by_category;
  std::map<std::string, unsigned long> by_rewrite;
  std::map<std::string, unsigned long> verify_fail_by_rewrite;
};

static void mergeStats(Stats &dst, const Stats &src) {
  dst.files_seen             += src.files_seen;
  dst.files_parsed           += src.files_parsed;
  dst.files_failed_parse     += src.files_failed_parse;
  dst.fns_seen               += src.fns_seen;
  dst.variants_attempted     += src.variants_attempted;
  dst.variants_written       += src.variants_written;
  dst.variants_verify_failed += src.variants_verify_failed;
  dst.variants_skipped_noop  += src.variants_skipped_noop;
  dst.variants_io_failed     += src.variants_io_failed;
  for (auto &kv : src.by_category)            dst.by_category[kv.first]            += kv.second;
  for (auto &kv : src.by_rewrite)             dst.by_rewrite[kv.first]             += kv.second;
  for (auto &kv : src.verify_fail_by_rewrite) dst.verify_fail_by_rewrite[kv.first] += kv.second;
}

// ----------------------------------------------------------------------
// File / name utilities.
// ----------------------------------------------------------------------

static std::string sanitize(std::string s) {
  for (char &c : s) {
    if (!(std::isalnum(static_cast<unsigned char>(c)) ||
          c == '_' || c == '-' || c == '.')) {
      c = '_';
    }
  }
  if (s.empty()) s = "anon";
  return s;
}

// Cap filenames to a safe ext4 limit, stamping a hash so trimmed names
// remain unique per source.
static std::string clampName(const std::string &name, size_t limit = 240) {
  if (name.size() <= limit) return name;
  uint64_t h = 0xcbf29ce484222325ull;
  for (unsigned char c : name) { h ^= c; h *= 0x100000001b3ull; }
  char tag[10];
  std::snprintf(tag, sizeof(tag), "%08x", static_cast<unsigned>(h));
  auto sep = name.rfind("__");
  if (sep == std::string::npos) {
    return name.substr(0, limit - 9) + "_" + tag;
  }
  std::string head = name.substr(0, sep);
  std::string tail = name.substr(sep);
  size_t head_budget = limit - tail.size() - 9;
  if (head.size() > head_budget) head = head.substr(0, head_budget);
  return head + "_" + tag + tail;
}

static std::string writeModule(const llvm::Module &M,
                               const fs::path &out_path) {
  std::error_code ec;
  llvm::raw_fd_ostream os(out_path.string(), ec);
  if (ec) return ec.message();
  M.print(os, /*AAW=*/nullptr);
  return std::string();
}

static bool verifyOk(llvm::Module &M) {
  std::string s;
  llvm::raw_string_ostream os(s);
  return !llvm::verifyModule(M, &os);
}

// ----------------------------------------------------------------------
// Per-file processing.
// ----------------------------------------------------------------------

static void processFile(const std::string &path,
                        const std::string &out_dir,
                        const Spec &spec,
                        const std::unordered_set<std::string> &cat_filter,
                        bool include_all_cats,
                        Stats &stats) {
  using namespace llvm;
  ++stats.files_seen;

  std::ifstream in(path, std::ios::binary);
  if (!in) { ++stats.files_failed_parse; return; }
  std::string text((std::istreambuf_iterator<char>(in)),
                   std::istreambuf_iterator<char>());
  if (text.empty()) { ++stats.files_failed_parse; return; }

  LLVMContext Ctx;
  SMDiagnostic Err;
  auto Buf = MemoryBuffer::getMemBuffer(text, path);
  auto M = parseIR(Buf->getMemBufferRef(), Err, Ctx);
  if (!M) { ++stats.files_failed_parse; return; }
  ++stats.files_parsed;

  fs::path in_path(path);
  std::string stem = in_path.stem().string();

  for (Function &F : *M) {
    if (F.isDeclaration()) continue;
    ++stats.fns_seen;
    std::string fn_san = sanitize(F.getName().str());
    std::string base = stem + "__" + fn_san;

    for (const Rewrite &R : spec.rewrites) {
      if (!include_all_cats && !cat_filter.count(R.category)) continue;
      auto matches = findMatches(F, R);
      unsigned counter = 0;
      for (const MatchLocation &loc : matches) {
        ++stats.variants_attempted;
        ValueToValueMapTy VMap;
        std::unique_ptr<Module> Clone = CloneModule(*M, VMap);
        if (!Clone) { ++stats.variants_skipped_noop; continue; }

        if (!applyTransform(*Clone, VMap, F, loc, R.transform)) {
          ++stats.variants_skipped_noop;
          continue;
        }
        if (!verifyOk(*Clone)) {
          ++stats.variants_verify_failed;
          ++stats.verify_fail_by_rewrite[R.id];
          continue;
        }
        std::ostringstream suffix;
        suffix << "__rw_" << R.id << "_" << counter++;
        std::string name = base + suffix.str() + ".ll";
        fs::path out_path = fs::path(out_dir) / clampName(name);
        std::string io_err = writeModule(*Clone, out_path);
        if (!io_err.empty()) { ++stats.variants_io_failed; continue; }
        ++stats.variants_written;
        if (!R.category.empty()) ++stats.by_category[R.category];
        ++stats.by_rewrite[R.id];
      }
    }
  }
}

// ----------------------------------------------------------------------
// Enumeration.
// ----------------------------------------------------------------------

static void collectLLFiles(const fs::path &root,
                           std::vector<std::string> &out) {
  std::error_code ec;
  for (fs::recursive_directory_iterator
         it(root, fs::directory_options::skip_permission_denied, ec), end;
       it != end; it.increment(ec)) {
    if (ec) { ec.clear(); continue; }
    if (!it->is_regular_file(ec)) { ec.clear(); continue; }
    const fs::path &p = it->path();
    if (p.extension() == ".ll") out.push_back(p.string());
  }
}

static void readFileList(const std::string &path,
                         std::vector<std::string> &out) {
  std::ifstream in(path);
  if (!in) {
    std::fprintf(stderr, "spec-mutate: cannot open %s\n", path.c_str());
    std::exit(1);
  }
  std::string line;
  while (std::getline(in, line)) {
    while (!line.empty() && (line.back() == '\r' || line.back() == ' ' ||
                              line.back() == '\t'))
      line.pop_back();
    if (line.empty() || line[0] == '#') continue;
    out.push_back(line);
  }
}

// ----------------------------------------------------------------------
// CLI.
// ----------------------------------------------------------------------

struct Opts {
  std::string spec_path;
  std::string input_dir;
  std::string output_dir;
  std::string file_list;
  std::string summary_output;
  std::string categories = "all";
  unsigned threads = 0;
  uint64_t seed = 42;
  size_t max_files = 0;
  bool quiet = false;
};

static void usage() {
  std::fprintf(stderr,
    "Usage: spec-mutate --spec PATH --input-dir DIR --output-dir DIR\n"
    "                    [--file-list FILE] [--categories CSV]\n"
    "                    [--summary-output PATH] [--threads N]\n"
    "                    [--seed N] [--max-files N] [--quiet]\n"
    "\n"
    "  --categories      comma-separated subset of {flag,attribute,metadata,\n"
    "                    freeze,constant} or 'all' (default)\n"
    "  --max-files N     process only the first N files (after sort)\n");
}

static bool parseOpts(int argc, char **argv, Opts &o) {
  for (int i = 1; i < argc; ++i) {
    std::string a = argv[i];
    auto next = [&](const char *flag) -> std::string {
      if (i + 1 >= argc) {
        std::fprintf(stderr, "%s: missing argument\n", flag);
        std::exit(2);
      }
      return argv[++i];
    };
    if      (a == "--spec")           o.spec_path      = next("--spec");
    else if (a == "--input-dir")      o.input_dir      = next("--input-dir");
    else if (a == "--output-dir")     o.output_dir     = next("--output-dir");
    else if (a == "--file-list")      o.file_list      = next("--file-list");
    else if (a == "--summary-output") o.summary_output = next("--summary-output");
    else if (a == "--categories")     o.categories     = next("--categories");
    else if (a == "--threads")        o.threads        = std::stoi(next("--threads"));
    else if (a == "--seed")           o.seed           = std::stoull(next("--seed"));
    else if (a == "--max-files")      o.max_files      = std::stoull(next("--max-files"));
    else if (a == "--quiet")          o.quiet          = true;
    else if (a == "-h" || a == "--help") { usage(); std::exit(0); }
    else {
      std::fprintf(stderr, "unknown flag: %s\n", a.c_str());
      usage();
      return false;
    }
  }
  if (o.spec_path.empty())  { std::fprintf(stderr, "--spec is required\n");       return false; }
  if (o.output_dir.empty()) { std::fprintf(stderr, "--output-dir is required\n"); return false; }
  if (o.input_dir.empty() && o.file_list.empty()) {
    std::fprintf(stderr, "--input-dir or --file-list is required\n");
    return false;
  }
  return true;
}

static std::unordered_set<std::string> parseCategorySet(const std::string &csv,
                                                        bool &all) {
  std::unordered_set<std::string> out;
  all = false;
  if (csv == "all" || csv.empty()) { all = true; return out; }
  size_t i = 0;
  while (i < csv.size()) {
    size_t j = csv.find(',', i);
    if (j == std::string::npos) j = csv.size();
    std::string tok = csv.substr(i, j - i);
    while (!tok.empty() && tok.back() == ' ') tok.pop_back();
    while (!tok.empty() && tok.front() == ' ') tok.erase(tok.begin());
    if (!tok.empty()) out.insert(tok);
    i = j + 1;
  }
  return out;
}

}  // namespace

int main(int argc, char **argv) {
  Opts opts;
  if (!parseOpts(argc, argv, opts)) return 2;

  Spec spec;
  std::string err;
  if (!loadSpec(opts.spec_path, spec, err)) {
    std::fprintf(stderr, "spec-mutate: %s\n", err.c_str());
    return 1;
  }
  if (!opts.quiet)
    std::fprintf(stderr, "[spec-mutate] %zu rewrites loaded from %s\n",
                 spec.rewrites.size(), opts.spec_path.c_str());

  bool include_all = false;
  auto cat_set = parseCategorySet(opts.categories, include_all);

  std::vector<std::string> paths;
  if (!opts.input_dir.empty()) collectLLFiles(opts.input_dir, paths);
  else                         readFileList(opts.file_list, paths);
  std::sort(paths.begin(), paths.end());
  if (opts.max_files && paths.size() > opts.max_files)
    paths.resize(opts.max_files);
  if (!opts.quiet)
    std::fprintf(stderr, "[spec-mutate] %zu input files\n", paths.size());

  std::error_code ec;
  fs::create_directories(opts.output_dir, ec);
  if (ec) {
    std::fprintf(stderr, "cannot create %s: %s\n",
                 opts.output_dir.c_str(), ec.message().c_str());
    return 1;
  }

  unsigned T = opts.threads;
  if (T == 0) T = std::max(1u, std::thread::hardware_concurrency());
  if (T > paths.size()) T = std::max<unsigned>(1, paths.size());

  std::vector<Stats> per_thread(T);
  std::atomic<size_t> progress{0};
  std::atomic<bool> done{false};
  std::thread prog_thread;
  if (!opts.quiet && paths.size() > 500) {
    prog_thread = std::thread([&]() {
      while (!done.load(std::memory_order_relaxed)) {
        std::this_thread::sleep_for(std::chrono::seconds(10));
        size_t p = progress.load(std::memory_order_relaxed);
        std::fprintf(stderr, "[spec-mutate] %zu / %zu files\n",
                     p, paths.size());
      }
    });
  }

  auto t0 = std::chrono::steady_clock::now();
  size_t chunk = (paths.size() + T - 1) / T;
  std::vector<std::thread> workers;
  workers.reserve(T);
  for (unsigned t = 0; t < T; ++t) {
    size_t b = std::min(paths.size(), size_t(t) * chunk);
    size_t e = std::min(paths.size(), b + chunk);
    workers.emplace_back([&, b, e, t]() {
      for (size_t i = b; i < e; ++i) {
        processFile(paths[i], opts.output_dir, spec,
                    cat_set, include_all, per_thread[t]);
        progress.fetch_add(1, std::memory_order_relaxed);
      }
    });
  }
  for (auto &w : workers) w.join();
  done.store(true);
  if (prog_thread.joinable()) prog_thread.join();
  auto t1 = std::chrono::steady_clock::now();

  Stats total;
  for (auto &s : per_thread) mergeStats(total, s);

  if (!opts.quiet) {
    std::fprintf(stderr,
      "[spec-mutate] %zu files / %zu functions / %zu variants written "
      "(verify_fail=%zu noop=%zu io_fail=%zu) in %.1fs\n",
      total.files_seen, total.fns_seen, total.variants_written,
      total.variants_verify_failed, total.variants_skipped_noop,
      total.variants_io_failed,
      std::chrono::duration<double>(t1 - t0).count());
  }

  // Summary JSON.
  json summary;
  summary["spec_path"]              = opts.spec_path;
  summary["input_dir"]              = opts.input_dir;
  summary["output_dir"]             = opts.output_dir;
  summary["categories"]             = opts.categories;
  summary["seed"]                   = opts.seed;
  summary["files_seen"]             = total.files_seen;
  summary["files_parsed"]           = total.files_parsed;
  summary["files_failed_parse"]     = total.files_failed_parse;
  summary["functions"]              = total.fns_seen;
  summary["variants_attempted"]     = total.variants_attempted;
  summary["variants_written"]       = total.variants_written;
  summary["variants_verify_failed"] = total.variants_verify_failed;
  summary["variants_skipped_noop"]  = total.variants_skipped_noop;
  summary["variants_io_failed"]     = total.variants_io_failed;
  summary["by_category"]            = total.by_category;
  summary["by_rewrite"]             = total.by_rewrite;
  summary["verify_fail_by_rewrite"] = total.verify_fail_by_rewrite;

  std::string sjson = summary.dump(2);
  std::printf("%s\n", sjson.c_str());
  if (!opts.summary_output.empty()) {
    std::ofstream of(opts.summary_output);
    if (of) of << sjson << "\n";
  }
  return 0;
}
