// coverage_probe.cc — single-input pass-coverage probe.
//
// Runs the same O2 pipeline as the fuzz target on one IR file, then prints the
// names of every function whose code was reached during MPM.run. Used to
// answer "which SLPVectorizer / InstCombine / GVN code paths does this seed
// actually exercise?"
//
// Sancov model: the LLVM static libraries we link against are built with
//   -fsanitize-coverage=inline-8bit-counters,pc-table,trace-cmp
// Each instrumented translation unit emits a __sancov_cntrs section (one byte
// per basic block) and a __sancov_pcs section (a {PC, flags} pair per basic
// block). The linker concatenates them into two merged sections and exposes
// __start_/__stop_ symbols. We bypass the per-module init constructors
// entirely (they get COMDAT-deduplicated unreliably in static-archive builds)
// and read the merged sections directly: zero counters before MPM.run, then
// walk the matching PC pair for every nonzero counter afterwards.
//
// Symbolization is delegated to llvm-symbolizer (default
// $LLVM_SYMBOLIZER_PATH, fallback "llvm-symbolizer"); we shell out via two
// temp files because popen is unidirectional.

#include "llvm/IR/DataLayout.h"
#include "llvm/IR/DebugInfo.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Verifier.h"
#include "llvm/IRReader/IRReader.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Passes/PassBuilder.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/SourceMgr.h"
#include "llvm/Support/TargetSelect.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Target/TargetMachine.h"
#include "llvm/Target/TargetOptions.h"
#include "llvm/TargetParser/Triple.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cerrno>
#include <csetjmp>
#include <csignal>
#include <fcntl.h>
#include <fstream>
#include <memory>
#include <optional>
#include <poll.h>
#include <set>
#include <string>
#include <sys/wait.h>
#include <unistd.h>
#include <unordered_map>
#include <vector>

// ----------------------------------------------------------------------
// Sancov section bounds. Linker synthesizes __start_/__stop_ for any section
// with a name that is a valid C identifier.
// ----------------------------------------------------------------------

extern "C" {
extern uint8_t __start___sancov_cntrs[];
extern uint8_t __stop___sancov_cntrs[];
extern uint8_t __start___sancov_pcs[];
extern uint8_t __stop___sancov_pcs[];
}

// Stubs for the sancov runtime hooks. We don't rely on the per-module
// constructors actually invoking these (most are COMDAT-discarded), but the
// surviving ones still need their callees to link.
extern "C" __attribute__((no_sanitize("coverage"))) void
__sanitizer_cov_8bit_counters_init(uint8_t *, uint8_t *) {}
extern "C" __attribute__((no_sanitize("coverage"))) void
__sanitizer_cov_pcs_init(const uintptr_t *, const uintptr_t *) {}

// ----------------------------------------------------------------------
// Ordered PC trace.
//
// The sancov build flags include trace-cmp but NOT trace-pc-guard, so we
// have no per-BB runtime callback. Instead we repurpose the trace-cmp /
// trace-switch / trace-div / trace-gep / trace-pc-indir hooks: each fires
// at its corresponding instruction, and __builtin_return_address(0) gives
// the caller's PC inside the LLVM library. Symbolizing that PC yields the
// enclosing function name. SLPVectorizer is comparison-heavy so this
// gives a dense (but cmp-biased) function transition trace.
//
// Buffer is fixed-size to keep the hot path branch-light and avoid
// allocation noise. On overflow we set a flag and stop appending; the
// partial trace is still useful for chain extraction.
// ----------------------------------------------------------------------

namespace {
// Default cap: 5M PCs * 8B = 40MB. The full O2 pipeline emits hundreds of
// thousands of cmp/switch/div/gep events even on tiny inputs (PassManager
// + DenseMap lookups dominate the early trace), so we need substantial
// headroom. Override via REGATONI_TRACE_CAP if the default isn't enough.
constexpr size_t kDefaultTraceCapacity = 5'000'000;
struct TraceState {
  uintptr_t *buf;       // heap-allocated, never freed (process exits soon)
  size_t cap;
  size_t n;
  bool overflow;
  bool enabled;
};
}  // namespace

static TraceState g_trace = {nullptr, 0, 0, false, false};

extern "C" __attribute__((no_sanitize("coverage"))) void
record_trace_pc(uintptr_t pc) {
  if (!g_trace.enabled) return;
  // Cheap consecutive-PC dedup: many cmp PCs repeat on tight loops.
  if (g_trace.n > 0 && g_trace.buf[g_trace.n - 1] == pc) return;
  if (g_trace.n >= g_trace.cap) {
    g_trace.overflow = true;
    return;
  }
  g_trace.buf[g_trace.n++] = pc;
}

#define TRACE_HOOK_BODY()                                                      \
  record_trace_pc(reinterpret_cast<uintptr_t>(__builtin_return_address(0)))

extern "C" __attribute__((no_sanitize("coverage"))) void
__sanitizer_cov_trace_cmp1(uint8_t, uint8_t) { TRACE_HOOK_BODY(); }
extern "C" __attribute__((no_sanitize("coverage"))) void
__sanitizer_cov_trace_cmp2(uint16_t, uint16_t) { TRACE_HOOK_BODY(); }
extern "C" __attribute__((no_sanitize("coverage"))) void
__sanitizer_cov_trace_cmp4(uint32_t, uint32_t) { TRACE_HOOK_BODY(); }
extern "C" __attribute__((no_sanitize("coverage"))) void
__sanitizer_cov_trace_cmp8(uint64_t, uint64_t) { TRACE_HOOK_BODY(); }
extern "C" __attribute__((no_sanitize("coverage"))) void
__sanitizer_cov_trace_const_cmp1(uint8_t, uint8_t) { TRACE_HOOK_BODY(); }
extern "C" __attribute__((no_sanitize("coverage"))) void
__sanitizer_cov_trace_const_cmp2(uint16_t, uint16_t) { TRACE_HOOK_BODY(); }
extern "C" __attribute__((no_sanitize("coverage"))) void
__sanitizer_cov_trace_const_cmp4(uint32_t, uint32_t) { TRACE_HOOK_BODY(); }
extern "C" __attribute__((no_sanitize("coverage"))) void
__sanitizer_cov_trace_const_cmp8(uint64_t, uint64_t) { TRACE_HOOK_BODY(); }
extern "C" __attribute__((no_sanitize("coverage"))) void
__sanitizer_cov_trace_switch(uint64_t, uint64_t *) { TRACE_HOOK_BODY(); }
extern "C" __attribute__((no_sanitize("coverage"))) void
__sanitizer_cov_trace_div4(uint32_t) { TRACE_HOOK_BODY(); }
extern "C" __attribute__((no_sanitize("coverage"))) void
__sanitizer_cov_trace_div8(uint64_t) { TRACE_HOOK_BODY(); }
extern "C" __attribute__((no_sanitize("coverage"))) void
__sanitizer_cov_trace_gep(uintptr_t) { TRACE_HOOK_BODY(); }
extern "C" __attribute__((no_sanitize("coverage"))) void
__sanitizer_cov_trace_pc_indir(uintptr_t) { TRACE_HOOK_BODY(); }

