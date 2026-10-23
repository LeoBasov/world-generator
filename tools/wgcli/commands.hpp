#pragma once

// The commands of plan section 6.7, one source file each (cmd_<name>.cpp).

#include "args.hpp"
#include "common.hpp"

namespace wg::cli {

struct Command {
  const CommandSpec& spec;
  int (*run)(const ParsedArgs& args, const Io& io);
};

extern const CommandSpec kGenerateSpec;
extern const CommandSpec kSweepSpec;
extern const CommandSpec kInspectSpec;
extern const CommandSpec kCompareSpec;
extern const CommandSpec kHashSpec;
extern const CommandSpec kExportSpec;
extern const CommandSpec kValidateSpec;
extern const CommandSpec kBenchSpec;

int run_generate(const ParsedArgs& args, const Io& io);
int run_sweep(const ParsedArgs& args, const Io& io);
int run_inspect(const ParsedArgs& args, const Io& io);
int run_compare(const ParsedArgs& args, const Io& io);
int run_hash(const ParsedArgs& args, const Io& io);
int run_export(const ParsedArgs& args, const Io& io);
int run_validate(const ParsedArgs& args, const Io& io);
int run_bench(const ParsedArgs& args, const Io& io);

}  // namespace wg::cli
