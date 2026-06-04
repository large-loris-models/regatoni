// spec-inject — inject litmus function calls into split_seed hosts.
//
// Loads a directory of single-function litmus .ll files into a
// LitmusLibrary. For each host .ll under --host-dir, parses the
// module, walks its defined functions, and for up to N compatible
// litmus tests creates a clone with the litmus linked in and a call
// spliced into the host.
//
// Output: one .ll per (host_function, litmus) pair under --output-dir,
// named `<host_stem>__inject_<litmus_name>.ll`.

#include "deps/nlohmann-json/json.hpp"

#include "src/spec-inject/injector.h"
#include "src/spec-inject/litmus_library.h"

#include "llvm/Support/raw_ostream.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <random>
#include <string>
#include <thread>
#include <vector>

namespace fs = std::filesystem;
using nlohmann::json;
using namespace spec_inject;

namespace {

struct Opts {
  std::string litmus_dir;
  std::string host_dir;
  std::string output_dir;
  std::string summary_output;
  std::string file_list;
  size_t max_injections_per_fn = 5;
  uint64_t seed = 42;
  unsigned threads = 0;
  size_t max_files = 0;
  bool quiet = false;
};

void usage() {
  std::fprintf(stderr,
    "Usage: spec-inject --litmus-dir DIR --host-dir DIR --output-dir DIR\n"
    "                   [--file-list FILE] [--max-injections-per-function N]\n"
    "                   [--seed N] [--threads N] [--max-files N]\n"
    "                   [--summary-output PATH] [--quiet]\n");
}

bool parseOpts(int argc, char **argv, Opts &o) {
  for (int i = 1; i < argc; ++i) {
    std::string a = argv[i];
    auto next = [&](const char *flag) -> std::string {
      if (i + 1 >= argc) {
        std::fprintf(stderr, "%s: missing argument\n", flag);
        std::exit(2);
      }
      return argv[++i];
    };
    if      (a == "--litmus-dir")                 o.litmus_dir       = next("--litmus-dir");
    else if (a == "--host-dir")                   o.host_dir         = next("--host-dir");
    else if (a == "--output-dir")                 o.output_dir       = next("--output-dir");
    else if (a == "--summary-output")             o.summary_output   = next("--summary-output");
    else if (a == "--file-list")                  o.file_list        = next("--file-list");
    else if (a == "--max-injections-per-function")
      o.max_injections_per_fn = std::stoul(next("--max-injections-per-function"));
    else if (a == "--seed")                       o.seed             = std::stoull(next("--seed"));
    else if (a == "--threads")                    o.threads          = std::stoi(next("--threads"));
    else if (a == "--max-files")                  o.max_files        = std::stoull(next("--max-files"));
    else if (a == "--quiet")                      o.quiet            = true;
    else if (a == "-h" || a == "--help")          { usage(); std::exit(0); }
    else {
      std::fprintf(stderr, "unknown flag: %s\n", a.c_str());
      usage();
      return false;
    }
  }
  if (o.litmus_dir.empty()) { std::fprintf(stderr, "--litmus-dir is required\n"); return false; }
  if (o.output_dir.empty()) { std::fprintf(stderr, "--output-dir is required\n"); return false; }
  if (o.host_dir.empty() && o.file_list.empty()) {
    std::fprintf(stderr, "--host-dir or --file-list is required\n");
    return false;
  }
  return true;
}

void collectLLFiles(const fs::path &root, std::vector<std::string> &out) {
  std::error_code ec;
  for (fs::recursive_directory_iterator
         it(root, fs::directory_options::skip_permission_denied, ec), end;
       it != end; it.increment(ec)) {
    if (ec) { ec.clear(); continue; }
    if (!it->is_regular_file(ec)) { ec.clear(); continue; }
    if (it->path().extension() == ".ll") out.push_back(it->path().string());
  }
}

void readFileList(const std::string &path, std::vector<std::string> &out) {
  std::ifstream in(path);
  if (!in) {
    std::fprintf(stderr, "spec-inject: cannot open %s\n", path.c_str());
    std::exit(1);
  }
  std::string line;
  while (std::getline(in, line)) {
    while (!line.empty() && (line.back() == '\r' || line.back() == ' ' || line.back() == '\t'))
      line.pop_back();
    if (line.empty() || line[0] == '#') continue;
    out.push_back(line);
  }
}

}  // namespace

