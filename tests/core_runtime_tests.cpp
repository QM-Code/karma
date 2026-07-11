#ifdef NDEBUG
#undef NDEBUG
#endif

#include <algorithm>
#include <array>
#include <atomic>
#include <cassert>
#include <chrono>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "karma/app.h"
#include "karma/core.h"
#include "karma/platform.h"
#include "karma/world.h"
#include "../src/platform/window/gamepad_repeat.h"
#include "../src/runtime/app/ui_event_routing.h"

namespace {

struct TestComponent : karma::world::ComponentTag {
  int value = 0;
};

template <int Index>
struct ConcurrentType {};

template <int Index>
void assignTypeId(std::array<karma::core::TypeId, 8>& ids,
                  std::atomic<int>& ready,
                  std::atomic<bool>& go) {
  ready.fetch_add(1, std::memory_order_release);
  while (!go.load(std::memory_order_acquire)) {
    std::this_thread::yield();
  }
  ids[Index] = karma::core::typeId<ConcurrentType<Index>>();
}

void testConcurrentTypeIds() {
  std::array<karma::core::TypeId, 8> ids{};
  std::atomic<int> ready{0};
  std::atomic<bool> go{false};
  std::array<std::thread, 8> threads{
      std::thread(assignTypeId<0>, std::ref(ids), std::ref(ready), std::ref(go)),
      std::thread(assignTypeId<1>, std::ref(ids), std::ref(ready), std::ref(go)),
      std::thread(assignTypeId<2>, std::ref(ids), std::ref(ready), std::ref(go)),
      std::thread(assignTypeId<3>, std::ref(ids), std::ref(ready), std::ref(go)),
      std::thread(assignTypeId<4>, std::ref(ids), std::ref(ready), std::ref(go)),
      std::thread(assignTypeId<5>, std::ref(ids), std::ref(ready), std::ref(go)),
      std::thread(assignTypeId<6>, std::ref(ids), std::ref(ready), std::ref(go)),
      std::thread(assignTypeId<7>, std::ref(ids), std::ref(ready), std::ref(go)),
  };
  while (ready.load(std::memory_order_acquire) != static_cast<int>(threads.size())) {
    std::this_thread::yield();
  }
  go.store(true, std::memory_order_release);
  for (std::thread& thread : threads) {
    thread.join();
  }

  std::unordered_set<karma::core::TypeId> unique(ids.begin(), ids.end());
  assert(unique.size() == ids.size());
  assert(std::none_of(ids.begin(), ids.end(), [](karma::core::TypeId id) {
    return id == 0;
  }));
}

void testGamepadRepeatScheduler() {
  using karma::platform::Event;
  using karma::platform::EventType;
  using karma::platform::GamepadAxis;
  using karma::platform::GamepadButton;
  using karma::platform::detail::GamepadRepeatScheduler;

  GamepadRepeatScheduler scheduler;
  std::vector<Event> events;
  const GamepadRepeatScheduler::TimePoint start{};

  scheduler.buttonChanged(3, GamepadButton::DpadRight, true, start);
  scheduler.buttonChanged(3, GamepadButton::A, true, start);
  scheduler.axisChanged(3, GamepadAxis::LeftY, -0.8f, start);
  scheduler.buttonChanged(3, GamepadButton::DpadRight, true,
                          start + std::chrono::milliseconds(200));
  scheduler.axisChanged(3, GamepadAxis::LeftY, -0.9f,
                        start + std::chrono::milliseconds(200));
  scheduler.appendDue(events, start + std::chrono::milliseconds(449));
  assert(events.empty());

  scheduler.appendDue(events, start + std::chrono::milliseconds(450));
  assert(events.size() == 2u);
  assert(events[0].repeat && events[1].repeat);
  assert(std::ranges::any_of(events, [](const Event& event) {
    return event.type == EventType::GamepadButtonDown &&
           event.gamepadButton == GamepadButton::DpadRight;
  }));
  assert(std::ranges::any_of(events, [](const Event& event) {
    return event.type == EventType::GamepadAxisMotion &&
           event.gamepadAxis == GamepadAxis::LeftY &&
           event.gamepadValue == -0.9f;
  }));
  assert(std::ranges::none_of(events, [](const Event& event) {
    return event.gamepadButton == GamepadButton::A;
  }));

  events.clear();
  scheduler.appendDue(events, start + std::chrono::milliseconds(539));
  assert(events.empty());
  scheduler.appendDue(events, start + std::chrono::milliseconds(540));
  assert(events.size() == 2u);

  events.clear();
  scheduler.buttonChanged(3, GamepadButton::DpadRight, false,
                          start + std::chrono::milliseconds(550));
  scheduler.axisChanged(3, GamepadAxis::LeftY, 0.0f,
                        start + std::chrono::milliseconds(550));
  scheduler.appendDue(events, start + std::chrono::seconds(2));
  assert(events.empty());

  scheduler.axisChanged(3, GamepadAxis::LeftX, 0.9f, start);
  scheduler.axisChanged(3, GamepadAxis::LeftX, -0.9f,
                        start + std::chrono::milliseconds(300));
  scheduler.appendDue(events, start + std::chrono::milliseconds(749));
  assert(events.empty());
  scheduler.appendDue(events, start + std::chrono::milliseconds(750));
  assert(events.size() == 1u && events.front().gamepadValue == -0.9f);

  events.clear();
  scheduler.resetGamepad(3);
  scheduler.appendDue(events, start + std::chrono::seconds(4));
  assert(events.empty());
}

void testStackedUiReleaseRouting() {
  using karma::app::detail::isUiStateReleaseEvent;
  using karma::app::detail::shouldRouteToLowerUiLayer;
  using karma::platform::Event;
  using karma::platform::EventType;

  assert(!shouldRouteToLowerUiLayer(
      true, Event{.type = EventType::KeyDown}));
  assert(shouldRouteToLowerUiLayer(
      true, Event{.type = EventType::KeyUp}));
  assert(shouldRouteToLowerUiLayer(
      true, Event{.type = EventType::MouseButtonUp}));
  assert(shouldRouteToLowerUiLayer(
      true, Event{.type = EventType::GamepadButtonUp}));
  assert(isUiStateReleaseEvent(
      Event{.type = EventType::GamepadDisconnected}));
  assert(isUiStateReleaseEvent(
      Event{.type = EventType::GamepadAxisMotion, .gamepadValue = 0.1f}));
  assert(!isUiStateReleaseEvent(
      Event{.type = EventType::GamepadAxisMotion, .gamepadValue = 0.8f}));
  assert(shouldRouteToLowerUiLayer(
      false, Event{.type = EventType::KeyDown}));
}

void testEntityLivenessAndComponents() {
  karma::world::World world;
  const auto first = world.createEntity();
  const auto removed = world.createEntity();
  const auto last = world.createEntity();
  world.add(first, TestComponent{.value = 7});
  assert(world.tryGet<TestComponent>(first)->value == 7);
  assert(world.tryGet<TestComponent>(last) == nullptr);

  world.destroyEntity(removed);
  const karma::world::Entity forged{removed.index, removed.generation + 1};
  assert(!world.isAlive(removed));
  assert(!world.isAlive(forged));
  assert(world.isAlive(first));
  assert(world.isAlive(last));

  const auto reused = world.createEntity();
  assert(reused == forged);
  assert(world.isAlive(reused));
  world.add(reused, TestComponent{.value = 11});
  assert(world.get<TestComponent>(reused).value == 11);

  world.destroyEntity(reused);
  assert(world.tryGet<TestComponent>(reused) == nullptr);
  bool threw = false;
  try {
    (void)world.get<TestComponent>(reused);
  } catch (const std::out_of_range&) {
    threw = true;
  }
  assert(threw);
}

void testSceneCycleRejection() {
  karma::world::Scene scene;
  const auto root = scene.createNode();
  const auto child = scene.createNode();
  const auto grandchild = scene.createNode();
  assert(scene.reparent(child, root));
  assert(scene.reparent(grandchild, child));
  assert(!scene.reparent(root, grandchild));
  assert(!scene.reparent(child, child));
  assert(!scene.reparent(child, 9999));
  assert(scene.get(child).parent == root);
  assert(scene.reparent(child, karma::world::Node::kInvalidId));
  assert(scene.get(child).parent == karma::world::Node::kInvalidId);
  assert(scene.get(root).children.empty());
}

class LoggingSystem final : public karma::world::ISystem {
 public:
  LoggingSystem(int value, std::vector<int>& log) : value_(value), log_(log) {}

