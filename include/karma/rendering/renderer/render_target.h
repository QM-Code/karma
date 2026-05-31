#pragma once

namespace karma::renderer {

struct RenderTargetDesc {
  int width = 0;
  int height = 0;
  bool depth = true;
  bool stencil = false;
};

}  // namespace karma::renderer
