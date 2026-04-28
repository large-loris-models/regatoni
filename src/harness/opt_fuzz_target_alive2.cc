// opt_fuzz_target_alive2.cc — Alive2 verification harness.
//
// Parses IR, clones the module, runs opt O2 on the clone, then uses Alive2's
// Verifier::compareFunctions on (original, optimized). If any function is
// unsound (num_correct == 0), abort() so Centipede records the input.
//
// Linked against ASAN LLVM + Alive2 libs; NOT sancov instrumented and NOT
// linked against the Centipede runner (Centipede calls this as an
// --extra_binaries target in the background).

#include "llvm_util/compare.h"
#include "llvm_util/llvm2alive.h"
#include "llvm_util/llvm_optimizer.h"
#include "llvm_util/utils.h"
#include "smt/smt.h"
#include "util/config.h"

#include "llvm/Analysis/TargetLibraryInfo.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Verifier.h"
#include "llvm/IRReader/IRReader.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/SourceMgr.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/TargetParser/Triple.h"
#include "llvm/Transforms/Utils/Cloning.h"

#include <hiredis/hiredis.h>

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <iterator>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

using llvm_util::Verifier;

static llvm::LLVMContext *Ctx = nullptr;
static std::stringstream *LogStream = nullptr;
static std::optional<llvm::TargetLibraryInfoWrapperPass> TLI;
static std::optional<smt::smt_initializer> SmtInit;
static std::optional<Verifier> Verif;
static std::optional<llvm_util::initializer> LlvmUtilInit;
static bool VerifierReady = false;

// Best-effort dedup cache. RedisCtx stays null if redis is unavailable; all
// cache helpers no-op in that case so the harness behaves exactly as before.
static redisContext *RedisCtx = nullptr;
static const uint8_t *CurrentData = nullptr;
static size_t CurrentSize = 0;

static void initRedis() {
  const char *host = std::getenv("REDIS_HOST");
  if (!host || !*host)
    host = "127.0.0.1";
  const char *port_str = std::getenv("REDIS_PORT");
  int port = (port_str && *port_str) ? std::atoi(port_str) : 6379;

  RedisCtx = redisConnect(host, port);
  if (!RedisCtx) {
    std::cerr << "[cache] redisConnect returned null; proceeding without cache\n";
    return;
  }
  if (RedisCtx->err) {
    std::cerr << "[cache] redis connection failed: " << RedisCtx->errstr
              << "; proceeding without cache\n";
    redisFree(RedisCtx);
    RedisCtx = nullptr;
  }
}

static void disableRedis() {
  if (RedisCtx) {
    redisFree(RedisCtx);
    RedisCtx = nullptr;
  }
}

static bool cacheGet(const uint8_t *data, size_t size, std::string &out) {
  if (!RedisCtx)
    return false;
  redisReply *reply = (redisReply *)redisCommand(
      RedisCtx, "GET %b", reinterpret_cast<const char *>(data), size);
  if (!reply || RedisCtx->err) {
    if (reply)
      freeReplyObject(reply);
    disableRedis();
    return false;
  }
  bool hit = false;
  if (reply->type == REDIS_REPLY_STRING) {
    out.assign(reply->str, reply->len);
    hit = true;
  }
  freeReplyObject(reply);
  return hit;
}

static void cacheSet(const uint8_t *data, size_t size, const char *verdict) {
  if (!RedisCtx || !data || size == 0)
    return;
  redisReply *reply = (redisReply *)redisCommand(
      RedisCtx, "SET %b %s", reinterpret_cast<const char *>(data), size,
      verdict);
  if (!reply || RedisCtx->err) {
    if (reply)
      freeReplyObject(reply);
    disableRedis();
    return;
  }
  freeReplyObject(reply);
}

static int verdictExitCode(const std::string &v) {
  if (v == "pass")
    return 0;
  if (v == "fail")
    return 134;
  if (v == "timeout")
    return 124;
  return 1;
}

static void ensureVerifier(const llvm::Module &M) {
  if (VerifierReady)
    return;
  if (!LlvmUtilInit.has_value())
    LlvmUtilInit.emplace(*LogStream, M.getDataLayout());
  llvm::Triple TT(M.getTargetTriple());
  TLI.emplace(TT);
  util::config::disable_undef_input = true;
  if (const char *unroll_env = std::getenv("ALIVE2_UNROLL")) {
    unsigned n = std::atoi(unroll_env);
    util::config::src_unroll_cnt = n;
    util::config::tgt_unroll_cnt = n;
  }
  smt::set_query_timeout("5000");
  SmtInit.emplace();
  Verif.emplace(TLI.value(), SmtInit.value(), *LogStream);
  VerifierReady = true;
}

