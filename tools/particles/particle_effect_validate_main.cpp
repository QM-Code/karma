#include "particle_effect_tools.h"

#include <filesystem>
#include <iostream>
#include <string>

int main(int argc, char** argv) {
  if (argc < 2) {
    std::cerr << "usage: karma_particle_effect_validate <file.kpeffect>...\n";
    return 2;
  }

  bool ok = true;
  for (int i = 1; i < argc; ++i) {
    std::string diagnostic;
    if (!karma::tools::particles::validateEffectFile(std::filesystem::path(argv[i]),
                                                     &diagnostic)) {
      ok = false;
      std::cerr << diagnostic << '\n';
    }
  }
  return ok ? 0 : 1;
}