#undef TRACE_HOOK_BODY

// ----------------------------------------------------------------------
// Coverage harvesting.
// ----------------------------------------------------------------------

namespace {

// PC table layout: each entry is a pair {PC, flags}, both pointer-sized.
constexpr size_t kPCEntrySize = 2 * sizeof(uintptr_t);

void resetCounters() {
  std::memset(__start___sancov_cntrs, 0,
              __stop___sancov_cntrs - __start___sancov_cntrs);
}

std::vector<uintptr_t> harvestPCs() {
  std::vector<uintptr_t> hits;
  size_t nc = __stop___sancov_cntrs - __start___sancov_cntrs;
  size_t np = (__stop___sancov_pcs - __start___sancov_pcs) / kPCEntrySize;
  size_t n = std::min(nc, np);
  const uintptr_t *pcs =
      reinterpret_cast<const uintptr_t *>(__start___sancov_pcs);
  for (size_t i = 0; i < n; ++i) {
    if (__start___sancov_cntrs[i] != 0)
      hits.push_back(pcs[2 * i]);
  }
  return hits;
}

std::string selfPath() {
  char buf[4096];
  ssize_t n = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
  if (n <= 0) return {};
  buf[n] = 0;
  return std::string(buf);
}

// PIE binaries are loaded at a randomized base. llvm-symbolizer --obj wants
// static (in-file) VAs, so we subtract the load base. Find it by walking
// /proc/self/maps for the first executable mapping of our own path.
uintptr_t selfLoadBase(const std::string &self) {
  std::FILE *f = std::fopen("/proc/self/maps", "r");
  if (!f) return 0;
  char line[8192];
  uintptr_t base = 0;
  while (std::fgets(line, sizeof(line), f)) {
    // format: "addr-addr perms offset dev inode  pathname"
    unsigned long lo, hi;
    char perms[8];
    unsigned long off;
    int devmaj, devmin;
    unsigned long inode;
    int pathOff = 0;
    if (std::sscanf(line, "%lx-%lx %7s %lx %x:%x %lu %n", &lo, &hi, perms,
                    &off, &devmaj, &devmin, &inode, &pathOff) >= 7) {
      const char *path = line + pathOff;
      // strip trailing newline
      size_t plen = std::strlen(path);
      while (plen > 0 && (path[plen - 1] == '\n' || path[plen - 1] == '\r'))
        plen--;
      if (plen == self.size() &&
          std::memcmp(path, self.data(), plen) == 0 && off == 0) {
        base = lo;
        break;
      }
    }
  }
  std::fclose(f);
  return base;
}

struct SymInfo {
  std::string name;  // demangled function name; "" on lookup failure
  std::string file;  // source file (raw "file:line[:col]"); "" if unknown
};

// Returns parallel list of (name, file) pairs. Calls llvm-symbolizer once
// with all addresses on stdin; output is one record per address, each
// record is "func\nfile:line[:col]\n\n" (with --no-inlines). Both fields
// are empty strings on lookup failure ("??").
std::vector<SymInfo> symbolize(const std::vector<uintptr_t> &pcs,
                               const std::string &binary) {
  std::vector<SymInfo> out(pcs.size());
  if (pcs.empty()) return out;

  const char *envSym = std::getenv("LLVM_SYMBOLIZER_PATH");
  std::string symPath = (envSym && *envSym) ? envSym : "llvm-symbolizer";

  char inTmpl[] = "/tmp/coverage_probe_in.XXXXXX";
  int infd = mkstemp(inTmpl);
  if (infd < 0) return out;
  for (uintptr_t pc : pcs) {
    char line[64];
    int len = std::snprintf(line, sizeof(line), "0x%lx\n",
                            static_cast<unsigned long>(pc));
    if (write(infd, line, len) != len) {
      close(infd);
      unlink(inTmpl);
      return out;
    }
  }
  close(infd);

  char outTmpl[] = "/tmp/coverage_probe_out.XXXXXX";
  int outfd = mkstemp(outTmpl);
  if (outfd < 0) {
    unlink(inTmpl);
    return out;
  }
  close(outfd);

  // --functions=linkage gives the fully-qualified demangled name (e.g.
  // "llvm::SLPVectorizerPass::tryToVectorize(...)") so substring filters
  // like "SLPVectorizer" or "SLPVectorizerPass::run" work as expected.
  std::string cmd = "'" + symPath + "' --obj='" + binary +
                    "' --functions=linkage --no-inlines --demangle < '" +
                    inTmpl + "' > '" + outTmpl + "' 2>/dev/null";
  int rc = std::system(cmd.c_str());
  (void)rc;

  std::FILE *f = std::fopen(outTmpl, "r");
  if (!f) {
    unlink(inTmpl);
    unlink(outTmpl);
    return out;
  }
  std::vector<SymInfo> records;
  SymInfo current;
  // 0 = expecting function name, 1 = expecting file:line, 2 = swallow rest
  int phase = 0;
  char buf[4096];
  while (std::fgets(buf, sizeof(buf), f)) {
    std::string line(buf);
    while (!line.empty() && (line.back() == '\n' || line.back() == '\r'))
      line.pop_back();
    if (line.empty()) {
      records.push_back(std::move(current));
      current = SymInfo{};
      phase = 0;
    } else if (phase == 0) {
      current.name = line;
      phase = 1;
    } else if (phase == 1) {
      current.file = line;
      phase = 2;
    }
    // ignore further lines until blank delimiter
  }
  if (phase != 0) records.push_back(std::move(current));
  std::fclose(f);
  unlink(inTmpl);
  unlink(outTmpl);

  for (size_t i = 0; i < pcs.size() && i < records.size(); ++i) {
    if (records[i].name != "??")
      out[i].name = records[i].name;
    // llvm-symbolizer emits "??:?" or "??:0:0" when it has no source info.
    if (records[i].file.compare(0, 2, "??") != 0)
      out[i].file = records[i].file;
  }
  return out;
}

// Batch-mode symbolizer cache: PC → SymInfo, populated lazily. An entry
// with empty name/file means "we asked the symbolizer and got '??'" — still
// a valid cached answer, so we don't re-ask.
std::vector<SymInfo>
symbolizeCached(const std::vector<uintptr_t> &pcs, const std::string &binary,
                std::unordered_map<uintptr_t, SymInfo> &cache) {
  std::vector<uintptr_t> unseen;
  unseen.reserve(pcs.size());
  for (uintptr_t pc : pcs) {
    if (cache.find(pc) == cache.end()) unseen.push_back(pc);
  }
  std::sort(unseen.begin(), unseen.end());
  unseen.erase(std::unique(unseen.begin(), unseen.end()), unseen.end());
  if (!unseen.empty()) {
    std::vector<SymInfo> fresh = symbolize(unseen, binary);
    for (size_t i = 0; i < unseen.size(); ++i) {
      cache.emplace(unseen[i], fresh[i]);
    }
  }
  std::vector<SymInfo> out;
  out.reserve(pcs.size());
  for (uintptr_t pc : pcs) {
    auto it = cache.find(pc);
    out.push_back(it == cache.end() ? SymInfo{} : it->second);
  }
  return out;
}

// ----------------------------------------------------------------------
// CLI.
// ----------------------------------------------------------------------

enum class OutputMode {
  ReachedSet,     // default: unique function names hit (existing behavior)
  CallChains,     // unique k-chains from ordered transition sequence
  CallSequence,   // dedup'd function transition sequence, one fn per line
  Batch,          // --batch <list>: one line per file, "<path>\t<fn,fn,...>"
  BatchCheck,     // --batch-check <list> --function=<n>: emit hit paths
};

struct ProbeOptions {
  std::string input_path;
  std::string filter;          // empty = no function-name filter
  std::string filter_source;   // empty = no source-file filter
  OutputMode mode = OutputMode::ReachedSet;
  size_t k = 3;        // chain length for --call-chains
  std::string batch_list;       // path to file-list for --batch / --batch-check
  std::string check_function;   // --function=<name> for --batch-check
  bool batch_chains = false;    // --call-chains while in --batch
};

[[noreturn]] void usage(int code) {
  std::fprintf(
      stderr,
      "Usage: coverage_probe <input.ll> [--filter=<substring>]\n"
      "                                 [--filter-by-source=<substring>]\n"
      "                                 [--call-chains | --call-sequence]\n"
      "                                 [--k=<N>]\n"
      "       coverage_probe --batch <filelist> [--filter=<substring>]\n"
      "                                        [--filter-by-source=<substring>]\n"
      "       coverage_probe --batch-check <filelist> --function=<name>\n"
      "                                 [--filter=<substring>]\n"
      "                                 [--filter-by-source=<substring>]\n"
      "\n"
      "  Default: prints unique function names reached during the O2\n"
      "  pipeline (set form, deduped). With --call-chains or\n"
      "  --call-sequence, captures an ordered PC trace via the sancov\n"
      "  trace-cmp/switch/div/gep/pc_indir hooks and reports either:\n"
      "    --call-chains    unique sliding-window chains of length k\n"
      "                     (default k=3), one chain per line as\n"
      "                     'fn1 -> fn2 -> fn3'\n"
      "    --call-sequence  the dedup'd function transition sequence,\n"
      "                     one fn per line\n"
      "  --k=<N>            chain length (default 3, only with --call-chains)\n"
      "  --filter=<sub>     restrict to function names containing the\n"
      "                     substring (applied before chain extraction)\n"
      "  --filter-by-source=<sub>\n"
      "                     restrict to PCs whose symbolized source file\n"
      "                     path contains the substring (e.g.\n"
      "                     'SLPVectorizer' matches SLPVectorizer.cpp/.h)\n"
      "\n"
      "  Batch modes amortize one-time setup (target init, TargetMachine,\n"
      "  llvm-symbolizer, PC->name cache) across many files:\n"
      "    --batch <list>          one line per file:\n"
      "                            '<path>\\t<fn,fn,...>' or '<path>\\tERROR'\n"
      "    --batch-check <list> --function=<name>\n"
      "                            emit only paths where the named function\n"
      "                            (substring match) was reached\n"
      "\n"
      "  Trace buffer is capped at 100K PCs; on overflow a warning is\n"
      "  printed to stderr and the partial trace is used.\n"
      "\n"
      "  llvm-symbolizer is invoked from $LLVM_SYMBOLIZER_PATH (or PATH).\n"
      "\n"
      "Exit codes: 0 success | 1 IR parse failure | 2 usage error\n");
  std::exit(code);
}

// Helper: pull the value for a flag of either form `--name=value` or
// `--name value`. Advances `i` past a consumed space-form value.
static bool takeFlagValue(const std::string &arg, const char *flag,
                          int &i, int argc, char **argv,
                          std::string &out) {
  size_t flen = std::strlen(flag);
  if (arg.size() > flen && arg.compare(0, flen, flag) == 0 &&
      arg[flen] == '=') {
    out = arg.substr(flen + 1);
    return true;
  }
  if (arg.size() == flen && arg.compare(0, flen, flag) == 0) {
    if (i + 1 >= argc) {
      std::fprintf(stderr, "coverage_probe: %s requires a value\n", flag);
      usage(2);
    }
    out = argv[++i];
    return true;
  }
  return false;
}

ProbeOptions parseArgs(int argc, char **argv) {
  ProbeOptions opts;
  bool sawCallChains = false;
  bool sawCallSequence = false;
  bool sawBatch = false;
  bool sawBatchCheck = false;
  for (int i = 1; i < argc; ++i) {
    std::string a = argv[i];
    if (a == "-h" || a == "--help") usage(2);
    static const char kFilter[] = "--filter=";
    static const char kFilterSrc[] = "--filter-by-source=";
    static const char kK[] = "--k=";
    if (a.rfind(kFilterSrc, 0) == 0) {
      opts.filter_source = a.substr(sizeof(kFilterSrc) - 1);
      continue;
    }
    if (a.rfind(kFilter, 0) == 0) {
      opts.filter = a.substr(sizeof(kFilter) - 1);
      continue;
    }
    if (a == "--call-chains") {
      sawCallChains = true;
      continue;
    }
    if (a == "--call-sequence") {
      sawCallSequence = true;
      continue;
    }
    if (a.rfind(kK, 0) == 0) {
      opts.k = static_cast<size_t>(
          std::strtoul(a.substr(sizeof(kK) - 1).c_str(), nullptr, 10));
      continue;
    }
    {
      std::string val;
      if (takeFlagValue(a, "--batch", i, argc, argv, val)) {
        opts.batch_list = val;
        sawBatch = true;
        continue;
      }
      if (takeFlagValue(a, "--batch-check", i, argc, argv, val)) {
        opts.batch_list = val;
        sawBatchCheck = true;
        continue;
      }
      if (takeFlagValue(a, "--function", i, argc, argv, val)) {
        opts.check_function = val;
        continue;
      }
    }
    if (!a.empty() && a[0] == '-') {
      std::fprintf(stderr, "coverage_probe: unknown flag '%s'\n", a.c_str());
      usage(2);
    }
    if (!opts.input_path.empty()) {
      std::fprintf(stderr,
                   "coverage_probe: extra positional argument '%s'\n",
                   a.c_str());
      usage(2);
    }
    opts.input_path = a;
  }
  if (sawBatch && sawBatchCheck) {
    std::fprintf(stderr,
                 "coverage_probe: --batch and --batch-check are mutually "
                 "exclusive\n");
    usage(2);
  }
  if (sawBatch || sawBatchCheck) {
    if (!opts.input_path.empty()) {
      std::fprintf(stderr,
                   "coverage_probe: positional input.ll is not allowed in "
                   "batch mode\n");
      usage(2);
    }
    if (sawCallSequence) {
      std::fprintf(stderr,
                   "coverage_probe: --call-sequence is not supported in "
                   "batch mode (it's order-sensitive and hard to aggregate)\n");
      usage(2);
    }
    if (sawBatchCheck && sawCallChains) {
      std::fprintf(stderr,
                   "coverage_probe: --call-chains and --batch-check are "
                   "mutually exclusive\n");
      usage(2);
    }
    opts.mode = sawBatch ? OutputMode::Batch : OutputMode::BatchCheck;
    // --call-chains in batch mode triggers per-file chain extraction; the
    // probe's mode stays Batch but a flag tells the child to use the trace
    // path instead of the reached-set path.
    if (sawCallChains) {
      // Reuse opts.k; flag is implicit via opts.k semantics + a marker.
      // Simpler: store as a bool by reusing CallChains as a sub-mode signal.
      opts.batch_chains = true;
    }
    if (sawBatchCheck && opts.check_function.empty()) {
      std::fprintf(stderr,
                   "coverage_probe: --batch-check requires --function=<n>\n");
      usage(2);
    }
    if (opts.k < 1) {
      std::fprintf(stderr, "coverage_probe: --k must be >= 1\n");
      usage(2);
    }
    return opts;
  }
  if (opts.input_path.empty()) usage(2);
  if (sawCallChains && sawCallSequence) {
    std::fprintf(stderr,
                 "coverage_probe: --call-chains and --call-sequence are "
                 "mutually exclusive\n");
    usage(2);
  }
  if (sawCallChains) opts.mode = OutputMode::CallChains;
  else if (sawCallSequence) opts.mode = OutputMode::CallSequence;
  if (opts.k < 1) {
    std::fprintf(stderr, "coverage_probe: --k must be >= 1\n");
    usage(2);
  }
  return opts;
}

}  // namespace