  std::string_view name() const override { return "LoggingSystem"; }
  void update(karma::world::World&, float) override { log_.push_back(value_); }

 private:
  int value_ = 0;
  std::vector<int>& log_;
};

void testSystemGraphValidationAndOrder() {
  karma::world::SystemGraph graph;
  std::vector<int> log;
  const auto third = graph.addSystem(std::make_unique<LoggingSystem>(3, log));
  const auto first = graph.addSystem(std::make_unique<LoggingSystem>(1, log));
  const auto second = graph.addSystem(std::make_unique<LoggingSystem>(2, log));
  assert(graph.addDependency(third, second));
  assert(!graph.addDependency(third, second));
  assert(!graph.addDependency(second, third));
  assert(!graph.addDependency(third, 9999));
  assert(!graph.addDependency(9999, first));

  karma::world::World world;
  graph.update(world, 0.0f);
  assert((log == std::vector<int>{1, 2, 3}));

  const auto systems = graph.systems();
  assert(systems.size() == 3);
  assert(systems[0].id == third);
  assert(systems[1].id == first);
  assert(systems[2].id == second);

  bool threw = false;
  try {
    (void)graph.addSystem(nullptr);
  } catch (const std::invalid_argument&) {
    threw = true;
  }
  assert(threw);
}

class FakeWindow final : public karma::platform::Window {
 public:
  void pollEvents() override {}
  const std::vector<karma::platform::Event>& events() const override { return events_; }
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
  bool isKeyDown(karma::platform::Key key) const override {
    return keys_.contains(key);
  }
  bool isMouseDown(karma::platform::MouseButton button) const override {
    return mouse_buttons_.contains(button);
  }
  bool isGamepadButtonDown(karma::platform::GamepadButton button,
                           int = -1) const override {
    return gamepad_buttons_.contains(button);
  }
  float gamepadAxis(karma::platform::GamepadAxis axis, int = -1) const override {
    const auto found = gamepad_axes_.find(axis);
    return found == gamepad_axes_.end() ? 0.0f : found->second;
  }
  void setCursorVisible(bool) override {}
  void setClipboardText(std::string_view text) override { clipboard_ = text; }
  std::string getClipboardText() const override { return clipboard_; }
  void* nativeHandle() const override { return reinterpret_cast<void*>(1); }

