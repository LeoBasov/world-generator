// wgcli: command-line front end of worldgen-core (plan section 6.7). The commands live in
// wgcli_app (cli.hpp) so the tests can run them in-process.

#include <iostream>
#include <string_view>
#include <vector>

#include "cli.hpp"

int main(int argc, char** argv) {
  try {
    std::vector<std::string_view> args;
    args.reserve(static_cast<std::size_t>(argc > 1 ? argc - 1 : 0));
    for (int i = 1; i < argc; ++i) {
      args.emplace_back(argv[i]);
    }
    return wg::cli::run(args, std::cout, std::cerr);
  } catch (...) {
    return 3;  // internal error (plan section 6.7)
  }
}
