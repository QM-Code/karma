#pragma once

namespace karma::renderer {

/// \ingroup karma_rendering
/// Per-frame renderer viewport and timing data.
struct FrameInfo {
  int width = 0;
  int height = 0;
  float delta_time = 0.0f;
};

}  // namespace karma::renderer
