#include "particle_effect_tools.h"

#include <filesystem>
#include <iostream>
#include <string>

int main(int argc, char** argv) {
  bool check_only = false;
  int first_file = 1;
  if (argc >= 2 && std::string(argv[1]) == "--check") {
    check_only = true;
    first_file = 2;
  }
  if (argc <= first_file) {
    std::cerr << "usage: karma_particle_effect_format [--check] <file.kpeffect>...\n";
    return 2;
  }

  bool ok = true;
  for (int i = first_file; i < argc; ++i) {
    std::string diagnostic;
    if (!karma::tools::particles::formatEffectFile(std::filesystem::path(argv[i]),
                                                   check_only,
                                                   &diagnostic)) {
      ok = false;
      std::cerr << diagnostic << '\n';
    }
  }
  return ok ? 0 : 1;
}
