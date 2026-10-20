// wgcli: command-line front end of worldgen-core.
// Placeholder until the commands of plan section 6.7 land in work package A10.

#include <iostream>
#include <string_view>

#include "wg/wg.hpp"

int main(int argc, char** argv) {
  for (int i = 1; i < argc; ++i) {
    const std::string_view arg{argv[i]};
    if (arg != "--version" && arg != "-V") {
      std::cerr << "wgcli: unknown argument '" << arg << "'\n";
      return 2;  // invalid input (plan section 6.7)
    }
  }
  std::cout << "wgcli " << wg::core::version_string() << '\n';
  return 0;
}
