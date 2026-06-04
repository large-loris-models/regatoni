// spec-inject: per-host injection driver.
//
// For each host .ll, walk its defined functions, find compatible
// litmus tests in the library, and for each match clone the host
// module, link the litmus module in, splice a CallInst, RAUW one
// downstream use of the anchor value, verify, and write the result.

#pragma once

#include "src/spec-inject/litmus_library.h"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <map>
#include <random>
#include <string>
#include <vector>

namespace spec_inject {

struct InjectorOpts {
  size_t max_injections_per_fn = 5;
  uint64_t seed = 42;
};

struct InjectorStats {
  size_t hosts_seen = 0;
  size_t hosts_parsed = 0;
  size_t hosts_parse_failed = 0;
  size_t fns_seen = 0;
  size_t fns_skipped_no_values = 0;     // no usable typed values
  size_t attempts = 0;
  size_t written = 0;
  size_t skipped_no_compat = 0;
  size_t skipped_no_anchor = 0;
  size_t skipped_no_use = 0;
  size_t link_failed = 0;
  size_t verify_failed = 0;
  size_t io_failed = 0;
  std::map<std::string, size_t> written_by_litmus;
  std::map<std::string, size_t> verify_fail_by_litmus;
};

void mergeStats(InjectorStats &dst, const InjectorStats &src);

class Injector {
public:
  Injector(const LitmusLibrary &lib, InjectorOpts opts);

  // Process one host .ll file, writing zero or more injected .ll
  // files into `out_dir`. All bookkeeping accumulates into `stats`.
  // RNG is local to the call so threads do not contend.
  void processFile(const std::string &host_path,
                   const std::string &out_dir,
                   std::mt19937 &rng,
                   InjectorStats &stats);

private:
  const LitmusLibrary &lib_;
  InjectorOpts opts_;
};

}  // namespace spec_inject