int main(int argc, char **argv) {
  Opts opts;
  if (!parseOpts(argc, argv, opts)) return 2;

  // Load litmus library.
  LitmusLibrary lib;
  if (!lib.loadDir(opts.litmus_dir)) {
    std::fprintf(stderr, "spec-inject: failed to load litmus dir\n");
    return 1;
  }
  if (!opts.quiet) {
    std::fprintf(stderr,
      "[spec-inject] litmus: %zu files seen, %zu indexed "
      "(parse_fail=%zu no_def=%zu void=%zu agg_or_vec=%zu)\n",
      lib.numFilesSeen(), lib.numEntries(), lib.numSkippedParse(),
      lib.numSkippedNoDef(), lib.numSkippedVoid(), lib.numSkippedAggOrVec());
  }
  if (lib.numEntries() == 0) {
    std::fprintf(stderr, "spec-inject: no usable litmus tests\n");
    return 1;
  }

  // Enumerate hosts.
  std::vector<std::string> hosts;
  if (!opts.host_dir.empty()) collectLLFiles(opts.host_dir, hosts);
  else                        readFileList(opts.file_list, hosts);
  std::sort(hosts.begin(), hosts.end());
  if (opts.max_files && hosts.size() > opts.max_files)
    hosts.resize(opts.max_files);
  if (!opts.quiet)
    std::fprintf(stderr, "[spec-inject] %zu host files\n", hosts.size());

  std::error_code ec;
  fs::create_directories(opts.output_dir, ec);
  if (ec) {
    std::fprintf(stderr, "cannot create %s: %s\n",
                 opts.output_dir.c_str(), ec.message().c_str());
    return 1;
  }

  unsigned T = opts.threads;
  if (T == 0) T = std::max(1u, std::thread::hardware_concurrency());
  if (T > hosts.size()) T = std::max<unsigned>(1, hosts.size());

  std::vector<InjectorStats> per_thread(T);
  std::atomic<size_t> progress{0};
  std::atomic<bool> done{false};
  std::thread prog_thread;
  if (!opts.quiet && hosts.size() > 500) {
    prog_thread = std::thread([&]() {
      while (!done.load(std::memory_order_relaxed)) {
        std::this_thread::sleep_for(std::chrono::seconds(10));
        size_t p = progress.load(std::memory_order_relaxed);
        std::fprintf(stderr, "[spec-inject] %zu / %zu files\n", p, hosts.size());
      }
    });
  }

  InjectorOpts iopts;
  iopts.max_injections_per_fn = opts.max_injections_per_fn;
  iopts.seed = opts.seed;

  auto t0 = std::chrono::steady_clock::now();
  size_t chunk = (hosts.size() + T - 1) / T;
  std::vector<std::thread> workers;
  workers.reserve(T);
  for (unsigned t = 0; t < T; ++t) {
    size_t b = std::min(hosts.size(), size_t(t) * chunk);
    size_t e = std::min(hosts.size(), b + chunk);
    workers.emplace_back([&, b, e, t]() {
      Injector inj(lib, iopts);
      std::mt19937 rng(opts.seed + t * 0x9e3779b97f4a7c15ull);
      for (size_t i = b; i < e; ++i) {
        inj.processFile(hosts[i], opts.output_dir, rng, per_thread[t]);
        progress.fetch_add(1, std::memory_order_relaxed);
      }
    });
  }
  for (auto &w : workers) w.join();
  done.store(true);
  if (prog_thread.joinable()) prog_thread.join();
  auto t1 = std::chrono::steady_clock::now();

  InjectorStats total;
  for (auto &s : per_thread) mergeStats(total, s);

  if (!opts.quiet) {
    std::fprintf(stderr,
      "[spec-inject] %zu hosts / %zu functions / %zu attempts / "
      "%zu written (verify_fail=%zu link_fail=%zu no_compat=%zu "
      "no_anchor=%zu no_use=%zu) in %.1fs\n",
      total.hosts_seen, total.fns_seen, total.attempts, total.written,
      total.verify_failed, total.link_failed, total.skipped_no_compat,
      total.skipped_no_anchor, total.skipped_no_use,
      std::chrono::duration<double>(t1 - t0).count());
  }

  json summary;
  summary["litmus_dir"]               = opts.litmus_dir;
  summary["host_dir"]                 = opts.host_dir;
  summary["output_dir"]               = opts.output_dir;
  summary["max_injections_per_function"] = opts.max_injections_per_fn;
  summary["seed"]                     = opts.seed;
  summary["litmus_indexed"]           = lib.numEntries();
  summary["hosts_seen"]               = total.hosts_seen;
  summary["hosts_parsed"]             = total.hosts_parsed;
  summary["hosts_parse_failed"]       = total.hosts_parse_failed;
  summary["functions"]                = total.fns_seen;
  summary["functions_no_values"]      = total.fns_skipped_no_values;
  summary["attempts"]                 = total.attempts;
  summary["written"]                  = total.written;
  summary["skipped_no_compat"]        = total.skipped_no_compat;
  summary["skipped_no_anchor"]        = total.skipped_no_anchor;
  summary["skipped_no_use"]           = total.skipped_no_use;
  summary["link_failed"]              = total.link_failed;
  summary["verify_failed"]            = total.verify_failed;
  summary["io_failed"]                = total.io_failed;
  summary["written_by_litmus"]        = total.written_by_litmus;
  summary["verify_fail_by_litmus"]    = total.verify_fail_by_litmus;

  std::string sjson = summary.dump(2);
  std::printf("%s\n", sjson.c_str());
  if (!opts.summary_output.empty()) {
    std::ofstream of(opts.summary_output);
    if (of) of << sjson << "\n";
  }
  return 0;
}