extern "C" int LLVMFuzzerInitialize(int *argc, char ***argv) {
  Ctx = new llvm::LLVMContext();
  LogStream = new std::stringstream();
  initRedis();
  return 0;
}

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *Data, size_t Size) {
  if (Size == 0)
    return 0;

  // Dedup cache lookup. The raw IR content is the key; the value is the
  // verdict string written by a previous run. On hit, replay the verdict
  // via the appropriate exit code so the oracle's log capture sees it.
  std::string Cached;
  if (cacheGet(Data, Size, Cached)) {
    std::cerr << "[cache] hit: " << Cached << "\n";
    int code = verdictExitCode(Cached);
    disableRedis();
    std::exit(code);
  }

  // Recorded so the abort site below can write "fail" before terminating.
  CurrentData = Data;
  CurrentSize = Size;

  llvm::SMDiagnostic Err;
  auto Buf = llvm::MemoryBuffer::getMemBufferCopy(
      llvm::StringRef(reinterpret_cast<const char *>(Data), Size),
      "fuzz_input");

  auto M1 = llvm::parseIR(*Buf, Err, *Ctx);
  if (!M1) {
    cacheSet(Data, Size, "pass");
    return 0;
  }
  if (llvm::verifyModule(*M1, nullptr)) {
    cacheSet(Data, Size, "pass");
    return 0;
  }

  std::unique_ptr<llvm::Module> M2 = llvm::CloneModule(*M1);
  if (!llvm_util::optimize_module(*M2, "O2").empty()) {
    cacheSet(Data, Size, "pass");
    return 0;
  }

  ensureVerifier(*M1);

  for (llvm::Function &F1 : *M1) {
    if (F1.isDeclaration())
      continue;
    llvm::Function *F2 = M2->getFunction(F1.getName());
    if (!F2 || F2->isDeclaration())
      continue;

    Verif->num_correct = 0;
    Verif->num_errors = 0;
    Verif->num_unsound = 0;
    Verif->num_failed = 0;
    Verif->compareFunctions(F1, *F2);

    // Only a definitive unsoundness result is a bug. Timeouts (num_failed) and
    // translation/Alive2 errors (num_errors) are skipped — continue to next fn.
    if (Verif->num_unsound > 0) {
      // Known false positive filters. Extend this list as new patterns are
      // discovered:
      //   - "did not return" : recursion, Alive2 can't model it
      //   - "timeout"        : Z3 SMT query timeout
      //   - "initializes("   : initializes() param attribute not handled
      //                        correctly by Alive2
      const std::string Log = LogStream->str();
      std::string OptIR;
      llvm::raw_string_ostream OS(OptIR);
      F2->print(OS);
      OS.flush();

      bool false_positive = Log.find("did not return") != std::string::npos ||
                            Log.find("timeout") != std::string::npos ||
                            OptIR.find("initializes(") != std::string::npos;

      if (!false_positive) {
        llvm::errs() << "ALIVE2 MISCOMPILE in function " << F1.getName() << "\n"
                     << Log << "\n";
        cacheSet(CurrentData, CurrentSize, "fail");
        std::abort();
      }
    }
    LogStream->str("");
  }

  cacheSet(Data, Size, "pass");
  return 0;
}

static std::vector<uint8_t> readAll(std::istream &in) {
  return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

int main(int argc, char **argv) {
  LLVMFuzzerInitialize(&argc, &argv);

  if (argc < 2) {
    auto bytes = readAll(std::cin);
    LLVMFuzzerTestOneInput(bytes.data(), bytes.size());
    disableRedis();
    return 0;
  }

  for (int i = 1; i < argc; ++i) {
    std::ifstream f(argv[i], std::ios::binary);
    if (!f) {
      llvm::errs() << "failed to open " << argv[i] << "\n";
      disableRedis();
      return 1;
    }
    auto bytes = readAll(f);
    LLVMFuzzerTestOneInput(bytes.data(), bytes.size());
  }
  disableRedis();
  return 0;
}
