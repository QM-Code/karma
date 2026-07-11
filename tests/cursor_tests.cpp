#include "karma/platform.h"

#include <cassert>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "../src/platform/window/standard_cursor_cache.h"
#include "../src/runtime/app/cursor_arbitration.h"

namespace {

class TrackingWindow final : public karma::platform::Window {
 public:
  void pollEvents() override {}
  const std::vector<karma::platform::Event>& events() const override {
    return events_;
  }
  void clearEvents() override { events_.clear(); }
  bool shouldClose() const override { return false; }
  void requestClose() override {}
  void swapBuffers() override {}
  void setVsync(bool) override {}
  void setFullscreen(bool) override {}
  bool isFullscreen() const override { return false; }
  void setIcon(const std::string&) override {}
  void getFramebufferSize(int& width, int& height) const override {
    width = 1280;
    height = 720;
  }
  float getContentScale() const override { return 1.0f; }
  bool isKeyDown(karma::platform::Key) const override { return false; }
  bool isMouseDown(karma::platform::MouseButton) const override { return false; }
  void setCursorVisible(bool) override {}
  void setCursorShape(karma::platform::CursorShape shape) override {
    cursor_commits.push_back(shape);
  }
  void setClipboardText(std::string_view) override {}
  std::string getClipboardText() const override { return {}; }
  void* nativeHandle() const override { return nullptr; }

  std::vector<karma::platform::CursorShape> cursor_commits;

 private:
  std::vector<karma::platform::Event> events_;
};

void testLayerPrecedenceAndSingleCommit() {
  using karma::app::detail::CursorArbitrator;
  using karma::platform::CursorShape;

  TrackingWindow window;

  CursorArbitrator native(CursorShape::Pointer);
  native.overrideWith(false, CursorShape::Crosshair);
  native.commit(&window);
  native.commit(&window);
  assert(window.cursor_commits.size() == 1u);
  assert(window.cursor_commits.back() == CursorShape::Pointer);

  CursorArbitrator custom(CursorShape::Pointer);
  custom.overrideWith(true, CursorShape::Crosshair);
  custom.overrideWith(false, CursorShape::Text);
  custom.commit(&window);
  assert(window.cursor_commits.size() == 2u);
  assert(window.cursor_commits.back() == CursorShape::Crosshair);

  CursorArbitrator debug(CursorShape::Pointer);
  debug.overrideWith(true, CursorShape::Crosshair);
  debug.overrideWith(true, CursorShape::Text);
  debug.commit(&window);
  assert(window.cursor_commits.size() == 3u);
  assert(window.cursor_commits.back() == CursorShape::Text);

  // An explicit Default request is an override, not the absence of a request.
  CursorArbitrator explicit_default(CursorShape::Pointer);
  explicit_default.overrideWith(true, CursorShape::Default);
  explicit_default.commit(&window);
  assert(window.cursor_commits.back() == CursorShape::Default);

  // A stable hover still resolves once each frame, then leaving all layers
  // restores the default on the following frame.
  CursorArbitrator stable_pointer(CursorShape::Pointer);
  stable_pointer.commit(&window);
  CursorArbitrator next_pointer(CursorShape::Pointer);
  next_pointer.commit(&window);
  CursorArbitrator restored_default;
  restored_default.commit(&window);
  assert(window.cursor_commits.size() == 7u);
  assert(window.cursor_commits[4] == CursorShape::Pointer);
  assert(window.cursor_commits[5] == CursorShape::Pointer);
  assert(window.cursor_commits[6] == CursorShape::Default);
}

void testStandardCursorCache() {
  karma::platform::detail::StandardCursorCache<int, void*> cache;
  int allocations = 0;
  int destructions = 0;
  auto create = [&](int shape) -> void* {
    ++allocations;
    if (shape < 0) {
      return nullptr;
    }
    return reinterpret_cast<void*>(static_cast<std::uintptr_t>(shape + 1));
  };

  void* pointer = cache.getOrCreate(1, create);
  assert(pointer != nullptr);
  assert(cache.getOrCreate(1, create) == pointer);
  assert(allocations == 1);

  void* text = cache.getOrCreate(2, create);
  assert(text != nullptr && text != pointer);
  assert(cache.getOrCreate(2, create) == text);
  assert(allocations == 2);
  assert(cache.size() == 2u);

  // Failed platform allocation is not cached, so a future request can retry.
  assert(cache.getOrCreate(-1, create) == nullptr);
  assert(cache.getOrCreate(-1, create) == nullptr);
  assert(allocations == 4);
  assert(cache.size() == 2u);

  cache.clear([&](void*) { ++destructions; });
  assert(destructions == 2);
  assert(cache.size() == 0u);
}

}  // namespace

int main() {
  testLayerPrecedenceAndSingleCommit();
  testStandardCursorCache();
  return 0;
}
