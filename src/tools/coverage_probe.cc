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
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <unistd.h>
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

extern "C" void __sanitizer_cov_trace_cmp1(uint8_t, uint8_t) {}
extern "C" void __sanitizer_cov_trace_cmp2(uint16_t, uint16_t) {}
extern "C" void __sanitizer_cov_trace_cmp4(uint32_t, uint32_t) {}
extern "C" void __sanitizer_cov_trace_cmp8(uint64_t, uint64_t) {}
extern "C" void __sanitizer_cov_trace_const_cmp1(uint8_t, uint8_t) {}
extern "C" void __sanitizer_cov_trace_const_cmp2(uint16_t, uint16_t) {}
extern "C" void __sanitizer_cov_trace_const_cmp4(uint32_t, uint32_t) {}
extern "C" void __sanitizer_cov_trace_const_cmp8(uint64_t, uint64_t) {}
extern "C" void __sanitizer_cov_trace_switch(uint64_t, uint64_t *) {}
extern "C" void __sanitizer_cov_trace_div4(uint32_t) {}
extern "C" void __sanitizer_cov_trace_div8(uint64_t) {}
extern "C" void __sanitizer_cov_trace_gep(uintptr_t) {}
extern "C" void __sanitizer_cov_trace_pc_indir(uintptr_t) {}

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

// Returns parallel list of function names ("" on lookup failure). Calls
// llvm-symbolizer once with all addresses on stdin; output is one record
// per address, each record is "func\nfile:line[:col]\n\n" (with
// --no-inlines).
std::vector<std::string> symbolize(const std::vector<uintptr_t> &pcs,
                                   const std::string &binary) {
  std::vector<std::string> names(pcs.size());
  if (pcs.empty()) return names;

  const char *envSym = std::getenv("LLVM_SYMBOLIZER_PATH");
  std::string symPath = (envSym && *envSym) ? envSym : "llvm-symbolizer";

  char inTmpl[] = "/tmp/coverage_probe_in.XXXXXX";
  int infd = mkstemp(inTmpl);
  if (infd < 0) return names;
  for (uintptr_t pc : pcs) {
    char line[64];
    int len = std::snprintf(line, sizeof(line), "0x%lx\n",
                            static_cast<unsigned long>(pc));
    if (write(infd, line, len) != len) {
      close(infd);
      unlink(inTmpl);
      return names;
    }
  }
  close(infd);

  char outTmpl[] = "/tmp/coverage_probe_out.XXXXXX";
  int outfd = mkstemp(outTmpl);
  if (outfd < 0) {
    unlink(inTmpl);
    return names;
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
    return names;
  }
  std::vector<std::string> records;
  std::string current;
  bool haveFn = false;
  char buf[4096];
  while (std::fgets(buf, sizeof(buf), f)) {
    std::string line(buf);
    while (!line.empty() && (line.back() == '\n' || line.back() == '\r'))
      line.pop_back();
    if (line.empty()) {
      records.push_back(current);
      current.clear();
      haveFn = false;
    } else if (!haveFn) {
      current = line;  // first non-empty line of record = function name
      haveFn = true;
    }
    // ignore subsequent lines of the same record (file:line)
  }
  if (haveFn) records.push_back(current);
  std::fclose(f);
  unlink(inTmpl);
  unlink(outTmpl);

  for (size_t i = 0; i < pcs.size() && i < records.size(); ++i) {
    if (records[i] != "??")
      names[i] = records[i];
  }
  return names;
}

// ----------------------------------------------------------------------
// CLI.
// ----------------------------------------------------------------------

struct ProbeOptions {
  std::string input_path;
  std::string filter;  // empty = no filter
};

[[noreturn]] void usage(int code) {
  std::fprintf(
      stderr,
      "Usage: coverage_probe <input.ll> [--filter=<substring>]\n"
      "\n"
      "  Runs the O2 pipeline on the input IR file and prints the unique\n"
      "  function names of every basic block reached during the run.\n"
      "  --filter=<sub> restricts output to function names containing the\n"
      "  given substring (e.g. --filter=SLPVectorizer).\n"
      "\n"
      "  llvm-symbolizer is invoked from $LLVM_SYMBOLIZER_PATH (or PATH).\n"
      "\n"
      "Exit codes: 0 success | 1 IR parse failure | 2 usage error\n");
  std::exit(code);
}

ProbeOptions parseArgs(int argc, char **argv) {
  ProbeOptions opts;
  for (int i = 1; i < argc; ++i) {
    std::string a = argv[i];
    if (a == "-h" || a == "--help") usage(2);
    static const char kFilter[] = "--filter=";
    if (a.rfind(kFilter, 0) == 0) {
      opts.filter = a.substr(sizeof(kFilter) - 1);
      continue;
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
  if (opts.input_path.empty()) usage(2);
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

int main(int argc, char **argv) {
  ProbeOptions opts = parseArgs(argc, argv);

  llvm::InitializeAllTargets();
  llvm::InitializeAllTargetMCs();
  llvm::InitializeAllAsmPrinters();
  llvm::InitializeAllAsmParsers();

  llvm::LLVMContext Ctx;
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
  if (!TMOwned) TMOwned = buildTM(llvm::Triple(kDefaultTripleStr));
  llvm::TargetMachine *TM = TMOwned.get();

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

  // Isolate pipeline coverage: zero counters now, then harvest.
  resetCounters();
  MPM.run(*M, MAM);

  std::vector<uintptr_t> pcs = harvestPCs();
  std::string self = selfPath();
  if (self.empty()) {
    std::fprintf(stderr,
                 "coverage_probe: cannot determine self path "
                 "(/proc/self/exe)\n");
    return 1;
  }

  // Translate runtime PCs to in-file static VAs for --obj symbolization.
  uintptr_t base = selfLoadBase(self);
  if (base != 0) {
    for (uintptr_t &pc : pcs) pc -= base;
  }

  std::vector<std::string> names = symbolize(pcs, self);

  std::set<std::string> uniq;
  for (const std::string &n : names) {
    if (n.empty()) continue;
    if (!opts.filter.empty() && n.find(opts.filter) == std::string::npos)
      continue;
    uniq.insert(n);
  }
  for (const std::string &n : uniq) std::printf("%s\n", n.c_str());
  return 0;
}
