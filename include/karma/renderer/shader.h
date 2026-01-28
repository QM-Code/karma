#pragma once

#include <string>
#include <vector>

namespace karma::renderer {

struct ShaderDesc {
  enum class Stage {
    Vertex,
    Fragment,
    Compute
  };

  struct Source {
    Stage stage = Stage::Vertex;
    std::string asset_key;
  };

  std::string shader_key;
  std::vector<Source> sources;
  bool is_lit = true;
};

}  // namespace karma::renderer
