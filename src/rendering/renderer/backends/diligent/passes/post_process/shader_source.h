#pragma once

#include <string>

namespace karma::rendering::backend::post_process {

enum class ShaderFallback {
  FullscreenTriangle,
  Copy,
  Composite,
  BloomCombine,
  Temporal,
};

std::string loadShader(const char* filename, ShaderFallback fallback);

}  // namespace karma::rendering::backend::post_process
