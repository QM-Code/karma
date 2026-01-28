#pragma once

#include "karma/components/layers.h"

namespace karma::integration {

// Pseudocode sketch for GLFW input glue.
class GlfwInputBackend {
 public:
  void poll() {
    // glfwPollEvents();
  }

  void mapEvents(/* platform::Window& window */) {
    // Translate GLFW callbacks to engine input events.
  }
};

}  // namespace karma::integration