  std::unordered_set<karma::platform::Key> keys_;
  std::unordered_set<karma::platform::MouseButton> mouse_buttons_;
  std::unordered_set<karma::platform::GamepadButton> gamepad_buttons_;
  std::unordered_map<karma::platform::GamepadAxis, float> gamepad_axes_;
  std::vector<karma::platform::Event> events_;
  std::string clipboard_;
};

void testInputRepeatAndHeldModifiers() {
  FakeWindow window;
  karma::app::InputSystem input;
  input.setWindow(&window);
  input.bindKey("save", karma::platform::Key::S, karma::app::Trigger::Down);
  input.setRequiredModifiers("save", {.control = true});
  input.bindKey("jump", karma::platform::Key::Space, karma::app::Trigger::Pressed);
  input.bindMouse("fire", karma::platform::MouseButton::Left,
                  karma::app::Trigger::Down);
  input.bindGamepadButton("accept", karma::platform::GamepadButton::A,
                          karma::app::Trigger::Down);
  input.bindGamepadAxis("steer", karma::platform::GamepadAxis::LeftX, 0.5f,
                        true, karma::app::Trigger::Down);

  window.keys_.insert(karma::platform::Key::S);
  input.update({});
  assert(!input.actionDown("save"));
  window.keys_.insert(karma::platform::Key::LeftControl);
  input.update({});
  assert(input.actionDown("save"));

  karma::platform::Event repeat{};
  repeat.type = karma::platform::EventType::KeyDown;
  repeat.key = karma::platform::Key::Space;
  repeat.repeat = true;
  input.update({repeat});
  assert(!input.actionPressed("jump"));
  repeat.repeat = false;
  input.update({repeat});
  assert(input.actionPressed("jump"));

  karma::platform::Event first_move{};
  first_move.type = karma::platform::EventType::MouseMove;
  first_move.x = 10.0;
  first_move.y = 20.0;
  input.update({first_move});
  karma::platform::Event second_move = first_move;
  second_move.x = 15.0;
  second_move.y = 28.0;
  input.update({second_move});
  assert(input.mouseDeltaX() == 5.0f && input.mouseDeltaY() == 8.0f);

  karma::platform::Event focus_lost{};
  focus_lost.type = karma::platform::EventType::WindowFocus;
  focus_lost.focused = false;
  karma::platform::Event resumed_move = second_move;
  resumed_move.x = 900.0;
  resumed_move.y = 700.0;
  window.mouse_buttons_.insert(karma::platform::MouseButton::Left);
  window.gamepad_buttons_.insert(karma::platform::GamepadButton::A);
  window.gamepad_axes_[karma::platform::GamepadAxis::LeftX] = 0.8f;
  karma::platform::Event focus_lost_press{};
  focus_lost_press.type = karma::platform::EventType::KeyDown;
  focus_lost_press.key = karma::platform::Key::Space;
  input.update({focus_lost_press, focus_lost, resumed_move});
  assert(!input.actionDown("save"));
  assert(!input.actionDown("fire"));
  assert(!input.actionDown("accept"));
  assert(!input.actionDown("steer"));
  assert(!input.actionPressed("jump"));
  assert(input.mouseDeltaX() == 0.0f && input.mouseDeltaY() == 0.0f);
}

void testInputFilteringAndGamepadBindings() {
  using namespace karma;
  FakeWindow window;
  app::InputSystem input;
  input.setWindow(&window);
  input.bindKey("held-key", platform::Key::W);
  input.bindKey("filtered-shortcut", platform::Key::S, app::Trigger::Pressed);
  input.setRequiredModifiers("filtered-shortcut", {.control = true});
  input.bindMouse("held-mouse", platform::MouseButton::Left);
  input.bindGamepadButton("pad-press", platform::GamepadButton::A,
                          app::Trigger::Pressed);
  input.bindGamepadButton("pad-held", platform::GamepadButton::A);
  input.bindGamepadAxis("pad-right", platform::GamepadAxis::LeftX, 0.5f);

  window.keys_.insert(platform::Key::W);
  window.mouse_buttons_.insert(platform::MouseButton::Left);
  window.gamepad_buttons_.insert(platform::GamepadButton::A);
  window.gamepad_axes_[platform::GamepadAxis::LeftX] = 0.8f;
  input.update({});
  assert(input.actionDown("held-key"));
  assert(input.actionDown("held-mouse"));
  assert(input.actionDown("pad-held"));
  assert(input.actionDown("pad-right"));

  app::InputFilter held_filter;
  held_filter.keys.insert(platform::Key::W);
  held_filter.keys.insert(platform::Key::LeftControl);
  held_filter.mouse_buttons.insert(platform::MouseButton::Left);
  held_filter.gamepad_buttons.insert(platform::GamepadButton::A);
  held_filter.gamepad_axes.insert(platform::GamepadAxis::LeftX);
  input.update({}, held_filter);
  assert(!input.actionDown("held-key"));
  assert(!input.actionDown("held-mouse"));
  assert(!input.actionDown("pad-held"));
  assert(!input.actionDown("pad-right"));

  platform::Event shortcut{};
  shortcut.type = platform::EventType::KeyDown;
  shortcut.key = platform::Key::S;
  shortcut.mods.control = true;
  input.update({shortcut}, held_filter);
  assert(!input.actionPressed("filtered-shortcut"));

  platform::Event button_down{};
  button_down.type = platform::EventType::GamepadButtonDown;
  button_down.gamepadButton = platform::GamepadButton::A;
  input.update({button_down}, held_filter);
  assert(!input.actionPressed("pad-press"));
  input.update({button_down});
  assert(input.actionPressed("pad-press"));

  platform::Event first_move{};
  first_move.type = platform::EventType::MouseMove;
  first_move.x = 4.0;
  first_move.y = 6.0;
  input.update({first_move});
  platform::Event captured_move = first_move;
  captured_move.x = 40.0;
  captured_move.y = 60.0;
  app::InputFilter motion_filter;
  motion_filter.mouse_motion = true;
  input.update({captured_move}, motion_filter);
  assert(input.mouseDeltaX() == 0.0f && input.mouseDeltaY() == 0.0f);
  platform::Event resumed_move = captured_move;
  resumed_move.x = 42.0;
  resumed_move.y = 63.0;
  input.update({resumed_move});
  assert(input.mouseDeltaX() == 2.0f && input.mouseDeltaY() == 3.0f);
}

struct RuntimeCounts {
  int attached = 0;
  int frame_begun = 0;
  int updated = 0;
  int frame_ended = 0;
  int detached = 0;
};

class CountingModule final : public karma::app::RuntimeModule {
 public:
  explicit CountingModule(RuntimeCounts& counts, bool throw_on_attach = false)
      : counts_(counts), throw_on_attach_(throw_on_attach) {}

