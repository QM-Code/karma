#pragma once

#include "karma/platform.h"

namespace karma::app::detail {

// Resolves UI cursor requests in paint/input precedence and commits once after
// every participating layer has had an opportunity to override the shape.
class CursorArbitrator {
 public:
  explicit CursorArbitrator(
      platform::CursorShape native_shape = platform::CursorShape::Default)
      : shape_(native_shape) {}

  void overrideWith(bool requested, platform::CursorShape shape) {
    if (requested) {
      shape_ = shape;
    }
  }

  [[nodiscard]] platform::CursorShape shape() const { return shape_; }

  void commit(platform::Window* window) {
    if (!window || committed_) {
      return;
    }
    window->setCursorShape(shape_);
    committed_ = true;
  }

 private:
  platform::CursorShape shape_ = platform::CursorShape::Default;
  bool committed_ = false;
};

}  // namespace karma::app::detail