// ----------------------------------------------------------------------
// Pipeline (mirrors src/harness/opt_fuzz_target.cc).
// ----------------------------------------------------------------------

static constexpr const char *kDefaultTripleStr = "x86_64-unknown-linux-gnu";

static std::unique_ptr<llvm::TargetMachine>
buildTM(const llvm::Triple &T) {
  std::string Err;
  const llvm::Target *TheTarget = llvm::TargetRegistry::lookupTarget(T, Err);
  if (!TheTarget) return nullptr;
  llvm::TargetOptions Opts;
  return std::unique_ptr<llvm::TargetMachine>(TheTarget->createTargetMachine(
      T, /*CPU=*/"", /*Features=*/"", Opts,
      /*RM=*/std::nullopt, /*CM=*/std::nullopt,
      llvm::CodeGenOptLevel::Default));
}

// ----------------------------------------------------------------------
// Batch supervision.
//
// LLVM's report_fatal_error path calls exit(1) by default, and a buggy
// pass + exotic IR can SIGABRT/SIGSEGV. Either kills the whole process,
// which is unacceptable when scanning thousands of corpus files. We isolate
// the per-file pipeline run in a forked child and let the parent supervise:
//
//   parent ── path ──▶ child  (long-running, processes many files in-process
//                              with full PC->name cache reuse)
//   parent ◀── result ── child
//
// A 5-second poll timeout per file lets the parent detect runaway or hung
// children and restart them. Output framing on the result pipe is a 4-byte
// little-endian length followed by `length` bytes of UTF-8 (the formatted
// per-file output, no filepath prefix).
// ----------------------------------------------------------------------