  void onAttach(const karma::app::RuntimeModuleContext& context) override {
    ++counts_.attached;
    assert(context.scene != nullptr);
    assert(context.assets != nullptr);
    assert(context.graphics == nullptr);
    if (throw_on_attach_) {
      throw std::runtime_error("module attach failed");
    }
  }
  void onFrameBegin(karma::world::World&, float) override { ++counts_.frame_begun; }
  void onUpdate(karma::world::World&, float, float) override { ++counts_.updated; }
  void onFrameEnd(karma::world::World&) override { ++counts_.frame_ended; }
  void onDetach() override { ++counts_.detached; }

 private:
  RuntimeCounts& counts_;
  bool throw_on_attach_ = false;
};

class AddingModule final : public karma::app::RuntimeModule {
 public:
  AddingModule(karma::app::EngineApp& app, RuntimeCounts& added_counts)
      : app_(app), added_counts_(added_counts) {}

  void onAttach(const karma::app::RuntimeModuleContext&) override {}
  void onUpdate(karma::world::World&, float, float) override {
    if (!added_) {
      added_ = true;
      app_.addRuntimeModule(std::make_unique<CountingModule>(added_counts_));
    }
  }

 private:
  karma::app::EngineApp& app_;
  RuntimeCounts& added_counts_;
  bool added_ = false;
};

struct GameCounts {
  int started = 0;
  int fixed = 0;
  int updated = 0;
  int shutdown = 0;
};

class CountingGame final : public karma::app::GameInterface {
 public:
  explicit CountingGame(GameCounts& counts, bool throw_on_start = false)
      : counts_(counts), throw_on_start_(throw_on_start) {}

