#pragma once

#include <filesystem>
#include <string>

namespace karma::tools::particles {

bool validateEffectFile(const std::filesystem::path& path, std::string* diagnostic = nullptr);

bool formatEffectFile(const std::filesystem::path& path,
                      bool check_only,
                      std::string* diagnostic = nullptr);

bool generateParticleEffectPackage(const std::filesystem::path& spec_path,
                                   const std::filesystem::path& output_dir,
                                   std::string* diagnostic = nullptr);

}  // namespace karma::tools::particles