namespace batch_ipc {

constexpr int kPerFileTimeoutMs = 5000;

// Special length sentinel meaning "no result, file errored cleanly". This
// is distinct from a zero-byte success (e.g. batch-check miss).
constexpr uint32_t kErrorSentinel = 0xFFFFFFFEu;

ssize_t fullRead(int fd, void *buf, size_t n) {
  size_t got = 0;
  while (got < n) {
    ssize_t r = ::read(fd, static_cast<char *>(buf) + got, n - got);
    if (r == 0) return static_cast<ssize_t>(got);
    if (r < 0) {
      if (errno == EINTR) continue;
      return -1;
    }
    got += static_cast<size_t>(r);
  }
  return static_cast<ssize_t>(got);
}

bool fullWrite(int fd, const void *buf, size_t n) {
  size_t put = 0;
  while (put < n) {
    ssize_t r = ::write(fd, static_cast<const char *>(buf) + put, n - put);
    if (r < 0) {
      if (errno == EINTR) continue;
      return false;
    }
    put += static_cast<size_t>(r);
  }
  return true;
}

bool writeFrame(int fd, uint32_t lenOrSentinel, const std::string &body) {
  if (!fullWrite(fd, &lenOrSentinel, sizeof(lenOrSentinel))) return false;
  if (lenOrSentinel == kErrorSentinel) return true;
  if (lenOrSentinel == 0) return true;
  return fullWrite(fd, body.data(), body.size());
}

enum class FrameStatus { Ok, Error, Timeout, ChildGone };

FrameStatus readFrame(int fd, std::string &out, int timeoutMs) {
  // Wait for the first byte with a poll-based timeout. Once data starts
  // arriving we read the rest unconditionally; the child writes the whole
  // frame in one go and the kernel buffer is a few MB so blocking after
  // the header is safe in practice.
  struct pollfd p = {fd, POLLIN, 0};
  int pr;
  do {
    pr = ::poll(&p, 1, timeoutMs);
  } while (pr < 0 && errno == EINTR);
  if (pr == 0) return FrameStatus::Timeout;
  if (pr < 0) return FrameStatus::ChildGone;
  uint32_t lenOrSentinel = 0;
  ssize_t r = fullRead(fd, &lenOrSentinel, sizeof(lenOrSentinel));
  if (r == 0) return FrameStatus::ChildGone;
  if (r < 0 || r != static_cast<ssize_t>(sizeof(lenOrSentinel)))
    return FrameStatus::ChildGone;
  if (lenOrSentinel == kErrorSentinel) {
    out.clear();
    return FrameStatus::Error;
  }
  out.resize(lenOrSentinel);
  if (lenOrSentinel > 0) {
    ssize_t br = fullRead(fd, out.data(), lenOrSentinel);
    if (br != static_cast<ssize_t>(lenOrSentinel)) return FrameStatus::ChildGone;
  }
  return FrameStatus::Ok;
}

}  // namespace batch_ipc

