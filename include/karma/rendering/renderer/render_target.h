#pragma once

namespace karma::renderer {

/// \ingroup karma_rendering
/// Render target creation descriptor.
struct RenderTargetDesc {
  int width = 0;
  int height = 0;
  bool depth = true;
  bool stencil = false;
};

}  // namespace karma::renderer
