#pragma once

#include <string>

namespace karma::renderer {

struct SkyboxDesc {
  std::string cubemap_texture_key;
  float intensity = 1.0f;
  bool use_irradiance = true;
};

}  // namespace karma::renderer