// ----------------------------------------------------------------------
// Per-file processing (runs in the child).
// ----------------------------------------------------------------------

// Returns true on success and fills `result`; returns false on parse failure
// (caller should send the ERROR sentinel). On success, `result` is the
// formatted body to send to the parent (without the filepath prefix). For
// chain mode this is multi-line (one chain per line); for reached-set this
// is a single line with comma-joined names; for batch-check this is "HIT"
// or empty.
static bool processOneFile(const std::string &path, const ProbeOptions &opts,
                           llvm::LLVMContext &Ctx,
                           llvm::TargetMachine *DefaultTM,
                           const std::string &self, uintptr_t base,
                           std::unordered_map<uintptr_t, SymInfo> &pcCache,
                           std::string &result) {
  llvm::SMDiagnostic LErr;
  auto M = llvm::parseIRFile(path, LErr, Ctx);
  if (!M) return false;
  llvm::StripDebugInfo(*M);

  std::unique_ptr<llvm::TargetMachine> PerFileTMOwned;
  llvm::TargetMachine *TM = DefaultTM;
  const llvm::Triple &MT = M->getTargetTriple();
  if (!MT.empty() &&
      (DefaultTM == nullptr ||
       MT.str() != DefaultTM->getTargetTriple().str())) {
    PerFileTMOwned = buildTM(MT);
    if (PerFileTMOwned) TM = PerFileTMOwned.get();
  }

  llvm::LoopAnalysisManager LAM;
  llvm::FunctionAnalysisManager FAM;
  llvm::CGSCCAnalysisManager CGAM;
  llvm::ModuleAnalysisManager MAM;
  llvm::PipelineTuningOptions PTO;
  PTO.LoopVectorization = true;
  PTO.SLPVectorization = true;
  llvm::PassBuilder PB(TM, PTO);
  PB.registerModuleAnalyses(MAM);
  PB.registerCGSCCAnalyses(CGAM);
  PB.registerFunctionAnalyses(FAM);
  PB.registerLoopAnalyses(LAM);
  PB.crossRegisterProxies(LAM, FAM, CGAM, MAM);
  llvm::ModulePassManager MPM =
      PB.buildPerModuleDefaultPipeline(llvm::OptimizationLevel::O2);

  resetCounters();
  g_trace.n = 0;
  g_trace.overflow = false;
  g_trace.enabled = opts.batch_chains;
  MPM.run(*M, MAM);
  g_trace.enabled = false;

  if (opts.batch_chains) {
    // Build ordered fnSeq from the trace buffer, then emit unique k-chains
    // one per line. Mirrors single-file --call-chains.
    std::vector<uintptr_t> traceVA(g_trace.buf, g_trace.buf + g_trace.n);
    if (base != 0) for (uintptr_t &pc : traceVA) pc -= base;
    std::vector<uintptr_t> uniquePCs(traceVA.begin(), traceVA.end());
    std::sort(uniquePCs.begin(), uniquePCs.end());
    uniquePCs.erase(std::unique(uniquePCs.begin(), uniquePCs.end()),
                    uniquePCs.end());
    std::vector<SymInfo> uniqueInfos =
        symbolizeCached(uniquePCs, self, pcCache);
    std::unordered_map<uintptr_t, SymInfo> pcToInfo;
    pcToInfo.reserve(uniquePCs.size() * 2);
    for (size_t i = 0; i < uniquePCs.size(); ++i)
      pcToInfo.emplace(uniquePCs[i], uniqueInfos[i]);

    std::vector<std::string> fnSeq;
    fnSeq.reserve(traceVA.size() / 32 + 16);
    for (uintptr_t pc : traceVA) {
      auto it = pcToInfo.find(pc);
      if (it == pcToInfo.end() || it->second.name.empty()) continue;
      const std::string &nm = it->second.name;
      if (!opts.filter.empty() && nm.find(opts.filter) == std::string::npos)
        continue;
      if (!opts.filter_source.empty() &&
          it->second.file.find(opts.filter_source) == std::string::npos)
        continue;
      if (!fnSeq.empty() && fnSeq.back() == nm) continue;
      fnSeq.push_back(nm);
    }

    result.clear();
    if (fnSeq.size() < opts.k) return true;
    std::set<std::string> chains;
    for (size_t i = 0; i + opts.k <= fnSeq.size(); ++i) {
      std::string chain;
      for (size_t j = 0; j < opts.k; ++j) {
        if (j) chain += " -> ";
        chain += fnSeq[i + j];
      }
      chains.insert(std::move(chain));
    }
    for (const std::string &c : chains) {
      if (!result.empty()) result.push_back('\n');
      result += c;
    }
    return true;
  }

  // Reached-set / batch-check share counter-derived hits.
  std::vector<uintptr_t> pcs = harvestPCs();
  if (base != 0) for (uintptr_t &pc : pcs) pc -= base;
  std::vector<SymInfo> infos = symbolizeCached(pcs, self, pcCache);
  std::set<std::string> uniq;
  for (const SymInfo &info : infos) {
    if (info.name.empty()) continue;
    if (!opts.filter.empty() &&
        info.name.find(opts.filter) == std::string::npos)
      continue;
    if (!opts.filter_source.empty() &&
        info.file.find(opts.filter_source) == std::string::npos)
      continue;
    uniq.insert(info.name);
  }

  if (opts.mode == OutputMode::BatchCheck) {
    bool hit = false;
    for (const std::string &n : uniq) {
      if (n.find(opts.check_function) != std::string::npos) {
        hit = true;
        break;
      }
    }
    result = hit ? "HIT" : "";
    return true;
  }

  // OutputMode::Batch (reached-set form): newline-joined function names.
  // emitResult will split this back out and emit one '<path>\t<fn>' line per
  // function. We use '\n' as the body separator (rather than e.g. comma)
  // because demangled C++ names commonly contain commas in template args.
  result.clear();
  for (const std::string &n : uniq) {
    if (!result.empty()) result.push_back('\n');
    result += n;
  }
  return true;
}

