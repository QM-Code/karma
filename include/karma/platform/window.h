#pragma once

#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "karma/platform/events.h"

namespace karma::platform {

struct WindowConfig {
  int width = 1280;
  int height = 720;
  std::string title = "Karma";
  std::string icon_path;
  int glMajor = 3;
  int glMinor = 3;
  bool glCoreProfile = true;
  int samples = 4;
};

class Window {
 public:
  virtual ~Window() = default;

  virtual void pollEvents() = 0;
  virtual const std::vector<Event>& events() const = 0;
  virtual void clearEvents() = 0;

  virtual bool shouldClose() const = 0;
  virtual void requestClose() = 0;

  virtual void swapBuffers() = 0;
  virtual void setVsync(bool enabled) = 0;
  virtual void setFullscreen(bool enabled) = 0;
  virtual bool isFullscreen() const = 0;
  virtual void setIcon(const std::string& path) = 0;

  virtual void getFramebufferSize(int& width, int& height) const = 0;
  virtual float getContentScale() const = 0;

  virtual bool isKeyDown(Key key) const = 0;
  virtual bool isMouseDown(MouseButton button) const = 0;

  virtual void setCursorVisible(bool visible) = 0;
  virtual void setClipboardText(std::string_view text) = 0;
  virtual std::string getClipboardText() const = 0;

  virtual void* nativeHandle() const = 0;
};

std::unique_ptr<Window> CreateWindow(const WindowConfig& config);
std::unique_ptr<Window> CreateGlfwWindow(const WindowConfig& config);
std::unique_ptr<Window> CreateSdlWindow(const WindowConfig& config);

}  // namespace karma::platform