  void onStart() override {
    ++counts_.started;
    assert(world != nullptr);
    assert(graphics == nullptr);
    if (throw_on_start_) {
      throw std::runtime_error("startup failed");
    }
  }
  void onFixedUpdate(float) override { ++counts_.fixed; }
  void onUpdate(float) override { ++counts_.updated; }
  void onShutdown() override { ++counts_.shutdown; }

  bool contextBound() const { return world != nullptr; }

 private:
  GameCounts& counts_;
  bool throw_on_start_ = false;
};

class DisabledFrameGraphGame final : public karma::app::GameInterface {
 public:
  void onStart() override {
    const auto& graph = assets->resolveFrameGraph(karma::rendering::kDefaultFrameGraphKey);
    observed_disabled_graph = !graph.enabled;
  }
  void onFixedUpdate(float) override {}
  void onUpdate(float) override {}
  void onShutdown() override {}

  bool observed_disabled_graph = false;
};

class StopOnFixedGame final : public karma::app::GameInterface {
 public:
  StopOnFixedGame(karma::app::EngineApp& app, GameCounts& counts)
      : app_(app), counts_(counts) {}

  void onStart() override { ++counts_.started; }
  void onFixedUpdate(float) override {
    ++counts_.fixed;
    app_.requestStop();
  }
  void onUpdate(float) override { ++counts_.updated; }
  void onShutdown() override { ++counts_.shutdown; }