// Child entry point. Reads newline-terminated paths from `in_fd`, runs each
// through processOneFile, writes a framed result to `out_fd`. Exits when
// `in_fd` reaches EOF.
[[noreturn]] static void
runBatchChild(int in_fd, int out_fd, const ProbeOptions &opts,
              llvm::TargetMachine *DefaultTM, const std::string &self,
              uintptr_t base) {
  // The child uses its own LLVMContext so cleanup at process exit is moot
  // and we get fresh context state per generation (a previous generation
  // may have died mid-pipeline and left global data in odd shape — fork
  // gives us a clean baseline anyway).
  llvm::LLVMContext Ctx;
  std::unordered_map<uintptr_t, SymInfo> pcCache;

  // Buffered line reader on in_fd.
  std::FILE *in = ::fdopen(in_fd, "r");
  if (!in) _exit(3);

  char *line = nullptr;
  size_t cap = 0;
  while (true) {
    ssize_t got = ::getline(&line, &cap, in);
    if (got < 0) break;
    std::string path(line, line + got);
    while (!path.empty() && (path.back() == '\n' || path.back() == '\r'))
      path.pop_back();
    if (path.empty()) {
      // Skip blank lines but still send an empty success frame so the
      // parent's per-file accounting stays in lockstep with input lines.
      // Actually — the parent only sends non-blank paths, so we shouldn't
      // see blank lines here. Defensive: skip without sending anything.
      continue;
    }
    std::string body;
    bool ok = processOneFile(path, opts, Ctx, DefaultTM, self, base,
                             pcCache, body);
    if (!ok) {
      batch_ipc::writeFrame(out_fd, batch_ipc::kErrorSentinel, "");
    } else {
      batch_ipc::writeFrame(out_fd, static_cast<uint32_t>(body.size()), body);
    }
  }
  std::free(line);
  _exit(0);
}

// ----------------------------------------------------------------------
// Parent supervisor.
// ----------------------------------------------------------------------

