#include "particle_effect_tools.h"

#include <filesystem>
#include <iostream>
#include <string>

int main(int argc, char** argv) {
  if (argc != 3) {
    std::cerr << "usage: karma_particle_effect_generate <effect.kpspec.json> <output_dir>\n";
    return 2;
  }

  std::string diagnostic;
  if (!karma::tools::particles::generateParticleEffectPackage(std::filesystem::path(argv[1]),
                                                              std::filesystem::path(argv[2]),
                                                              &diagnostic)) {
    std::cerr << diagnostic << '\n';
    return 1;
  }
  return 0;
}