 private:
  karma::app::EngineApp& app_;
  GameCounts& counts_;
};

karma::app::EngineConfig headlessConfig() {
  karma::app::EngineConfig config;
  config.loading_splash.enabled = false;
  config.frame_pacing_fps = 0.0f;
  return config;
}

void testEngineValidationAndHeadlessLifecycle() {
  auto invalid = headlessConfig();
  invalid.fixed_dt = 0.0f;
  const auto validation = karma::app::validateEngineConfig(invalid);
  assert(!validation.valid());
  assert(!validation.errors.empty());

  auto disabled_splash = headlessConfig();
  disabled_splash.loading_splash.target_fps = 0;
  assert(karma::app::validateEngineConfig(disabled_splash).valid());

  auto invalid_rendering = headlessConfig();
  invalid_rendering.window.samples = -1;
  invalid_rendering.background_color.r =
      std::numeric_limits<float>::quiet_NaN();
  invalid_rendering.lighting_exposure =
      std::numeric_limits<float>::infinity();
  const auto invalid_rendering_validation =
      karma::app::validateEngineConfig(invalid_rendering);
  assert(!invalid_rendering_validation.valid());
  assert(invalid_rendering_validation.errors.size() >= 3u);

  auto invalid_graph = headlessConfig();
  invalid_graph.default_frame_graph.frame_graph_key = "invalid/missing-output";
  invalid_graph.default_frame_graph.output_resource = "missing-output";
  assert(!karma::app::validateEngineConfig(invalid_graph).valid());

  GameCounts invalid_counts;
  CountingGame invalid_game(invalid_counts);
  karma::app::EngineApp invalid_app;
  bool invalid_threw = false;
  try {
    invalid_app.start(invalid_game, invalid);
  } catch (const std::invalid_argument&) {
    invalid_threw = true;
  }
  assert(invalid_threw);
  assert(invalid_counts.started == 0);

  auto missing_package_config = headlessConfig();
  missing_package_config.startup_asset_packages.emplace_back(
      "this-package-does-not-exist.kap");
  GameCounts missing_package_counts;
  CountingGame missing_package_game(missing_package_counts);
  karma::app::EngineApp missing_package_app;
  bool missing_package_threw = false;
  try {
    missing_package_app.start(missing_package_game, missing_package_config);
  } catch (const std::runtime_error&) {
    missing_package_threw = true;
  }
  assert(missing_package_threw);
  assert(missing_package_counts.started == 0);
  assert(!missing_package_game.contextBound());
  assert(!missing_package_app.isRunning());

  auto disabled_graph_config = headlessConfig();
  disabled_graph_config.default_frame_graph.enabled = false;
  DisabledFrameGraphGame disabled_graph_game;
  karma::app::EngineApp disabled_graph_app;
  disabled_graph_app.start(disabled_graph_game, disabled_graph_config);
  assert(disabled_graph_game.observed_disabled_graph);
  disabled_graph_app.requestStop();
  disabled_graph_app.tick();

  RuntimeCounts module_counts;
  GameCounts game_counts;
  CountingGame game(game_counts);
  {
    karma::app::EngineApp app;
    bool null_module_threw = false;
    try {
      app.addRuntimeModule(nullptr);
    } catch (const std::invalid_argument&) {
      null_module_threw = true;
    }
    assert(null_module_threw);
    app.addRuntimeModule(std::make_unique<CountingModule>(module_counts));
    RuntimeCounts reentrant_module_counts;
    app.addRuntimeModule(
        std::make_unique<AddingModule>(app, reentrant_module_counts));
    app.start(game, headlessConfig());
    assert(app.isRunning());
    assert(module_counts.attached == 1);
    RuntimeCounts late_module_counts;
    app.addRuntimeModule(std::make_unique<CountingModule>(late_module_counts));
    assert(late_module_counts.attached == 1);
    RuntimeCounts late_failure_counts;
    bool late_attach_threw = false;
    try {
      app.addRuntimeModule(
          std::make_unique<CountingModule>(late_failure_counts, true));
    } catch (const std::runtime_error&) {
      late_attach_threw = true;
    }
    assert(late_attach_threw);
    assert(late_failure_counts.attached == 1 && late_failure_counts.detached == 0);
    app.tick();
    assert(game_counts.updated == 1);
    assert(module_counts.updated == 1);
    assert(late_module_counts.updated == 1);
    assert(reentrant_module_counts.attached == 1);
    assert(reentrant_module_counts.frame_begun == 0);
    assert(reentrant_module_counts.updated == 0);
    assert(reentrant_module_counts.frame_ended == 0);
    app.tick();
    assert(game_counts.updated == 2);
    assert(module_counts.updated == 2);
    assert(late_module_counts.updated == 2);
    assert(reentrant_module_counts.frame_begun == 1);
    assert(reentrant_module_counts.updated == 1);
    assert(reentrant_module_counts.frame_ended == 1);
    app.requestStop();
    assert(!app.isRunning());
    app.tick();
    assert(game_counts.shutdown == 1);
    assert(module_counts.detached == 1);
    assert(late_module_counts.detached == 1);
    assert(reentrant_module_counts.detached == 1);
    assert(!game.contextBound());
    bool restart_threw = false;
    try {
      app.start(game, headlessConfig());
    } catch (const std::logic_error&) {
      restart_threw = true;
    }
    assert(restart_threw);
  }
  assert(module_counts.detached == 1);

  GameCounts throwing_counts;
  CountingGame throwing_game(throwing_counts, true);
  karma::app::EngineApp throwing_app;
  bool startup_threw = false;
  try {
    throwing_app.start(throwing_game, headlessConfig());
  } catch (const std::runtime_error&) {
    startup_threw = true;
  }
  assert(startup_threw);
  assert(throwing_counts.started == 1);
  assert(throwing_counts.shutdown == 1);
  assert(!throwing_game.contextBound());
  assert(!throwing_app.isRunning());

  RuntimeCounts first_module_counts;
  RuntimeCounts failing_module_counts;
  RuntimeCounts never_attached_counts;
  GameCounts attach_failure_game_counts;
  CountingGame attach_failure_game(attach_failure_game_counts);
  karma::app::EngineApp attach_failure_app;
  attach_failure_app.addRuntimeModule(
      std::make_unique<CountingModule>(first_module_counts));
  attach_failure_app.addRuntimeModule(
      std::make_unique<CountingModule>(failing_module_counts, true));
  attach_failure_app.addRuntimeModule(
      std::make_unique<CountingModule>(never_attached_counts));
  bool attach_threw = false;
  try {
    attach_failure_app.start(attach_failure_game, headlessConfig());
  } catch (const std::runtime_error&) {
    attach_threw = true;
  }
  assert(attach_threw);
  assert(first_module_counts.attached == 1 && first_module_counts.detached == 1);
  assert(failing_module_counts.attached == 1 && failing_module_counts.detached == 0);
  assert(never_attached_counts.attached == 0 && never_attached_counts.detached == 0);
  assert(attach_failure_game_counts.started == 0);
  assert(!attach_failure_game.contextBound());

  GameCounts fixed_stop_counts;
  karma::app::EngineApp fixed_stop_app;
  StopOnFixedGame fixed_stop_game(fixed_stop_app, fixed_stop_counts);
  auto fast_fixed_config = headlessConfig();
  fast_fixed_config.fixed_dt = 0.0001f;
  fixed_stop_app.start(fixed_stop_game, fast_fixed_config);
  std::this_thread::sleep_for(std::chrono::milliseconds(2));
  fixed_stop_app.tick();
  assert(fixed_stop_counts.fixed == 1);
  assert(fixed_stop_counts.updated == 0);
  assert(fixed_stop_counts.shutdown == 1);
}

}  // namespace

int main() {
  testGamepadRepeatScheduler();
  testStackedUiReleaseRouting();
  testConcurrentTypeIds();
  testEntityLivenessAndComponents();
  testSceneCycleRejection();
  testSystemGraphValidationAndOrder();
  testInputRepeatAndHeldModifiers();
  testInputFilteringAndGamepadBindings();
  testEngineValidationAndHeadlessLifecycle();
  return 0;
}