namespace {

struct ChildProc {
  pid_t pid = -1;
  int in_fd = -1;   // parent writes paths here
  int out_fd = -1;  // parent reads framed results from here
};

void closeChild(ChildProc &c) {
  if (c.in_fd >= 0) ::close(c.in_fd);
  if (c.out_fd >= 0) ::close(c.out_fd);
  c.in_fd = c.out_fd = -1;
  if (c.pid > 0) {
    int status = 0;
    ::waitpid(c.pid, &status, 0);
    c.pid = -1;
  }
}

void killChild(ChildProc &c) {
  if (c.pid > 0) {
    ::kill(c.pid, SIGKILL);
  }
  closeChild(c);
}

ChildProc spawnChild(const ProbeOptions &opts, llvm::TargetMachine *DefaultTM,
                     const std::string &self, uintptr_t base) {
  ChildProc c;
  int inPipe[2], outPipe[2];
  if (::pipe(inPipe) != 0 || ::pipe(outPipe) != 0) return c;
  pid_t pid = ::fork();
  if (pid < 0) {
    ::close(inPipe[0]); ::close(inPipe[1]);
    ::close(outPipe[0]); ::close(outPipe[1]);
    return c;
  }
  if (pid == 0) {
    // Child: read from inPipe[0], write framed results to outPipe[1].
    ::close(inPipe[1]);
    ::close(outPipe[0]);
    runBatchChild(inPipe[0], outPipe[1], opts, DefaultTM, self, base);
    // unreachable
    _exit(99);
  }
  // Parent.
  ::close(inPipe[0]);
  ::close(outPipe[1]);
  c.pid = pid;
  c.in_fd = inPipe[1];
  c.out_fd = outPipe[0];
  return c;
}

void emitResult(const ProbeOptions &opts, const std::string &path,
                const std::string &body) {
  if (opts.mode == OutputMode::BatchCheck) {
    if (body == "HIT") {
      std::printf("%s\n", path.c_str());
      std::fflush(stdout);
    }
    return;
  }
  // Both --call-chains and reached-set batch modes use newline-separated
  // bodies (chains: one chain per line; reached-set: one fn per line). We
  // splat them into '<path>\t<line>' rows here. For reached-set with an
  // empty body (file ran but no hits passed the filter) we still emit a
  // single '<path>\t' row so consumers can see the file was processed.
  if (body.empty()) {
    if (!opts.batch_chains) {
      std::printf("%s\t\n", path.c_str());
      std::fflush(stdout);
    }
    return;
  }
  size_t start = 0;
  for (size_t i = 0; i <= body.size(); ++i) {
    if (i == body.size() || body[i] == '\n') {
      if (i > start) {
        std::printf("%s\t%.*s\n", path.c_str(),
                    static_cast<int>(i - start), body.data() + start);
      }
      start = i + 1;
    }
  }
  std::fflush(stdout);
}

}  // namespace

static int runBatchSupervised(const ProbeOptions &opts,
                              llvm::TargetMachine *DefaultTM,
                              const std::string &self, uintptr_t base) {
  std::ifstream listf(opts.batch_list);
  if (!listf) {
    std::fprintf(stderr, "coverage_probe: cannot open '%s'\n",
                 opts.batch_list.c_str());
    return 1;
  }
  std::vector<std::string> paths;
  {
    std::string line;
    while (std::getline(listf, line)) {
      while (!line.empty() && (line.back() == '\r' || line.back() == '\n'))
        line.pop_back();
      if (!line.empty() && line[0] != '#') paths.push_back(line);
    }
  }

  // SIGPIPE would kill the parent when writing to a dead child's pipe;
  // we'd rather see EPIPE and recover.
  std::signal(SIGPIPE, SIG_IGN);

  ChildProc child;
  size_t i = 0;
  while (i < paths.size()) {
    if (child.pid <= 0) {
      child = spawnChild(opts, DefaultTM, self, base);
      if (child.pid <= 0) {
        std::fprintf(stderr, "coverage_probe: fork failed\n");
        return 1;
      }
    }

    // Send next path.
    std::string msg = paths[i] + "\n";
    if (!batch_ipc::fullWrite(child.in_fd, msg.data(), msg.size())) {
      // Child died before we could even send it work.
      killChild(child);
      std::printf("%s\tCRASH\n", paths[i].c_str());
      std::fflush(stdout);
      ++i;
      continue;
    }

    std::string body;
    auto status = batch_ipc::readFrame(child.out_fd, body,
                                        batch_ipc::kPerFileTimeoutMs);
    switch (status) {
      case batch_ipc::FrameStatus::Ok:
        emitResult(opts, paths[i], body);
        break;
      case batch_ipc::FrameStatus::Error:
        if (opts.mode == OutputMode::Batch) {
          std::printf("%s\tERROR\n", paths[i].c_str());
          std::fflush(stdout);
        }
        // batch-check: silent on error (mirrors miss).
        break;
      case batch_ipc::FrameStatus::Timeout:
        killChild(child);
        std::printf("%s\tTIMEOUT\n", paths[i].c_str());
        std::fflush(stdout);
        break;
      case batch_ipc::FrameStatus::ChildGone:
        killChild(child);
        std::printf("%s\tCRASH\n", paths[i].c_str());
        std::fflush(stdout);
        break;
    }
    ++i;
  }

  if (child.in_fd >= 0) ::close(child.in_fd);
  child.in_fd = -1;
  closeChild(child);
  return 0;
}

int main(int argc, char **argv) {
  ProbeOptions opts = parseArgs(argc, argv);

  // Allocate ordered-PC trace buffer once (chain modes only need it, but
  // it's cheap to allocate even if unused — heap pages stay reserved
  // unless the trace hooks actually write to them).
  {
    size_t cap = kDefaultTraceCapacity;
    if (const char *env = std::getenv("REGATONI_TRACE_CAP")) {
      char *end = nullptr;
      unsigned long v = std::strtoul(env, &end, 10);
      if (end && *end == 0 && v > 0) cap = static_cast<size_t>(v);
    }
    g_trace.buf = new uintptr_t[cap];
    g_trace.cap = cap;
  }

  llvm::InitializeAllTargets();
  llvm::InitializeAllTargetMCs();
  llvm::InitializeAllAsmPrinters();
  llvm::InitializeAllAsmParsers();

  // One-time setup shared across all input files (batch mode amortises
  // these over N files; single-file mode pays for them once).
  std::string self = selfPath();
  if (self.empty()) {
    std::fprintf(stderr,
                 "coverage_probe: cannot determine self path "
                 "(/proc/self/exe)\n");
    return 1;
  }
  uintptr_t base = selfLoadBase(self);

  std::unique_ptr<llvm::TargetMachine> DefaultTMOwned =
      buildTM(llvm::Triple(kDefaultTripleStr));
  llvm::TargetMachine *DefaultTM = DefaultTMOwned.get();

  llvm::LLVMContext Ctx;

  // -------- Batch / batch-check: fork-supervised long-running child -----
  if (opts.mode == OutputMode::Batch ||
      opts.mode == OutputMode::BatchCheck) {
    return runBatchSupervised(opts, DefaultTM, self, base);
  }

  // -------- Single-file flow (existing modes) ---------------------------
  llvm::SMDiagnostic Err;
  auto M = llvm::parseIRFile(opts.input_path, Err, Ctx);
  if (!M) {
    Err.print("coverage_probe", llvm::errs());
    return 1;
  }
  llvm::StripDebugInfo(*M);

  // Match resolveTargetMachine() in opt_fuzz_target.cc — module triple
  // first, default fallback otherwise.
  std::unique_ptr<llvm::TargetMachine> TMOwned;
  const llvm::Triple &MT = M->getTargetTriple();
  if (!MT.empty()) TMOwned = buildTM(MT);
  llvm::TargetMachine *TM = TMOwned ? TMOwned.get() : DefaultTM;

  llvm::LoopAnalysisManager LAM;
  llvm::FunctionAnalysisManager FAM;
  llvm::CGSCCAnalysisManager CGAM;
  llvm::ModuleAnalysisManager MAM;

  llvm::PipelineTuningOptions PTO;
  PTO.LoopVectorization = true;
  PTO.SLPVectorization = true;
  llvm::PassBuilder PB(TM, PTO);
  PB.registerModuleAnalyses(MAM);
  PB.registerCGSCCAnalyses(CGAM);
  PB.registerFunctionAnalyses(FAM);
  PB.registerLoopAnalyses(LAM);
  PB.crossRegisterProxies(LAM, FAM, CGAM, MAM);

  llvm::ModulePassManager MPM =
      PB.buildPerModuleDefaultPipeline(llvm::OptimizationLevel::O2);

  // Isolate pipeline coverage: zero counters and trace, run, harvest.
  resetCounters();
  g_trace.n = 0;
  g_trace.overflow = false;
  g_trace.enabled = (opts.mode != OutputMode::ReachedSet);
  MPM.run(*M, MAM);
  g_trace.enabled = false;

  // -------- Default mode: counter-derived reached set (unchanged) --------
  if (opts.mode == OutputMode::ReachedSet) {
    std::vector<uintptr_t> pcs = harvestPCs();
    if (base != 0) {
      for (uintptr_t &pc : pcs) pc -= base;
    }
    std::vector<SymInfo> infos = symbolize(pcs, self);
    std::set<std::string> uniq;
    for (const SymInfo &info : infos) {
      if (info.name.empty()) continue;
      if (!opts.filter.empty() &&
          info.name.find(opts.filter) == std::string::npos)
        continue;
      if (!opts.filter_source.empty() &&
          info.file.find(opts.filter_source) == std::string::npos)
        continue;
      uniq.insert(info.name);
    }
    for (const std::string &n : uniq) std::printf("%s\n", n.c_str());
    return 0;
  }

  // -------- Chain / sequence mode: ordered trace from trace-cmp hooks ----
  if (g_trace.overflow) {
    std::fprintf(stderr,
                 "coverage_probe: warning: trace buffer overflowed at %zu "
                 "PCs (cap=%zu); using partial trace. Override with "
                 "REGATONI_TRACE_CAP=<n>.\n",
                 g_trace.n, g_trace.cap);
  }

  // Symbolize unique PCs only (the trace has heavy repetition even after
  // consecutive-PC dedup, so unique-set is much smaller than n).
  std::vector<uintptr_t> traceVA(g_trace.buf, g_trace.buf + g_trace.n);
  if (base != 0) {
    for (uintptr_t &pc : traceVA) pc -= base;
  }
  std::vector<uintptr_t> uniquePCs(traceVA.begin(), traceVA.end());
  std::sort(uniquePCs.begin(), uniquePCs.end());
  uniquePCs.erase(std::unique(uniquePCs.begin(), uniquePCs.end()),
                  uniquePCs.end());
  std::vector<SymInfo> uniqueInfos = symbolize(uniquePCs, self);
  std::unordered_map<uintptr_t, SymInfo> pcToInfo;
  pcToInfo.reserve(uniquePCs.size() * 2);
  for (size_t i = 0; i < uniquePCs.size(); ++i) {
    pcToInfo.emplace(uniquePCs[i], uniqueInfos[i]);
  }

  // Build function transition sequence with consecutive-name dedup.
  std::vector<std::string> fnSeq;
  fnSeq.reserve(traceVA.size() / 32 + 16);
  for (uintptr_t pc : traceVA) {
    auto it = pcToInfo.find(pc);
    if (it == pcToInfo.end() || it->second.name.empty()) continue;
    const std::string &nm = it->second.name;
    if (!opts.filter.empty() && nm.find(opts.filter) == std::string::npos)
      continue;
    if (!opts.filter_source.empty() &&
        it->second.file.find(opts.filter_source) == std::string::npos)
      continue;
    if (!fnSeq.empty() && fnSeq.back() == nm) continue;
    fnSeq.push_back(nm);
  }

  if (opts.mode == OutputMode::CallSequence) {
    for (const std::string &n : fnSeq) std::printf("%s\n", n.c_str());
    return 0;
  }

  // CallChains: emit unique sliding windows of length k.
  if (fnSeq.size() < opts.k) return 0;
  std::set<std::string> chains;
  for (size_t i = 0; i + opts.k <= fnSeq.size(); ++i) {
    std::string chain;
    for (size_t j = 0; j < opts.k; ++j) {
      if (j) chain += " -> ";
      chain += fnSeq[i + j];
    }
    chains.insert(std::move(chain));
  }
  for (const std::string &c : chains) std::printf("%s\n", c.c_str());
  return 0;
}
