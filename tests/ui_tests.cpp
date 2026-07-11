#include "karma/assets.h"
#include "karma/ui.h"

#include <algorithm>
#include <atomic>
#include <cassert>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <optional>
#include <string>

namespace karma::ui::detail {

struct SystemTestAccess {
  static void buildFrame(System& system,
                         float dt,
                         int logical_width,
                         int logical_height,
                         int framebuffer_width,
                         int framebuffer_height,
                         float scale_x,
                         float scale_y,
                         rendering::UIDrawData& output) {
    system.buildFrame(dt, logical_width, logical_height, framebuffer_width,
                      framebuffer_height, scale_x, scale_y, output);
  }

  static bool processEvent(System& system, const platform::Event& event) {
    return system.processEvent(event) != System::InputDisposition::Ignored;
  }

  static bool capturesAllInput(const System& system) {
    const System::InputCapture capture = system.inputCapture();
    return capture.keyboard && capture.pointer && capture.gamepad;
  }

  static platform::CursorShape cursorShape(const System& system) {
    return system.cursorShape();
  }
};

}  // namespace karma::ui::detail

namespace {

std::filesystem::path makeTempDirectory(std::string_view label) {
  static std::atomic<std::uint64_t> sequence{0u};
  const auto stamp =
      std::chrono::steady_clock::now().time_since_epoch().count();
  const std::filesystem::path path =
      std::filesystem::temp_directory_path() /
      (std::string(label) + "_" + std::to_string(stamp) + "_" +
       std::to_string(sequence.fetch_add(1u, std::memory_order_relaxed)));
  std::filesystem::create_directories(path);
  return path;
}

void writeTextFile(const std::filesystem::path& path, std::string_view text) {
  std::ofstream stream(path, std::ios::binary | std::ios::trunc);
  stream.write(text.data(), static_cast<std::streamsize>(text.size()));
  assert(static_cast<bool>(stream));
}

struct TempDirectory {
  std::filesystem::path path;
  ~TempDirectory() {
    std::error_code error;
    std::filesystem::remove_all(path, error);
  }
};

const karma::ui::AccessibilityNode* accessible(
    const karma::ui::System& system,
    karma::ui::ElementHandle element) {
  const auto& nodes = system.accessibilityTree().nodes;
  const auto found = std::find_if(nodes.begin(), nodes.end(), [&](const auto& node) {
    return node.element == element;
  });
  return found == nodes.end() ? nullptr : &*found;
}

bool containsVertexColor(const karma::rendering::UIDrawData& draw_data,
                         std::uint32_t rgba) {
  return std::any_of(draw_data.vertices.begin(), draw_data.vertices.end(),
                     [&](const auto& vertex) { return vertex.rgba == rgba; });
}

std::optional<karma::ui::AccessibilityBounds> vertexColorBounds(
    const karma::rendering::UIDrawData& draw_data,
    std::uint32_t rgba) {
  std::optional<karma::ui::AccessibilityBounds> bounds;
  for (const auto& vertex : draw_data.vertices) {
    if (vertex.rgba != rgba) continue;
    if (!bounds.has_value()) {
      bounds = {.x = vertex.x, .y = vertex.y};
      continue;
    }
    const float left = std::min(bounds->x, vertex.x);
    const float top = std::min(bounds->y, vertex.y);
    const float right = std::max(bounds->x + bounds->width, vertex.x);
    const float bottom = std::max(bounds->y + bounds->height, vertex.y);
    *bounds = {.x = left,
               .y = top,
               .width = right - left,
               .height = bottom - top};
  }
  return bounds;
}

bool hasAccessibilityAction(const karma::ui::AccessibilityNode& node,
                            karma::ui::AccessibilityAction action) {
  return std::find(node.actions.begin(), node.actions.end(), action) !=
         node.actions.end();
}

bool nearlyEqual(float left, float right, float epsilon = 0.01f) {
  return std::abs(left - right) <= epsilon;
}

void clickElement(karma::ui::System& ui, karma::ui::ElementHandle element) {
  using karma::platform::Event;
  using karma::platform::EventType;
  using karma::ui::detail::SystemTestAccess;

  const auto* node = accessible(ui, element);
  assert(node != nullptr);
  const auto bounds = node->bounds;
  const double x = bounds.x + bounds.width * 0.5;
  const double y = bounds.y + bounds.height * 0.5;
  assert(SystemTestAccess::processEvent(
      ui, Event{.type = EventType::MouseMove, .x = x, .y = y}));
  assert(SystemTestAccess::processEvent(
      ui, Event{.type = EventType::MouseButtonDown, .x = x, .y = y}));
  assert(SystemTestAccess::processEvent(
      ui, Event{.type = EventType::MouseButtonUp, .x = x, .y = y}));
}

void testValueSemantics() {
  using karma::ui::Value;
  Value root = Value::Object{{"enabled", true},
                             {"count", 3},
                             {"items", Value::Array{1, 2, 3}}};
  Value copy = root;
  copy.asObject()->at("items").asArray()->push_back(4);
  assert(root.asObject()->at("items").asArray()->size() == 3u);
  assert(copy.asObject()->at("items").asArray()->size() == 4u);
  assert(Value(2) == Value(2.0));
  assert(Value(std::uint64_t{std::numeric_limits<std::uint64_t>::max()}).type() ==
         Value::Type::Number);
  assert(Value(std::int64_t{9007199254740993LL}) != Value(9007199254740992.0));
  assert(!Value(9223372036854775808.0).asInteger().has_value());
  assert(Value("karma").truthy());
  assert(!Value{}.truthy());
}

void testNativeCursorShapes() {
  using karma::platform::CursorShape;
  using karma::platform::Event;
  using karma::platform::EventType;
  using karma::ui::detail::SystemTestAccess;

  karma::assets::AssetRegistry assets;
  const std::string document = R"JSON({
    format: 'karma.ui.document', version: 2,
    root: {
      type: 'body', layout: {mode: 'row', width: 220, height: 60, gap: 10},
      children: [
        {type: 'button', id: 'automatic', layout: {width: 90, height: 40},
         props: {text: 'Auto'}},
        {type: 'button', id: 'authored',
         layout: {width: 90, height: 40, cursor: 'crosshair'},
         props: {text: 'Aim'}},
      ],
    },
  })JSON";
  assert(assets.registerUiDocumentAsset(
      "ui/cursors", {.canonical_json_utf8 = document}));
  karma::ui::UiSystemConfig config;
  config.enabled = true;
  config.hot_reload = false;
  karma::ui::System ui(assets, nullptr, config);
  const auto opened = ui.open("ui/cursors");
  assert(opened);
  karma::rendering::UIDrawData draw_data;
  SystemTestAccess::buildFrame(ui, 0.0f, 220, 60, 220, 60, 1.0f, 1.0f,
                               draw_data);
  auto hover = [&](std::string_view id) {
    const auto* node = accessible(ui, ui.findById(opened.document, id));
    assert(node != nullptr);
    (void)SystemTestAccess::processEvent(
        ui, Event{.type = EventType::MouseMove,
                  .x = node->bounds.x + node->bounds.width * 0.5,
                  .y = node->bounds.y + node->bounds.height * 0.5});
  };
  hover("automatic");
  assert(SystemTestAccess::cursorShape(ui) == CursorShape::Pointer);
  hover("automatic");
  assert(SystemTestAccess::cursorShape(ui) == CursorShape::Pointer);
  hover("authored");
  assert(SystemTestAccess::cursorShape(ui) == CursorShape::Crosshair);
  (void)SystemTestAccess::processEvent(
      ui, Event{.type = EventType::MouseMove, .x = -10.0, .y = -10.0});
  assert(SystemTestAccess::cursorShape(ui) == CursorShape::Default);
}

void testRetainedFrameWorkDiagnostics() {
  using karma::ui::detail::SystemTestAccess;
  karma::assets::AssetRegistry assets;
  const std::string document = R"JSON({
    format: 'karma.ui.document', version: 2,
    model: {progress: 0.25},
    root: {
      type: 'body', layout: {mode: 'column', width: 240, height: 100, gap: 8},
      children: [
        {type: 'text', props: {text: 'Retained diagnostics'}},
        {type: 'progress', id: 'progress', layout: {width: 220, height: 20},
         props: {value: {bind: 'progress'}, min: 0, max: 1}},
      ],
    },
  })JSON";
  assert(assets.registerUiDocumentAsset(
      "ui/retained-work", {.canonical_json_utf8 = document}));
  karma::ui::UiSystemConfig config;
  config.enabled = true;
  config.hot_reload = false;
  karma::ui::System ui(assets, nullptr, config);
  const auto opened = ui.open("ui/retained-work");
  assert(opened);

  karma::rendering::UIDrawData draw_data;
  auto frame = [&] {
    SystemTestAccess::buildFrame(ui, 0.0f, 240, 100, 240, 100, 1.0f, 1.0f,
                                 draw_data);
    assert(karma::rendering::validateUIDrawData(draw_data));
    return ui.frameDiagnostics();
  };
  const auto first = frame();
  assert(first.rebuilt_fragments > 0u && first.laid_out_nodes > 0u);

  const auto idle = frame();
  assert(idle.reconciled_nodes == 0u);
  assert(idle.restyled_nodes == 0u);
  assert(idle.laid_out_nodes == 0u);
  assert(idle.accessibility_nodes == 0u);
  assert(idle.rebuilt_fragments == 0u);

  assert(ui.set(opened.document, "progress", 0.75));
  const auto targeted = frame();
  assert(targeted.reconciled_nodes == 1u);
  assert(targeted.restyled_nodes == 0u);
  assert(targeted.laid_out_nodes == 0u);
  assert(targeted.rebuilt_fragments > 0u);
  assert(targeted.output_commands <= 3u);

  assert(ui.set(opened.document, "progress", 0.75));
  const auto same_value = frame();
  assert(same_value.reconciled_nodes == 0u);
  assert(same_value.restyled_nodes == 0u);
  assert(same_value.laid_out_nodes == 0u);
  assert(same_value.rebuilt_fragments == 0u);

  assert(ui.setMany(opened.document,
                    {{"progress", 0.75}, {"unused.telemetry", 1}}));
  const auto mixed_same_value = frame();
  assert(mixed_same_value.reconciled_nodes == 0u);
  assert(mixed_same_value.restyled_nodes == 0u);
  assert(mixed_same_value.laid_out_nodes == 0u);
  assert(mixed_same_value.rebuilt_fragments == 0u);
}

void testActiveMotionNodeTracking() {
  using karma::ui::detail::SystemTestAccess;

  karma::assets::AssetRegistry assets;
  const std::string theme = R"JSON({
    format: 'karma.ui.theme', version: 2,
    motions: {
      pulse: {
        duration_ms: 1000, easing: 'linear', iterations: 'infinite',
        direction: 'alternate',
        keyframes: [
          {at: 0, appearance: {box: {opacity: 0.25}}},
          {at: 1, appearance: {box: {opacity: 1}}},
        ],
      },
    },
    styles: {
      pulse: {
        layout: {width: 40, height: 20},
        appearance: {
          box: {background_color: '#ff3344'}, motion: 'pulse',
        },
      },
      inert: {
        layout: {width: 40, height: 20},
        appearance: {box: {background_color: '#334455'}},
      },
    },
  })JSON";
  const std::string document = R"JSON({
    format: 'karma.ui.document', version: 2,
    themes: [{asset: 'ui/active-motion-theme'}],
    model: {show_pulse: true},
    root: {
      type: 'body', layout: {mode: 'row', width: 240, height: 60, gap: 4},
      children: [
        {type: 'panel', id: 'pulse', styles: ['pulse'],
         when: {bind: 'show_pulse'}},
        {type: 'panel', styles: ['inert']},
        {type: 'panel', styles: ['inert']},
        {type: 'panel', styles: ['inert']},
        {type: 'panel', styles: ['inert']},
      ],
    },
  })JSON";
  assert(assets.registerUiThemeAsset(
      "ui/active-motion-theme", {.canonical_json_utf8 = theme}));
  assert(assets.registerUiDocumentAsset(
      "ui/active-motion",
      {.canonical_json_utf8 = document,
       .dependencies = {{karma::assets::UiAssetDependencyKind::UiTheme,
                         "ui/active-motion-theme"}}}));

  karma::ui::UiSystemConfig config;
  config.enabled = true;
  config.hot_reload = false;
  karma::ui::System ui(assets, nullptr, config);
  const auto opened = ui.open("ui/active-motion");
  assert(opened);

  karma::rendering::UIDrawData draw_data;
  auto frame = [&](float dt) {
    SystemTestAccess::buildFrame(ui, dt, 240, 60, 240, 60, 1.0f, 1.0f,
                                 draw_data);
    assert(karma::rendering::validateUIDrawData(draw_data));
    return ui.frameDiagnostics();
  };
  (void)frame(0.1f);
  const auto running = frame(0.1f);
  assert(running.advanced_motion_nodes == 1u);
  assert(running.restyled_nodes == 0u);
  assert(running.laid_out_nodes == 0u);
  assert(running.rebuilt_fragments > 0u);

  // Conditional release must purge the raw node pointer from both active
  // registries; showing it again restyles and re-registers the same track.
  assert(ui.set(opened.document, "show_pulse", false));
  const auto hidden = frame(0.1f);
  assert(hidden.advanced_motion_nodes == 0u);
  assert(ui.set(opened.document, "show_pulse", true));
  const auto restored = frame(0.1f);
  assert(restored.advanced_motion_nodes == 1u);

  // Reduced motion resolves the active infinite animation once and leaves no
  // recurring traversal or fragment rebuild behind.
  ui.setMotionScale(0.0f);
  const auto reduced = frame(0.1f);
  assert(reduced.advanced_motion_nodes == 0u);
  assert(reduced.rebuilt_fragments > 0u);
  const auto reduced_idle = frame(0.1f);
  assert(reduced_idle.advanced_motion_nodes == 0u);
  assert(reduced_idle.restyled_nodes == 0u);
  assert(reduced_idle.laid_out_nodes == 0u);
  assert(reduced_idle.rebuilt_fragments == 0u);
}

void testRetainedRuntimeChildOrder() {
  using karma::platform::Event;
  using karma::platform::EventType;
  using karma::ui::detail::SystemTestAccess;

  karma::assets::AssetRegistry assets;
  const std::string document = R"JSON({
    format: 'karma.ui.document', version: 2,
    root: {
      type: 'body', layout: {mode: 'overlay', width: 180, height: 100},
      children: [
        {type: 'window', id: 'first',
         props: {position: [10, 10], size: [120, 70], z: 1,
                 title: 'First', closable: false, collapsible: false,
                 resizable: false}},
        {type: 'window', id: 'second',
         props: {position: [10, 10], size: [120, 70], z: 2,
                 title: 'Second', closable: false, collapsible: false,
                 resizable: false}},
      ],
    },
  })JSON";
  assert(assets.registerUiDocumentAsset(
      "ui/retained-child-order", {.canonical_json_utf8 = document}));
  karma::ui::UiSystemConfig config;
  config.enabled = true;
  config.hot_reload = false;
  karma::ui::System ui(assets, nullptr, config);
  const auto opened = ui.open("ui/retained-child-order");
  assert(opened);
  const auto first = ui.findById(opened.document, "first");
  const auto second = ui.findById(opened.document, "second");
  assert(first && second);

  int first_down = 0;
  int second_down = 0;
  const auto first_listener = ui.on(
      first, karma::ui::EventType::PointerDown,
      [&](karma::ui::Event&) { ++first_down; });
  const auto second_listener = ui.on(
      second, karma::ui::EventType::PointerDown,
      [&](karma::ui::Event&) { ++second_down; });
  assert(first_listener && second_listener);

  karma::rendering::UIDrawData draw_data;
  auto build = [&] {
    SystemTestAccess::buildFrame(ui, 0.0f, 180, 100, 180, 100, 1.0f, 1.0f,
                                 draw_data);
  };
  auto press_overlap = [&] {
    (void)SystemTestAccess::processEvent(
        ui, Event{.type = EventType::MouseMove, .x = 50.0, .y = 50.0});
    assert(SystemTestAccess::processEvent(
        ui, Event{.type = EventType::MouseButtonDown, .x = 50.0, .y = 50.0}));
    assert(SystemTestAccess::processEvent(
        ui, Event{.type = EventType::MouseButtonUp, .x = 50.0, .y = 50.0}));
  };

  build();
  press_overlap();
  assert(first_down == 0 && second_down == 1);
  assert(ui.bringToFront(first));
  build();
  press_overlap();
  assert(first_down == 1 && second_down == 1);
}

void testVerticalSliderInput() {
  using karma::platform::Event;
  using karma::platform::EventType;
  using karma::ui::detail::SystemTestAccess;

  karma::assets::AssetRegistry assets;
  const std::string document = R"JSON({
    format: 'karma.ui.document', version: 2,
    model: {volume: 0},
    root: {
      type: 'body', layout: {mode: 'overlay', width: 100, height: 160},
      children: [{
        type: 'slider', id: 'vertical',
        layout: {position: [20, 10], width: 24, height: 120},
        props: {
          orientation: 'vertical', min: 0, max: 1, step: 0.1,
          value: {bind: 'volume', mode: 'two_way'},
        },
      }],
    },
  })JSON";
  assert(assets.registerUiDocumentAsset(
      "ui/vertical-slider", {.canonical_json_utf8 = document}));
  karma::ui::UiSystemConfig config;
  config.enabled = true;
  config.hot_reload = false;
  karma::ui::System ui(assets, nullptr, config);
  const auto opened = ui.open("ui/vertical-slider");
  assert(opened);
  karma::rendering::UIDrawData draw_data;
  SystemTestAccess::buildFrame(ui, 0.0f, 100, 160, 100, 160, 1.0f, 1.0f,
                               draw_data);
  const auto slider = ui.findById(opened.document, "vertical");
  const auto* semantic = accessible(ui, slider);
  assert(semantic != nullptr);
  const double x = semantic->bounds.x + semantic->bounds.width * 0.5;
  const double y = semantic->bounds.y + semantic->bounds.height * 0.1;
  assert(SystemTestAccess::processEvent(
      ui, Event{.type = EventType::MouseMove, .x = x, .y = y}));
  assert(SystemTestAccess::processEvent(
      ui, Event{.type = EventType::MouseButtonDown, .x = x, .y = y}));
  assert(SystemTestAccess::processEvent(
      ui, Event{.type = EventType::MouseButtonUp, .x = x, .y = y}));
  assert(ui.get(opened.document, "volume")->asNumber().value_or(0.0) >= 0.8);

  assert(ui.focus(slider));
  const double before = ui.get(opened.document, "volume")->asNumber().value();
  assert(SystemTestAccess::processEvent(
      ui, Event{.type = EventType::KeyDown, .key = karma::platform::Key::Down}));
  const double after = ui.get(opened.document, "volume")->asNumber().value();
  assert(after < before);
}

void testJsonPropMappingsAndAxisScrolling() {
  using karma::platform::Event;
  using karma::platform::EventType;
  using karma::ui::detail::SystemTestAccess;

  karma::assets::AssetRegistry assets;
  const std::string document = R"JSON({
    format: 'karma.ui.document', version: 2,
    root: {
      type: 'body', layout: {mode: 'overlay', width: 240, height: 180},
      children: [
        {
          type: 'button', id: 'underlay',
          layout: {position: [8, 8], width: 100, height: 36},
          props: {text: 'Underlay'}, on: {click: 'underlay-click'},
        },
        {
          type: 'panel', id: 'pointer-shield',
          props: {position: [8, 8], size: [100, 36], z: 10,
                  pointer_events: 'none'},
          appearance: {box: {background_color: '#ff0000'}},
        },
        {
          type: 'panel', id: 'axis-scroller',
          layout: {position: [8, 56], width: 100, height: 80},
          props: {scroll_x: 'hidden', scroll_y: 'auto'},
          semantics: {role: 'scroll', label: 'Axis scroller'},
          children: [{
            type: 'panel', id: 'axis-content',
            layout: {width: 220, height: 240, shrink: 0},
          }],
        },
        {
          type: 'panel', id: 'skinned-axis-scroller',
          layout: {position: [126, 56], width: 106, height: 80},
          props: {scroll_x: 'auto', scroll_y: 'auto'},
          appearance: {parts: {
            vertical_track: {
              box: {background_color: '#112233'}, metrics: {width: 14},
            },
            horizontal_track: {
              box: {background_color: '#445566'}, metrics: {height: 8},
            },
            vertical_thumb: {
              box: {background_color: '#778899'}, metrics: {min_length: 16},
            },
            horizontal_thumb: {
              box: {background_color: '#aabbcc'}, metrics: {min_length: 12},
            },
          }},
          children: [{
            type: 'panel', layout: {width: 220, height: 240, shrink: 0},
          }],
        },
        {
          type: 'panel', id: 'horizontal-only-scroller',
          layout: {position: [8, 144], width: 100, height: 30},
          props: {scroll_x: 'auto', scroll_y: 'hidden'},
          children: [{
            type: 'panel', layout: {width: 220, height: 18, shrink: 0},
            appearance: {box: {background_color: '#123456'}},
          }],
        },
      ],
    },
  })JSON";
  assert(assets.registerUiDocumentAsset(
      "ui/prop-mappings", {.canonical_json_utf8 = document}));
  karma::ui::UiSystemConfig config;
  config.enabled = true;
  config.hot_reload = false;
  karma::ui::System ui(assets, nullptr, config);
  const auto opened = ui.open("ui/prop-mappings");
  assert(opened);
  int clicks = 0;
  assert(ui.onAction(opened.document, "underlay-click",
                     [&](const karma::ui::ActionEvent&) { ++clicks; }));

  karma::rendering::UIDrawData draw_data;
  const auto build = [&] {
    SystemTestAccess::buildFrame(ui, 0.0f, 240, 180, 240, 180, 1.0f, 1.0f,
                                 draw_data);
  };
  build();
  const auto vertical_track = vertexColorBounds(draw_data, 0xff332211u);
  const auto horizontal_track = vertexColorBounds(draw_data, 0xff665544u);
  const auto horizontal_content_before =
      vertexColorBounds(draw_data, 0xff563412u);
  assert(vertical_track.has_value() && horizontal_track.has_value());
  assert(horizontal_content_before.has_value());
  assert(nearlyEqual(vertical_track->width, 14.0f));
  assert(nearlyEqual(horizontal_track->height, 8.0f));
  clickElement(ui, ui.findById(opened.document, "underlay"));
  assert(clicks == 1);

  const auto scroller = ui.findById(opened.document, "axis-scroller");
  const auto* semantic = accessible(ui, scroller);
  assert(semantic != nullptr);
  assert(semantic->role == karma::ui::AccessibilityRole::Scroll);
  assert(semantic->name == "Axis scroller");
  assert(semantic->scroll_max_x.value_or(-1.0) == 0.0);
  assert(semantic->scroll_max_y.value_or(0.0) > 100.0);
  assert(ui.scrollTo(scroller, 20.0f, 20.0f));
  build();
  assert(accessible(ui, scroller)->scroll_x.value_or(-1.0) == 0.0);
  assert(accessible(ui, scroller)->scroll_y.value_or(0.0) == 20.0);

  const auto bounds = semantic->bounds;
  assert(SystemTestAccess::processEvent(
      ui, Event{.type = EventType::MouseMove,
                .x = bounds.x + 10.0,
                .y = bounds.y + 10.0}));
  assert(SystemTestAccess::processEvent(
      ui, Event{.type = EventType::MouseScroll,
                .scrollY = -1.0}));
  build();
  assert(accessible(ui, scroller)->scroll_y.value_or(0.0) > 0.0);

  const auto skinned_scroller =
      ui.findById(opened.document, "skinned-axis-scroller");
  assert(skinned_scroller);
  const auto skinned_bounds = accessible(ui, skinned_scroller)->bounds;
  assert(SystemTestAccess::processEvent(
      ui, Event{.type = EventType::MouseMove,
                .x = skinned_bounds.x + 10.0,
                .y = skinned_bounds.y + 10.0}));
  assert(SystemTestAccess::processEvent(
      ui, Event{.type = EventType::MouseScroll, .scrollX = -1.0}));
  build();
  assert(accessible(ui, skinned_scroller)->scroll_x.value_or(0.0) > 0.0);
  assert(accessible(ui, skinned_scroller)->scroll_y.value_or(-1.0) == 0.0);

  assert(ui.scrollTo(skinned_scroller, 0.0f, 0.0f));
  build();
  int shifted_wheel_events = 0;
  const auto shifted_wheel_listener = ui.on(
      skinned_scroller, karma::ui::EventType::Scroll,
      [&](karma::ui::Event& event) {
        ++shifted_wheel_events;
        assert(event.modifiers.shift);
        assert(event.delta_x == 0.0);
        assert(event.delta_y == -1.0);
      });
  assert(shifted_wheel_listener);
  assert(SystemTestAccess::processEvent(
      ui, Event{.type = EventType::MouseScroll,
                .mods = {.shift = true},
                .scrollY = -1.0}));
  build();
  assert(shifted_wheel_events == 1);
  assert(accessible(ui, skinned_scroller)->scroll_x.value_or(0.0) > 0.0);
  assert(accessible(ui, skinned_scroller)->scroll_y.value_or(-1.0) == 0.0);

  const auto horizontal_only =
      ui.findById(opened.document, "horizontal-only-scroller");
  assert(horizontal_only);
  const auto horizontal_bounds = accessible(ui, horizontal_only)->bounds;
  assert(SystemTestAccess::processEvent(
      ui, Event{.type = EventType::MouseMove,
                .x = horizontal_bounds.x + 10.0,
                .y = horizontal_bounds.y + 10.0}));
  build();  // Settle hover before measuring wheel placement work.
  assert(SystemTestAccess::processEvent(
      ui, Event{.type = EventType::MouseScroll, .scrollY = -1.0}));
  const auto horizontal_wheel_work = [&] {
    build();
    return ui.frameDiagnostics();
  }();
  assert(accessible(ui, horizontal_only)->scroll_x.value_or(0.0) > 0.0);
  assert(accessible(ui, horizontal_only)->scroll_y.value_or(-1.0) == 0.0);
  const auto horizontal_content_after =
      vertexColorBounds(draw_data, 0xff563412u);
  assert(horizontal_content_after.has_value());
  assert(horizontal_content_after->x < horizontal_content_before->x);
  assert(horizontal_wheel_work.reconciled_nodes == 0u);
  assert(horizontal_wheel_work.restyled_nodes == 0u);
  assert(horizontal_wheel_work.laid_out_nodes == 0u);
  assert(horizontal_wheel_work.rebuilt_fragments > 0u);
}

void testRtlHorizontalSliderInput() {
  using karma::platform::Event;
  using karma::platform::EventType;
  using karma::ui::detail::SystemTestAccess;

  karma::assets::AssetRegistry assets;
  const std::string document = R"JSON({
    format: 'karma.ui.document', version: 2,
    model: {value: 0},
    root: {
      type: 'body', layout: {mode: 'overlay', width: 180, height: 60},
      appearance: {text: {direction: 'rtl'}},
      children: [{
        type: 'slider', id: 'rtl-slider',
        layout: {position: [20, 20], width: 120, height: 20},
        props: {value: {bind: 'value', mode: 'two_way'},
                min: 0, max: 1, step: 0.1},
      }],
    },
  })JSON";
  assert(assets.registerUiDocumentAsset(
      "ui/rtl-slider", {.canonical_json_utf8 = document}));
  karma::ui::UiSystemConfig config;
  config.enabled = true;
  config.hot_reload = false;
  karma::ui::System ui(assets, nullptr, config);
  const auto opened = ui.open("ui/rtl-slider");
  assert(opened);
  karma::rendering::UIDrawData draw_data;
  SystemTestAccess::buildFrame(ui, 0.0f, 180, 60, 180, 60, 1.0f, 1.0f,
                               draw_data);
  const auto* slider = accessible(
      ui, ui.findById(opened.document, "rtl-slider"));
  assert(slider != nullptr);
  const double x = slider->bounds.x + slider->bounds.width * 0.1;
  const double y = slider->bounds.y + slider->bounds.height * 0.5;
  assert(SystemTestAccess::processEvent(
      ui, Event{.type = EventType::MouseMove, .x = x, .y = y}));
  assert(SystemTestAccess::processEvent(
      ui, Event{.type = EventType::MouseButtonDown, .x = x, .y = y}));
  assert(SystemTestAccess::processEvent(
      ui, Event{.type = EventType::MouseButtonUp, .x = x, .y = y}));
  assert(ui.get(opened.document, "value")->asNumber().value_or(0.0) >= 0.8);

  const auto slider_handle = ui.findById(opened.document, "rtl-slider");
  assert(slider_handle);
  assert(ui.set(opened.document, "value", 0.2));
  assert(ui.focus(slider_handle));
  assert(SystemTestAccess::processEvent(
      ui, Event{.type = EventType::KeyDown,
                .key = karma::platform::Key::Right}));
  const double keyboard_value =
      ui.get(opened.document, "value")->asNumber().value_or(-1.0);
  assert(std::abs(keyboard_value - 0.3) < 0.001);

  assert(ui.set(opened.document, "value", 0.4));
  assert(SystemTestAccess::processEvent(
      ui, Event{.type = EventType::GamepadButtonDown,
                .gamepad = 0,
                .gamepadButton = karma::platform::GamepadButton::DpadRight}));
  const double gamepad_value =
      ui.get(opened.document, "value")->asNumber().value_or(-1.0);
  assert(std::abs(gamepad_value - 0.5) < 0.001);
}

void testDocumentsAndGenerationalHandles() {
  karma::assets::AssetRegistry assets;
  const std::string theme = R"JSON({
    "format": "karma.ui.theme",
    "version": 2,
    "variables": { "accent": "#4488ff" },
    "defaults": {
      "body": { "appearance": { "text": { "color": "white" } } }
    },
    "styles": {
      "primary": {
        "layout": { "width": 180 },
        "appearance": {
          "box": { "background_color": { "var": "accent" } }
        }
      },
      "conditional-text": {
        "appearance": { "text": { "color": "#ffffff" } }
      }
    }
  })JSON";
  assert(assets.registerUiThemeAsset(
      "ui/theme", {.canonical_json_utf8 = theme,
                   .content_hash = karma::assets::hashString(theme)}));

  const std::string document = R"JSON({
    "format": "karma.ui.document",
    "version": 2,
    "themes": [{ "asset": "ui/theme" }],
    "root": {
      "type": "body",
      "id": "menu",
      "styles": ["screen"],
      "children": [
        {
          "type": "button",
          "id": "play",
          "styles": ["primary"],
          "props": { "text": { "bind": "title" } },
          "on": { "click": "play" }
        },
        {
          "type": "toggle",
          "id": "music",
          "props": {
            "value": { "bind": "settings.music", "mode": "two_way" }
          }
        },
        {
          "type": "div",
          "id": "conditional",
          "when": { "bind": "show" },
          "children": [{
            "type": "text",
            "styles": ["conditional-text"],
            "props": { "text": "Visible" }
          }]
        },
        {
          "type": "repeat",
          "props": {
            "items": { "bind": "items" },
            "item": "item",
            "key": { "expr": "item.id" },
            "template": {
              "type": "button",
              "styles": ["row"],
              "props": { "text": { "bind": "item.name" } }
            }
          }
        }
      ]
    }
  })JSON";
  assert(assets.registerUiDocumentAsset(
      "ui/menu", {.canonical_json_utf8 = document,
                  .dependencies = {{karma::assets::UiAssetDependencyKind::UiTheme,
                                    "ui/theme"}},
                  .content_hash = karma::assets::hashString(document)}));

  karma::ui::UiSystemConfig config;
  config.enabled = true;
  config.hot_reload = false;
  karma::ui::System ui(assets, nullptr, config);
  auto opened = ui.open("ui/menu", {.layer = 100, .visible = true, .modal = true});
  assert(opened);
  assert(opened.diagnostics.empty());
  const auto menu = opened.document;
  const auto play = ui.findById(menu, "play");
  assert(play);
  assert(ui.addClass(play, "focused-look"));
  assert(ui.removeClass(play, "focused-look"));
  assert(ui.setText(play, "Play"));

  assert(ui.set(menu, "title", "Play Game"));
  assert(ui.get(menu, "title") == karma::ui::Value("Play Game"));
  assert(ui.set(menu, "settings.music", true));
  assert(ui.get(menu, "settings.music")->asBoolean().value_or(false));
  assert(ui.set(menu, "show", false));
  assert(!ui.findById(menu, "conditional"));
  assert(ui.set(menu, "show", true));
  const auto conditional = ui.findById(menu, "conditional");
  assert(conditional);

  assert(!ui.setMany(
      menu,
      {{"title", "Discarded"}, {"show", false}, {"invalid.", true}}));
  assert(ui.get(menu, "title") == karma::ui::Value("Play Game"));
  assert(ui.get(menu, "show") == karma::ui::Value(true));
  assert(ui.findById(menu, "conditional") == conditional);

  assert(ui.setMany(menu,
                    {{"show", true},
                     {"show", false},
                     {"settings.music", false}}));
  assert(ui.get(menu, "show") == karma::ui::Value(false));
  assert(!ui.get(menu, "settings.music")->truthy());
  assert(!ui.findById(menu, "conditional"));
  assert(!ui.setText(conditional, "stale"));

  assert(ui.setMany(menu,
                    {{"show", false},
                     {"show", true},
                     {"title", "Batched Play Game"}}));
  assert(ui.get(menu, "show") == karma::ui::Value(true));
  assert(ui.get(menu, "title") == karma::ui::Value("Batched Play Game"));
  assert(ui.findById(menu, "conditional"));

  karma::ui::Value::Array items;
  items.emplace_back(karma::ui::Value::Object{{"id", 10}, {"name", "First"}});
  items.emplace_back(karma::ui::Value::Object{{"id", 20}, {"name", "Second"}});
  assert(ui.set(menu, "items", std::move(items)));

  int action_count = 0;
  const auto listener = ui.onAction(menu, "play", [&](const karma::ui::ActionEvent&) {
    ++action_count;
  });
  assert(listener);
  assert(ui.removeListener(listener));
  assert(!ui.removeListener(listener));

  assert(ui.hide(menu));
  assert(ui.show(menu));
  assert(ui.setModal(menu, false));
  assert(ui.close(menu));
  assert(!ui.close(menu));
  assert(!ui.findById(menu, "play"));
  assert(!ui.setText(play, "stale"));

  const std::string invalid = R"JSON({
    "format": "karma.ui.document",
    "version": 2,
    "root": { "type": "script" }
  })JSON";
  assert(assets.registerUiDocumentAsset(
      "ui/invalid", {.canonical_json_utf8 = invalid,
                     .content_hash = karma::assets::hashString(invalid)}));
  const auto failed = ui.open("ui/invalid");
  assert(!failed);
  assert(!failed.diagnostics.empty());
}

void testDocumentControllerOwnership() {
  using karma::ui::detail::SystemTestAccess;

  karma::assets::AssetRegistry assets;
  const std::string document = R"JSON({
    "format": "karma.ui.document",
    "version": 2,
    "root": {
      "type": "body",
      "layout": {
        "mode": "row",
        "width": 220,
        "height": 60,
        "gap": 8
      },
      "children": [
        {
          "type": "button",
          "id": "fire",
          "layout": { "width": 100, "height": 40 },
          "props": { "text": "Fire" },
          "on": { "click": "fire" }
        },
        {
          "type": "button",
          "id": "cancel",
          "layout": { "width": 100, "height": 40 },
          "props": { "text": "Cancel" },
          "on": { "click": "cancel" }
        }
      ]
    }
  })JSON";
  assert(assets.registerUiDocumentAsset(
      "ui/controller", {.canonical_json_utf8 = document}));

  karma::ui::UiSystemConfig config;
  config.enabled = true;
  config.hot_reload = false;
  karma::ui::System ui(assets, nullptr, config);

  auto opened = ui.openController("ui/controller");
  assert(opened && opened.diagnostics.empty());
  assert(opened.controller.setMany(
      {{"controller.batch", 1}, {"controller.batch", 2}}));
  assert(opened.controller.get("controller.batch") == karma::ui::Value(2));
  const auto original_document = opened.controller.handle();
  int fire_count = 0;
  int cancel_count = 0;
  karma::ui::DocumentController::ActionMap actions;
  actions.emplace("fire", [&](const karma::ui::ActionEvent&) { ++fire_count; });
  actions.emplace("cancel", [&](const karma::ui::ActionEvent&) { ++cancel_count; });
  assert(opened.controller.bindActions(std::move(actions)));

  karma::rendering::UIDrawData draw_data;
  SystemTestAccess::buildFrame(ui, 0.0f, 240, 100, 240, 100, 1.0f, 1.0f,
                               draw_data);
  clickElement(ui, opened.controller.findById("fire"));
  clickElement(ui, opened.controller.findById("cancel"));
  assert(fire_count == 1 && cancel_count == 1);

  karma::ui::DocumentController moved = std::move(opened.controller);
  assert(!opened.controller.valid());
  assert(moved.valid() && moved.handle() == original_document);

  auto replacement = ui.openController("ui/controller");
  assert(replacement);
  const auto replaced_document = replacement.controller.handle();
  replacement.controller = std::move(moved);
  assert(!moved.valid());
  assert(replacement.controller.handle() == original_document);
  assert(!ui.findById(replaced_document, "fire"));

  clickElement(ui, replacement.controller.findById("fire"));
  assert(fire_count == 2);

  const auto released_document = replacement.controller.release();
  assert(released_document == original_document);
  assert(!replacement.controller.valid());
  const auto released_fire = ui.findById(released_document, "fire");
  assert(released_fire);
  clickElement(ui, released_fire);
  assert(fire_count == 3);
  assert(ui.close(released_document));

  karma::ui::DocumentHandle scoped_document;
  {
    auto scoped = ui.openController("ui/controller");
    assert(scoped);
    scoped_document = scoped.controller.handle();
    assert(scoped.controller.findById("fire"));
  }
  assert(!ui.findById(scoped_document, "fire"));

  karma::ui::DocumentController orphaned;
  {
    karma::ui::System short_lived(assets, nullptr, config);
    auto short_opened = short_lived.openController("ui/controller");
    assert(short_opened);
    orphaned = std::move(short_opened.controller);
    assert(orphaned.valid());
  }
  assert(!orphaned.valid());
  assert(!orphaned.close());
}

void testNativeDemoPackage() {
  karma::assets::AssetRegistry assets;
  const std::filesystem::path root =
      std::filesystem::path(__FILE__).parent_path().parent_path();
  std::string diagnostic;
  auto package = karma::assets::importAssetPackage(
      assets, root / "examples/assets/ui/native_menu", &diagnostic);
  if (!package.has_value() || !diagnostic.empty()) {
    std::cerr << "native UI demo package import failed: " << diagnostic << '\n';
  }
  assert(package.has_value() && diagnostic.empty());
  assert(assets.findUiDocumentAsset("ui/demo/main_menu") != nullptr);
  assert(assets.findUiThemeAsset("ui/demo/theme") != nullptr);
  assert(assets.findFontAsset("ui/demo/font") != nullptr);
  assert(assets.findSvgAsset("ui/demo/icon") != nullptr);
  assert(karma::assets::unloadAssetPackage(assets, *package));
}

void testPointerHoverRouting() {
  using karma::platform::Event;
  using karma::platform::EventType;
  using karma::ui::detail::SystemTestAccess;

  karma::assets::AssetRegistry assets;
  const std::string lower_theme = R"JSON({
    "format": "karma.ui.theme",
    "version": 2,
    "defaults": {
      "body": { "layout": { "width": "100%", "height": "100%" } }
    },
    "styles": {
      "lower-clip": {
        "layout": { "width": 80, "height": 60, "overflow": "hidden" }
      },
      "lower-target": {
        "layout": { "width": 120, "height": 60, "cursor": "move" },
        "appearance": {
          "box": { "background_color": "#220000" },
          "states": {
            "hover": { "box": { "background_color": "#ff00ff" } }
          }
        }
      }
    }
  })JSON";
  const std::string lower_document = R"JSON({
    "format": "karma.ui.document",
    "version": 2,
    "themes": [{ "asset": "ui/hover-lower-theme" }],
    "root": {
      "type": "body",
      "id": "lower-root",
      "children": [{
        "type": "div",
        "id": "lower-clip",
        "styles": ["lower-clip"],
        "children": [{
          "type": "div",
          "id": "lower-target",
          "styles": ["lower-target"]
        }]
      }]
    }
  })JSON";
  const std::string upper_theme = R"JSON({
    "format": "karma.ui.theme",
    "version": 2,
    "defaults": {
      "body": { "layout": { "width": "100%", "height": "100%" } }
    },
    "styles": {
      "upper-target": {
        "layout": { "width": 80, "height": 60, "cursor": "crosshair" },
        "appearance": {
          "box": { "background_color": "#002200" },
          "states": {
            "hover": { "box": { "background_color": "#00ff00" } }
          }
        }
      }
    }
  })JSON";
  const std::string upper_document = R"JSON({
    "format": "karma.ui.document",
    "version": 2,
    "themes": [{ "asset": "ui/hover-upper-theme" }],
    "root": {
      "type": "body",
      "id": "upper-root",
      "children": [{
        "type": "div",
        "id": "upper-target",
        "styles": ["upper-target"]
      }]
    }
  })JSON";
  assert(assets.registerUiThemeAsset(
      "ui/hover-lower-theme", {.canonical_json_utf8 = lower_theme}));
  assert(assets.registerUiDocumentAsset(
      "ui/hover-lower",
      {.canonical_json_utf8 = lower_document,
       .dependencies = {{karma::assets::UiAssetDependencyKind::UiTheme,
                         "ui/hover-lower-theme"}}}));
  assert(assets.registerUiThemeAsset(
      "ui/hover-upper-theme", {.canonical_json_utf8 = upper_theme}));
  assert(assets.registerUiDocumentAsset(
      "ui/hover-upper",
      {.canonical_json_utf8 = upper_document,
       .dependencies = {{karma::assets::UiAssetDependencyKind::UiTheme,
                         "ui/hover-upper-theme"}}}));

  karma::ui::UiSystemConfig config;
  config.enabled = true;
  config.hot_reload = false;
  karma::ui::System ui(assets, nullptr, config);
  const auto lower_opened = ui.open("ui/hover-lower", {.layer = 10});
  assert(lower_opened);
  const auto lower_root = ui.findById(lower_opened.document, "lower-root");
  const auto lower_clip = ui.findById(lower_opened.document, "lower-clip");
  const auto lower_target = ui.findById(lower_opened.document, "lower-target");
  assert(lower_root && lower_clip && lower_target);

  karma::rendering::UIDrawData draw_data;
  SystemTestAccess::buildFrame(ui, 0.0f, 200, 120, 200, 120, 1.0f, 1.0f,
                               draw_data);
  const auto target_bounds = accessible(ui, lower_target)->bounds;
  const auto clip_bounds = accessible(ui, lower_clip)->bounds;
  assert(target_bounds.width > clip_bounds.width);

  karma::ui::ElementHandle entered{};
  karma::ui::ElementHandle moved{};
  karma::ui::ElementHandle left{};
  int target_leave_count = 0;
  std::vector<int> transfer_order;
  const auto lower_enter = ui.on(
      lower_root, karma::ui::EventType::PointerEnter,
      [&](karma::ui::Event& event) { entered = event.target; }, {.capture = true});
  const auto lower_move = ui.on(
      lower_root, karma::ui::EventType::PointerMove,
      [&](karma::ui::Event& event) { moved = event.target; });
  const auto lower_leave = ui.on(
      lower_root, karma::ui::EventType::PointerLeave,
      [&](karma::ui::Event& event) {
        left = event.target;
        if (event.target == lower_target) {
          ++target_leave_count;
          transfer_order.push_back(1);
        }
      },
      {.capture = true});
  assert(lower_enter && lower_move && lower_leave);

  const double inside_x = target_bounds.x + 20.0;
  const double inside_y = target_bounds.y + 20.0;
  assert(SystemTestAccess::processEvent(
      ui, Event{.type = EventType::MouseMove, .x = inside_x, .y = inside_y}));
  assert(entered == lower_target);
  assert(moved == lower_root);
  assert(SystemTestAccess::cursorShape(ui) ==
         karma::platform::CursorShape::Move);
  SystemTestAccess::buildFrame(ui, 0.0f, 200, 120, 200, 120, 1.0f, 1.0f,
                               draw_data);
  assert(containsVertexColor(draw_data, 0xffff00ffu));

  // The child extends past its overflow-hidden parent, but that clipped portion
  // must not retain hover or receive pointer enter targeting.
  const double clipped_x = clip_bounds.x + clip_bounds.width + 10.0;
  assert(clipped_x < target_bounds.x + target_bounds.width);
  assert(SystemTestAccess::processEvent(
      ui, Event{.type = EventType::MouseMove, .x = clipped_x, .y = inside_y}));
  assert(left == lower_target);
  assert(target_leave_count == 1);
  assert(entered == lower_root);
  SystemTestAccess::buildFrame(ui, 0.0f, 200, 120, 200, 120, 1.0f, 1.0f,
                               draw_data);
  assert(!containsVertexColor(draw_data, 0xffff00ffu));

  assert(SystemTestAccess::processEvent(
      ui, Event{.type = EventType::MouseMove, .x = inside_x, .y = inside_y}));
  assert(entered == lower_target);
  assert(target_leave_count == 1);

  // A newly opened higher-layer document owns hover at the overlap. The lower
  // document is cleared before the higher document's move target returns early.
  const auto upper_opened = ui.open("ui/hover-upper", {.layer = 20});
  assert(upper_opened);
  const auto upper_root = ui.findById(upper_opened.document, "upper-root");
  const auto upper_target = ui.findById(upper_opened.document, "upper-target");
  assert(upper_root && upper_target);
  karma::ui::ElementHandle upper_entered{};
  karma::ui::ElementHandle upper_moved{};
  const auto upper_enter = ui.on(
      upper_root, karma::ui::EventType::PointerEnter,
      [&](karma::ui::Event& event) {
        upper_entered = event.target;
        transfer_order.push_back(2);
      },
      {.capture = true});
  const auto upper_move = ui.on(
      upper_root, karma::ui::EventType::PointerMove,
      [&](karma::ui::Event& event) { upper_moved = event.target; });
  assert(upper_enter && upper_move);
  SystemTestAccess::buildFrame(ui, 0.0f, 200, 120, 200, 120, 1.0f, 1.0f,
                               draw_data);
  transfer_order.clear();
  assert(SystemTestAccess::processEvent(
      ui, Event{.type = EventType::MouseMove, .x = inside_x, .y = inside_y}));
  assert(upper_entered == upper_target);
  assert(upper_moved == upper_root);
  assert(left == lower_target);
  assert(target_leave_count == 2);
  assert((transfer_order == std::vector<int>{1, 2}));
  assert(SystemTestAccess::cursorShape(ui) ==
         karma::platform::CursorShape::Crosshair);
  SystemTestAccess::buildFrame(ui, 0.0f, 200, 120, 200, 120, 1.0f, 1.0f,
                               draw_data);
  assert(containsVertexColor(draw_data, 0xff00ff00u));
  assert(!containsVertexColor(draw_data, 0xffff00ffu));

  // Visibility changes invalidate the retained document order used by input,
  // cursor selection, painting, and accessibility without reopening either
  // document.
  assert(ui.hide(upper_opened.document));
  entered = {};
  moved = {};
  assert(SystemTestAccess::processEvent(
      ui, Event{.type = EventType::MouseMove, .x = inside_x, .y = inside_y}));
  assert(entered == lower_target);
  assert(moved == lower_root);
  assert(SystemTestAccess::cursorShape(ui) ==
         karma::platform::CursorShape::Move);
  SystemTestAccess::buildFrame(ui, 0.0f, 200, 120, 200, 120, 1.0f, 1.0f,
                               draw_data);
  assert(containsVertexColor(draw_data, 0xffff00ffu));
  assert(!containsVertexColor(draw_data, 0xff00ff00u));

  assert(ui.show(upper_opened.document));
  upper_entered = {};
  upper_moved = {};
  assert(SystemTestAccess::processEvent(
      ui, Event{.type = EventType::MouseMove, .x = inside_x, .y = inside_y}));
  assert(upper_entered == upper_target);
  assert(upper_moved == upper_root);
  assert(SystemTestAccess::cursorShape(ui) ==
         karma::platform::CursorShape::Crosshair);
  SystemTestAccess::buildFrame(ui, 0.0f, 200, 120, 200, 120, 1.0f, 1.0f,
                               draw_data);
  assert(containsVertexColor(draw_data, 0xff00ff00u));
  assert(!containsVertexColor(draw_data, 0xffff00ffu));

  assert(ui.close(upper_opened.document));
  assert(ui.close(lower_opened.document));
}

void testRetainedRuntimeIntegration() {
  using karma::platform::Event;
  using karma::platform::EventType;
  using karma::ui::detail::SystemTestAccess;

  karma::assets::AssetRegistry assets;
  const std::string theme = R"JSON({
    "format": "karma.ui.theme",
    "version": 2,
    "defaults": {
      "body": {
        "layout": {
          "mode": "column",
          "width": "100%",
          "height": "100%",
          "padding": 10,
          "gap": 4
        },
        "appearance": { "box": { "background_color": "#101820" } }
      },
      "button": {
        "layout": { "width": 120, "height": 28 },
        "appearance": { "box": { "background_color": "#4477aa" } }
      },
      "toggle": {
        "layout": { "width": 120, "height": 28 },
        "appearance": { "box": { "background_color": "#4477aa" } }
      },
      "slider": {
        "layout": { "width": 120, "height": 28 },
        "appearance": { "box": { "background_color": "#4477aa" } }
      },
      "select": {
        "layout": { "width": 120, "height": 28 },
        "appearance": { "box": { "background_color": "#4477aa" } }
      }
    },
    "styles": {
      "first": {
        "appearance": {
          "box": {
            "background_image": "linear-gradient(to right, #224466, #66aaff)",
            "border_width": 2,
            "border_color": "white",
            "border_radius": 7
          }
        }
      }
    }
  })JSON";
  assert(assets.registerUiThemeAsset(
      "ui/runtime-theme", {.canonical_json_utf8 = theme}));

  const std::string document = R"JSON({
    "format": "karma.ui.document",
    "version": 2,
    "themes": [{ "asset": "ui/runtime-theme" }],
    "root": {
      "type": "body",
      "id": "root",
      "children": [
        {
          "type": "button",
          "id": "first",
          "styles": ["first"],
          "semantics": { "tab_index": 2 },
          "props": { "text": "First" }
        },
        {
          "type": "button",
          "id": "second",
          "semantics": { "tab_index": 1 },
          "props": { "text": "Second" },
          "on": { "click": "secondary" }
        },
        {
          "type": "toggle",
          "id": "music",
          "props": {
            "value": { "bind": "settings.music", "mode": "two_way" }
          }
        },
        {
          "type": "slider",
          "id": "volume",
          "props": {
            "value": { "bind": "settings.volume", "mode": "two_way" },
            "min": 0,
            "max": 1,
            "step": 0.25
          }
        },
        {
          "type": "select",
          "id": "quality",
          "semantics": { "tab_index": -1 },
          "props": {
            "value": { "bind": "settings.quality", "mode": "two_way" }
          },
          "children": [
            {
              "type": "option",
              "props": { "value": "low", "text": "Low" }
            },
            {
              "type": "option",
              "props": { "value": "high", "text": "High" }
            }
          ]
        }
      ]
    }
  })JSON";
  assert(assets.registerUiDocumentAsset(
      "ui/runtime", {.canonical_json_utf8 = document,
                     .dependencies = {
                         {karma::assets::UiAssetDependencyKind::UiTheme,
                          "ui/runtime-theme"}}}));

  karma::ui::UiSystemConfig config;
  config.enabled = true;
  config.hot_reload = true;
  config.source_poll_interval = std::chrono::milliseconds{0};
  karma::ui::System ui(assets, nullptr, config);
  const auto opened = ui.open("ui/runtime", {.layer = 20, .modal = true});
  assert(opened);
  const auto document_handle = opened.document;
  assert(ui.set(document_handle, "settings.music", false));
  assert(ui.set(document_handle, "settings.volume", 0.5));
  assert(ui.set(document_handle, "settings.quality", "low"));

  karma::rendering::UIDrawData draw_data;
  SystemTestAccess::buildFrame(ui, 1.0f / 60.0f, 200, 190, 400, 285,
                               2.0f, 1.5f, draw_data);
  assert(karma::rendering::validateUIDrawData(draw_data));
  assert(SystemTestAccess::capturesAllInput(ui));

  const auto root = ui.findById(document_handle, "root");
  const auto first = ui.findById(document_handle, "first");
  const auto second = ui.findById(document_handle, "second");
  const auto music = ui.findById(document_handle, "music");
  const auto quality = ui.findById(document_handle, "quality");
  assert(root && first && second && music && quality);
  const auto* first_accessible = accessible(ui, first);
  const auto* music_accessible = accessible(ui, music);
  assert(first_accessible != nullptr && first_accessible->focusable);
  assert(first_accessible->bounds.width > 100.0f);
  assert(music_accessible != nullptr && music_accessible->bounds.height > 20.0f);
  assert(accessible(ui, quality)->focus_order == -1);

  assert(SystemTestAccess::processEvent(
      ui, Event{.type = EventType::KeyDown, .key = karma::platform::Key::Tab}));
  SystemTestAccess::buildFrame(ui, 0.0f, 200, 190, 400, 285, 2.0f, 1.5f,
                               draw_data);
  assert(accessible(ui, second)->focused);

  // A blur handler that restores focus wins over the outer focus request.
  assert(ui.focus(first));
  const auto blur_listener = ui.on(first, karma::ui::EventType::Blur,
                                   [&](karma::ui::Event&) { assert(ui.focus(first)); });
  assert(blur_listener);
  assert(ui.focus(second));
  SystemTestAccess::buildFrame(ui, 0.0f, 200, 190, 400, 285, 2.0f, 1.5f,
                               draw_data);
  assert(accessible(ui, first)->focused);
  assert(!accessible(ui, second)->focused);
  assert(ui.removeListener(blur_listener));

  // Click dispatch follows capture, target, bubble and applies widget defaults.
  std::vector<int> phases;
  const auto capture = ui.on(root, karma::ui::EventType::Click,
                             [&](karma::ui::Event&) { phases.push_back(1); },
                             {.capture = true});
  const auto target = ui.on(music, karma::ui::EventType::Click,
                            [&](karma::ui::Event&) { phases.push_back(2); });
  const auto bubble = ui.on(root, karma::ui::EventType::Click,
                            [&](karma::ui::Event&) { phases.push_back(3); });
  assert(capture && target && bubble);
  const auto bounds = accessible(ui, music)->bounds;
  const double pointer_x = bounds.x + bounds.width * 0.5;
  const double pointer_y = bounds.y + bounds.height * 0.5;
  assert(SystemTestAccess::processEvent(
      ui, Event{.type = EventType::MouseMove, .x = pointer_x, .y = pointer_y}));
  assert(SystemTestAccess::processEvent(
      ui, Event{.type = EventType::MouseButtonDown, .x = pointer_x, .y = pointer_y}));
  assert(SystemTestAccess::processEvent(
      ui, Event{.type = EventType::MouseButtonUp, .x = pointer_x, .y = pointer_y}));
  assert((phases == std::vector<int>{1, 2, 3}));
  assert(ui.get(document_handle, "settings.music")->asBoolean().value_or(false));

  assert(ui.focus(quality));
  assert(SystemTestAccess::processEvent(
      ui, Event{.type = EventType::KeyDown, .key = karma::platform::Key::Down}));
  assert(ui.get(document_handle, "settings.quality") == karma::ui::Value("low"));
  assert(SystemTestAccess::processEvent(
      ui, Event{.type = EventType::KeyDown, .key = karma::platform::Key::Down}));
  assert(SystemTestAccess::processEvent(
      ui, Event{.type = EventType::KeyDown, .key = karma::platform::Key::Enter}));
  assert(ui.get(document_handle, "settings.quality") == karma::ui::Value("high"));

  int action_count = 0;
  const auto action = ui.onAction(document_handle, "secondary",
                                  [&](const karma::ui::ActionEvent&) { ++action_count; });
  assert(action);

  // Invalid reloads retain the last-good tree and diagnostics do not grow per poll.
  const std::string invalid = R"JSON({
    "format": "karma.ui.document",
    "version": 2,
    "root": { "type": "script" }
  })JSON";
  assert(assets.registerUiDocumentAsset(
      "ui/runtime", {.canonical_json_utf8 = invalid}));
  SystemTestAccess::buildFrame(ui, 1.0f / 60.0f, 200, 190, 400, 285,
                               2.0f, 1.5f, draw_data);
  assert(ui.findById(document_handle, "second") == second);
  const std::size_t diagnostic_count = ui.diagnostics(document_handle).size();
  assert(diagnostic_count > 0u);
  SystemTestAccess::buildFrame(ui, 1.0f / 60.0f, 200, 190, 400, 285,
                               2.0f, 1.5f, draw_data);
  assert(ui.diagnostics(document_handle).size() == diagnostic_count);

  const std::string replacement = R"JSON({
    "format": "karma.ui.document",
    "version": 2,
    "themes": [{ "asset": "ui/runtime-theme" }],
    "root": {
      "type": "body",
      "id": "root",
      "children": [
        {
          "type": "button",
          "id": "second",
          "props": { "text": "Updated" },
          "on": { "click": "secondary" }
        },
        {
          "type": "toggle",
          "id": "music",
          "props": {
            "value": { "bind": "settings.music", "mode": "two_way" }
          }
        }
      ]
    }
  })JSON";
  assert(assets.registerUiDocumentAsset(
      "ui/runtime", {.canonical_json_utf8 = replacement,
                     .dependencies = {
                         {karma::assets::UiAssetDependencyKind::UiTheme,
                          "ui/runtime-theme"}}}));
  SystemTestAccess::buildFrame(ui, 1.0f / 60.0f, 200, 190, 400, 285,
                               2.0f, 1.5f, draw_data);
  const auto reloaded_second = ui.findById(document_handle, "second");
  assert(reloaded_second && reloaded_second != second);
  assert(!ui.setText(second, "stale"));
  assert(ui.get(document_handle, "settings.music")->asBoolean().value_or(false));
  assert(ui.focus(reloaded_second));
  assert(SystemTestAccess::processEvent(
      ui, Event{.type = EventType::KeyDown, .key = karma::platform::Key::Enter}));
  assert(action_count == 1);
  assert(ui.close(document_handle));
}

void testConfigurableGamepadNavigationBindings() {
  using karma::platform::Event;
  using karma::platform::EventType;
  using karma::platform::GamepadAxis;
  using karma::platform::GamepadButton;
  using karma::ui::detail::SystemTestAccess;

  const karma::ui::GamepadNavigationBindings defaults;
  assert(defaults.up == GamepadButton::DpadUp);
  assert(defaults.right == GamepadButton::DpadRight);
  assert(defaults.down == GamepadButton::DpadDown);
  assert(defaults.left == GamepadButton::DpadLeft);
  assert(defaults.horizontal_axis == GamepadAxis::LeftX);
  assert(defaults.vertical_axis == GamepadAxis::LeftY);
  assert(defaults.accept == GamepadButton::A);
  assert(defaults.cancel == GamepadButton::B);
  assert(defaults.page_previous == GamepadButton::LeftShoulder);
  assert(defaults.page_next == GamepadButton::RightShoulder);

  karma::assets::AssetRegistry assets;
  const std::string theme = R"JSON({
    "format": "karma.ui.theme",
    "version": 2,
    "defaults": {
      "body": {
        "layout": {
          "mode": "grid",
          "width": "100%",
          "height": "100%",
          "columns": [80, 80],
          "rows": [30, 30, 50],
          "gap": 10
        }
      },
      "button": { "layout": { "width": 80, "height": 30 } }
    },
    "styles": {
      "pager": {
        "layout": {
          "grid_column": "1 / span 2",
          "width": 170,
          "height": 50
        }
      },
      "scroll-content": {
        "layout": { "width": 160, "height": 180, "shrink": 0 }
      }
    }
  })JSON";
  assert(assets.registerUiThemeAsset(
      "ui/gamepad-theme", {.canonical_json_utf8 = theme}));

  const std::string document = R"JSON({
    "format": "karma.ui.document",
    "version": 2,
    "themes": [{ "asset": "ui/gamepad-theme" }],
    "root": {
      "type": "body",
      "children": [
        {
          "type": "button",
          "id": "top-left",
          "props": { "text": "TL" },
          "on": { "click": "accept", "cancel": "cancel" }
        },
        {
          "type": "button",
          "id": "top-right",
          "props": { "text": "TR" }
        },
        {
          "type": "button",
          "id": "bottom-left",
          "props": { "text": "BL" }
        },
        {
          "type": "button",
          "id": "bottom-right",
          "props": { "text": "BR" }
        },
        {
          "type": "scroll",
          "id": "pager",
          "styles": ["pager"],
          "children": [{
            "type": "div",
            "id": "scroll-content",
            "styles": ["scroll-content"]
          }]
        }
      ]
    }
  })JSON";
  assert(assets.registerUiDocumentAsset(
      "ui/gamepad", {.canonical_json_utf8 = document,
                     .dependencies = {
                         {karma::assets::UiAssetDependencyKind::UiTheme,
                          "ui/gamepad-theme"}}}));

  karma::ui::UiSystemConfig config;
  config.enabled = true;
  config.hot_reload = false;
  config.gamepad_navigation = {
      .up = GamepadButton::LeftStick,
      .right = GamepadButton::Y,
      .down = GamepadButton::RightStick,
      .left = GamepadButton::X,
      .horizontal_axis = GamepadAxis::RightX,
      .vertical_axis = GamepadAxis::RightY,
      .accept = GamepadButton::Start,
      .cancel = GamepadButton::Back,
      .page_previous = GamepadButton::DpadUp,
      .page_next = GamepadButton::DpadDown,
  };
  karma::ui::System ui(assets, nullptr, config);
  const auto opened = ui.open("ui/gamepad", {.modal = true});
  assert(opened);

  karma::rendering::UIDrawData draw_data;
  SystemTestAccess::buildFrame(ui, 0.0f, 220, 180, 220, 180, 1.0f, 1.0f,
                               draw_data);
  const auto top_left = ui.findById(opened.document, "top-left");
  const auto top_right = ui.findById(opened.document, "top-right");
  const auto bottom_left = ui.findById(opened.document, "bottom-left");
  const auto bottom_right = ui.findById(opened.document, "bottom-right");
  const auto pager = ui.findById(opened.document, "pager");
  const auto scroll_content = ui.findById(opened.document, "scroll-content");
  assert(top_left && top_right && bottom_left && bottom_right && pager &&
         scroll_content);

  const auto focused = [&](karma::ui::ElementHandle element) {
    SystemTestAccess::buildFrame(ui, 0.0f, 220, 180, 220, 180, 1.0f, 1.0f,
                                 draw_data);
    const auto* node = accessible(ui, element);
    return node != nullptr && node->focused;
  };
  const auto button_down = [&](GamepadButton button) {
    assert(SystemTestAccess::processEvent(
        ui, Event{.type = EventType::GamepadButtonDown,
                  .gamepad = 0,
                  .gamepadButton = button}));
  };
  const auto axis_motion = [&](GamepadAxis axis, float value) {
    assert(SystemTestAccess::processEvent(
        ui, Event{.type = EventType::GamepadAxisMotion,
                  .gamepad = 0,
                  .gamepadAxis = axis,
                  .gamepadValue = value}));
  };

  assert(ui.focus(top_left));
  button_down(GamepadButton::DpadRight);
  axis_motion(GamepadAxis::LeftX, 1.0f);
  assert(focused(top_left));

  button_down(GamepadButton::Y);
  assert(focused(top_right));
  button_down(GamepadButton::RightStick);
  assert(focused(bottom_right));
  button_down(GamepadButton::X);
  assert(focused(bottom_left));
  button_down(GamepadButton::LeftStick);
  assert(focused(top_left));

  axis_motion(GamepadAxis::RightX, 1.0f);
  assert(focused(top_right));
  axis_motion(GamepadAxis::RightY, 1.0f);
  assert(focused(bottom_right));

  int accept_count = 0;
  int cancel_count = 0;
  assert(ui.onAction(opened.document, "accept",
                     [&](const karma::ui::ActionEvent&) { ++accept_count; }));
  assert(ui.onAction(opened.document, "cancel",
                     [&](const karma::ui::ActionEvent&) { ++cancel_count; }));
  assert(ui.focus(top_left));
  button_down(GamepadButton::A);
  button_down(GamepadButton::B);
  assert(accept_count == 0 && cancel_count == 0);
  button_down(GamepadButton::Start);
  button_down(GamepadButton::Back);
  assert(accept_count == 1 && cancel_count == 1);

  assert(ui.focus(pager));
  const float initial_content_y = accessible(ui, scroll_content)->bounds.y;
  button_down(GamepadButton::RightShoulder);
  SystemTestAccess::buildFrame(ui, 0.0f, 220, 180, 220, 180, 1.0f, 1.0f,
                               draw_data);
  assert(accessible(ui, scroll_content)->bounds.y == initial_content_y);
  button_down(GamepadButton::DpadDown);
  SystemTestAccess::buildFrame(ui, 0.0f, 220, 180, 220, 180, 1.0f, 1.0f,
                               draw_data);
  assert(accessible(ui, scroll_content)->bounds.y < initial_content_y);
  button_down(GamepadButton::DpadUp);
  SystemTestAccess::buildFrame(ui, 0.0f, 220, 180, 220, 180, 1.0f, 1.0f,
                               draw_data);
  assert(accessible(ui, scroll_content)->bounds.y == initial_content_y);
}

void testScrollbarsAndNestedScrolling() {
  using karma::platform::Event;
  using karma::platform::EventType;
  using karma::ui::detail::SystemTestAccess;

  constexpr std::uint32_t kOuterTrackColor = 0xff302010u;
  constexpr std::uint32_t kOuterThumbColor = 0xff605040u;

  karma::assets::AssetRegistry assets;
  const std::string theme = R"JSON({
    "format": "karma.ui.theme",
    "version": 2,
    "styles": {
      "outer-scroll": {
        "layout": { "width": 180, "height": 130 },
        "appearance": {
          "parts": {
            "vertical_track": {
              "box": { "background_color": "#102030" },
              "metrics": { "width": 14 }
            },
            "vertical_thumb": {
              "box": { "background_color": "#405060" },
              "metrics": { "min_length": 18 },
              "states": {
                "hover": { "box": { "background_color": "#405060" } },
                "pressed": { "box": { "background_color": "#405060" } }
              }
            }
          }
        }
      },
      "outer-content": {
        "layout": {
          "mode": "column",
          "width": 150,
          "height": 360,
          "shrink": 0
        }
      },
      "inner-scroll": {
        "layout": { "width": 120, "height": 70, "shrink": 0 },
        "appearance": {
          "parts": {
            "vertical_track": {
              "box": { "background_color": "#781234" },
              "metrics": { "width": 10 }
            },
            "vertical_thumb": {
              "box": { "background_color": "#895678" },
              "metrics": { "min_length": 14 }
            }
          }
        }
      },
      "inner-content": {
        "layout": { "width": 100, "height": 220, "shrink": 0 }
      },
      "scroll-spacer": {
        "layout": { "width": 100, "height": 210, "shrink": 0 }
      },
      "scroll-target": {
        "layout": { "width": 100, "height": 30, "shrink": 0 }
      }
    }
  })JSON";
  const std::string document = R"JSON({
    "format": "karma.ui.document",
    "version": 2,
    "themes": [{ "asset": "ui/scroll-theme" }],
    "root": {
      "type": "body",
      "layout": { "width": 320, "height": 240 },
      "children": [{
        "type": "scroll",
        "id": "outer-scroll",
        "styles": ["outer-scroll"],
        "props": {
          "scrollbar_visibility": "auto",
          "scrollbar_placement": "gutter"
        },
        "children": [{
          "type": "panel",
          "id": "outer-content",
          "styles": ["outer-content"],
          "children": [
            {
              "type": "scroll",
              "id": "inner-scroll",
              "styles": ["inner-scroll"],
              "props": {
                "scrollbar_visibility": "auto",
                "scrollbar_placement": "gutter"
              },
              "children": [{
                "type": "panel",
                "id": "inner-content",
                "styles": ["inner-content"]
              }]
            },
            {
              "type": "spacer",
              "styles": ["scroll-spacer"]
            },
            {
              "type": "button",
              "id": "scroll-target",
              "styles": ["scroll-target"],
              "props": { "text": "Target" }
            }
          ]
        }]
      }]
    }
  })JSON";
  assert(assets.registerUiThemeAsset(
      "ui/scroll-theme", {.canonical_json_utf8 = theme}));
  assert(assets.registerUiDocumentAsset(
      "ui/scroll-test",
      {.canonical_json_utf8 = document,
       .dependencies = {{karma::assets::UiAssetDependencyKind::UiTheme,
                         "ui/scroll-theme"}}}));

  karma::ui::UiSystemConfig config;
  config.enabled = true;
  config.hot_reload = false;
  karma::ui::System ui(assets, nullptr, config);
  const auto opened = ui.open("ui/scroll-test");
  assert(opened);
  const auto outer = ui.findById(opened.document, "outer-scroll");
  const auto inner = ui.findById(opened.document, "inner-scroll");
  const auto target = ui.findById(opened.document, "scroll-target");
  assert(outer && inner && target);

  karma::rendering::UIDrawData draw_data;
  const auto build = [&] {
    SystemTestAccess::buildFrame(ui, 0.0f, 320, 240, 320, 240, 1.0f, 1.0f,
                                 draw_data);
    assert(karma::rendering::validateUIDrawData(draw_data));
    return ui.frameDiagnostics();
  };
  (void)build();
  assert(!draw_data.commands.empty());
  const auto track_bounds = vertexColorBounds(draw_data, kOuterTrackColor);
  const auto thumb_bounds = vertexColorBounds(draw_data, kOuterThumbColor);
  assert(track_bounds.has_value() && thumb_bounds.has_value());
  assert(nearlyEqual(track_bounds->width, 14.0f));
  assert(track_bounds->height > thumb_bounds->height);
  assert(nearlyEqual(track_bounds->x, thumb_bounds->x));

  const auto* outer_accessible = accessible(ui, outer);
  const auto* inner_accessible = accessible(ui, inner);
  assert(outer_accessible != nullptr && inner_accessible != nullptr);
  assert(outer_accessible->role == karma::ui::AccessibilityRole::Scroll);
  assert(outer_accessible->scroll_y.value_or(-1.0) == 0.0);
  assert(outer_accessible->scroll_max_y.value_or(0.0) > 100.0);
  assert(inner_accessible->scroll_max_y.value_or(0.0) > 100.0);
  assert(hasAccessibilityAction(*outer_accessible,
                                karma::ui::AccessibilityAction::Scroll));

  const auto settled = build();
  assert(settled.reconciled_nodes == 0u);
  assert(settled.restyled_nodes == 0u);
  assert(settled.laid_out_nodes == 0u);
  assert(settled.rebuilt_fragments == 0u);

  assert(ui.scrollTo(outer, 0.0f, 40.0f));
  build();
  assert(nearlyEqual(
      static_cast<float>(accessible(ui, outer)->scroll_y.value_or(-1.0)),
      40.0f));
  assert(ui.scrollBy(outer, 0.0f, 25.0f));
  build();
  assert(nearlyEqual(
      static_cast<float>(accessible(ui, outer)->scroll_y.value_or(-1.0)),
      65.0f));

  assert(ui.scrollTo(outer, 0.0f, 0.0f));
  build();
  assert(ui.scrollIntoView(target, karma::ui::ScrollAlignment::Start,
                           karma::ui::ScrollAlignment::Start));
  build();
  const auto* aligned_outer = accessible(ui, outer);
  const auto* aligned_target = accessible(ui, target);
  assert(aligned_outer->scroll_y.value_or(0.0) > 100.0);
  assert(nearlyEqual(
      static_cast<float>(aligned_outer->scroll_y.value_or(-1.0)),
      static_cast<float>(aligned_outer->scroll_max_y.value_or(-2.0))));
  assert(aligned_target->bounds.y >= aligned_outer->bounds.y);
  assert(aligned_target->bounds.y + aligned_target->bounds.height <=
         aligned_outer->bounds.y + aligned_outer->bounds.height);

  assert(ui.scrollTo(outer, 0.0f, 0.0f));
  build();
  const auto initial_track = *vertexColorBounds(draw_data, kOuterTrackColor);
  const auto initial_thumb = *vertexColorBounds(draw_data, kOuterThumbColor);
  const double track_x = initial_track.x + initial_track.width * 0.5;
  const double page_y = initial_track.y + initial_track.height - 3.0;
  assert(page_y > initial_thumb.y + initial_thumb.height);
  assert(SystemTestAccess::processEvent(
      ui, Event{.type = EventType::MouseMove, .x = track_x, .y = page_y}));
  assert(SystemTestAccess::processEvent(
      ui, Event{.type = EventType::MouseButtonDown, .x = track_x, .y = page_y}));
  assert(SystemTestAccess::processEvent(
      ui, Event{.type = EventType::MouseButtonUp, .x = track_x, .y = page_y}));
  build();
  assert(accessible(ui, outer)->scroll_y.value_or(0.0) > 0.0);

  assert(ui.scrollTo(outer, 0.0f, 0.0f));
  build();
  const auto drag_thumb = *vertexColorBounds(draw_data, kOuterThumbColor);
  const double thumb_x = drag_thumb.x + drag_thumb.width * 0.5;
  const double thumb_y = drag_thumb.y + drag_thumb.height * 0.5;
  assert(SystemTestAccess::processEvent(
      ui, Event{.type = EventType::MouseMove, .x = thumb_x, .y = thumb_y}));
  (void)build();  // Settle the thumb hover pseudo-state.
  assert(SystemTestAccess::processEvent(
      ui, Event{.type = EventType::MouseButtonDown, .x = thumb_x, .y = thumb_y}));
  (void)build();  // Settle the pressed pseudo-state before measuring the drag.
  assert(SystemTestAccess::processEvent(
      ui, Event{.type = EventType::MouseMove, .x = thumb_x, .y = thumb_y + 24.0}));
  const auto thumb_drag_work = build();
  assert(thumb_drag_work.reconciled_nodes == 0u);
  assert(thumb_drag_work.restyled_nodes == 0u);
  assert(thumb_drag_work.laid_out_nodes == 0u);
  assert(thumb_drag_work.rebuilt_fragments > 0u);
  assert(SystemTestAccess::processEvent(
      ui, Event{.type = EventType::MouseButtonUp, .x = thumb_x,
                .y = thumb_y + 24.0}));
  (void)build();
  assert(accessible(ui, outer)->scroll_y.value_or(0.0) > 20.0);

  assert(ui.scrollTo(outer, 0.0f, 0.0f));
  assert(ui.scrollTo(inner, 0.0f, 10000.0f));
  build();
  const double inner_max = accessible(ui, inner)->scroll_max_y.value_or(-1.0);
  assert(inner_max > 0.0);
  assert(nearlyEqual(
      static_cast<float>(accessible(ui, inner)->scroll_y.value_or(-1.0)),
      static_cast<float>(inner_max)));
  const auto inner_bounds = accessible(ui, inner)->bounds;
  const double inner_x = inner_bounds.x + 10.0;
  const double inner_y = inner_bounds.y + 10.0;
  assert(SystemTestAccess::processEvent(
      ui, Event{.type = EventType::MouseMove, .x = inner_x, .y = inner_y}));
  (void)build();  // Settle hover before measuring wheel placement work.

  bool prevent_wheel = true;
  int inner_wheel_events = 0;
  const auto wheel_listener = ui.on(
      inner, karma::ui::EventType::Scroll, [&](karma::ui::Event& event) {
        ++inner_wheel_events;
        assert(event.target == inner);
        assert(event.delta_x == 0.0);
        assert(event.delta_y == -1.0);
        if (prevent_wheel) event.preventDefault();
      });
  assert(wheel_listener);
  assert(SystemTestAccess::processEvent(
      ui, Event{.type = EventType::MouseScroll, .scrollY = -1.0}));
  assert(inner_wheel_events == 1);
  assert(nearlyEqual(
      static_cast<float>(accessible(ui, inner)->scroll_y.value_or(-1.0)),
      static_cast<float>(inner_max)));
  assert(accessible(ui, outer)->scroll_y.value_or(-1.0) == 0.0);

  prevent_wheel = false;
  assert(SystemTestAccess::processEvent(
      ui, Event{.type = EventType::MouseScroll,
                .scrollY = -1.0}));
  assert(inner_wheel_events == 2);
  const auto wheel_work = build();
  assert(wheel_work.reconciled_nodes == 0u);
  assert(wheel_work.restyled_nodes == 0u);
  assert(wheel_work.laid_out_nodes == 0u);
  assert(wheel_work.rebuilt_fragments > 0u);
  assert(nearlyEqual(
      static_cast<float>(accessible(ui, inner)->scroll_y.value_or(-1.0)),
      static_cast<float>(inner_max)));
  assert(accessible(ui, outer)->scroll_y.value_or(0.0) > 0.0);
}

void testDisclosureWindowAndSplitterWidgets() {
  using karma::platform::Event;
  using karma::platform::EventType;
  using karma::ui::detail::SystemTestAccess;

  karma::assets::AssetRegistry assets;
  const std::string document = R"JSON({
    "format": "karma.ui.document",
    "version": 2,
    "root": {
      "type": "body",
      "layout": { "mode": "overlay", "width": 420, "height": 320 },
      "children": [
        {
          "type": "window",
          "id": "floating-window",
          "layout": {
            "position": [40, 30],
            "width": 180,
            "height": 120,
            "min_width": 120,
            "min_height": 80
          },
          "appearance": {
            "parts": {
              "titlebar": {
                "box": { "background_color": "#203050" },
                "metrics": { "height": 28 }
              },
              "resize_grip": {
                "box": { "background_color": "#607090" },
                "metrics": { "size": 8 }
              }
            }
          },
          "props": {
            "title": "Tools",
            "state": { "bind": "window_state", "mode": "two_way" },
            "resizable": true,
            "closable": false,
            "collapsible": false
          },
          "children": [{
            "type": "panel",
            "id": "window-content",
            "layout": { "width": 160, "height": 70 }
          }]
        },
        {
          "type": "disclosure",
          "id": "details",
          "layout": { "position": [260, 30], "width": 130, "height": 90 },
          "props": { "expanded": { "bind": "details_open" } },
          "on": { "toggle": "details-toggle" },
          "children": [
            {
              "type": "text",
              "id": "details-header",
              "layout": { "width": 120, "height": 24 },
              "props": { "text": "Details" }
            },
            {
              "type": "panel",
              "id": "details-content",
              "layout": { "width": 120, "height": 40 },
              "appearance": {
                "box": { "background_color": "#334455" }
              }
            }
          ]
        },
        {
          "type": "panel",
          "id": "split-row",
          "layout": {
            "mode": "row",
            "position": [20, 200],
            "width": 300,
            "height": 60
          },
          "children": [
            {
              "type": "panel",
              "id": "left-pane",
              "layout": { "width": 100, "height": 60, "shrink": 0 },
              "appearance": {
                "box": { "background_color": "#284060" }
              }
            },
            {
              "type": "splitter",
              "id": "pane-splitter",
              "layout": { "width": 10, "height": 60, "shrink": 0 },
              "appearance": {
                "box": { "background_color": "#8090a0" }
              },
              "props": { "orientation": "vertical" }
            },
            {
              "type": "panel",
              "id": "right-pane",
              "layout": { "width": 120, "height": 60, "shrink": 0 },
              "appearance": {
                "box": { "background_color": "#304828" }
              }
            }
          ]
        }
      ]
    }
  })JSON";
  assert(assets.registerUiDocumentAsset(
      "ui/widgets", {.canonical_json_utf8 = document}));

  karma::ui::UiSystemConfig config;
  config.enabled = true;
  config.hot_reload = false;
  karma::ui::System ui(assets, nullptr, config);
  const auto opened = ui.open("ui/widgets");
  assert(opened);
  const auto document_handle = opened.document;
  assert(ui.set(document_handle, "details_open", false));
  assert(ui.set(
      document_handle, "window_state",
      karma::ui::Value::Object{
          {"open", true},
          {"collapsed", false},
          {"position", karma::ui::Value::Array{40, 30}},
          {"size", karma::ui::Value::Array{180, 120}},
          {"z", 2},
      }));

  const auto disclosure = ui.findById(document_handle, "details");
  const auto disclosure_content =
      ui.findById(document_handle, "details-content");
  const auto window = ui.findById(document_handle, "floating-window");
  const auto window_content = ui.findById(document_handle, "window-content");
  const auto left_pane = ui.findById(document_handle, "left-pane");
  const auto splitter = ui.findById(document_handle, "pane-splitter");
  assert(disclosure && disclosure_content && window && window_content &&
         left_pane && splitter);

  int disclosure_toggles = 0;
  bool last_disclosure_value = false;
  assert(ui.onAction(
      document_handle, "details-toggle",
      [&](const karma::ui::ActionEvent& event) {
        ++disclosure_toggles;
        last_disclosure_value = event.value.asBoolean().value_or(false);
      }));
  int window_inputs = 0;
  int window_changes = 0;
  int splitter_inputs = 0;
  int splitter_changes = 0;
  assert(ui.on(window, karma::ui::EventType::Input,
               [&](karma::ui::Event&) { ++window_inputs; }));
  assert(ui.on(window, karma::ui::EventType::Change,
               [&](karma::ui::Event&) { ++window_changes; }));
  assert(ui.on(splitter, karma::ui::EventType::Input,
               [&](karma::ui::Event&) { ++splitter_inputs; }));
  assert(ui.on(splitter, karma::ui::EventType::Change,
               [&](karma::ui::Event&) { ++splitter_changes; }));

  karma::rendering::UIDrawData draw_data;
  const auto build = [&] {
    SystemTestAccess::buildFrame(ui, 0.0f, 420, 320, 420, 320, 1.0f, 1.0f,
                                 draw_data);
    return ui.frameDiagnostics();
  };
  build();
  assert(containsVertexColor(draw_data, 0xff503020u));
  assert(containsVertexColor(draw_data, 0xff907060u));

  const auto* disclosure_accessible = accessible(ui, disclosure);
  assert(disclosure_accessible != nullptr && !disclosure_accessible->expanded);
  assert(hasAccessibilityAction(*disclosure_accessible,
                                karma::ui::AccessibilityAction::Expand));
  assert(accessible(ui, disclosure_content) == nullptr);
  clickElement(ui, disclosure);
  build();
  assert(disclosure_toggles == 1 && last_disclosure_value);
  assert(ui.get(document_handle, "details_open") == karma::ui::Value(true));
  disclosure_accessible = accessible(ui, disclosure);
  assert(disclosure_accessible != nullptr && disclosure_accessible->expanded);
  assert(hasAccessibilityAction(*disclosure_accessible,
                                karma::ui::AccessibilityAction::Collapse));
  assert(accessible(ui, disclosure_content) != nullptr);

  auto window_bounds = accessible(ui, window)->bounds;
  assert(accessible(ui, window)->role == karma::ui::AccessibilityRole::Window);
  assert(accessible(ui, window)->open);
  const double move_start_x = window_bounds.x + 40.0;
  const double move_start_y = window_bounds.y + 14.0;
  constexpr float moved_window_x = 1.0f;
  constexpr float moved_window_y = 0.0f;
  const double move_end_x =
      move_start_x + moved_window_x - window_bounds.x;
  const double move_end_y =
      move_start_y + moved_window_y - window_bounds.y;
  assert(SystemTestAccess::processEvent(
      ui, Event{.type = EventType::MouseMove,
                .x = move_start_x,
                .y = move_start_y}));
  (void)build();  // Settle hover before measuring drag-only work.
  assert(SystemTestAccess::processEvent(
      ui, Event{.type = EventType::MouseButtonDown,
                .x = move_start_x,
                .y = move_start_y}));
  assert(SystemTestAccess::processEvent(
      ui, Event{.type = EventType::MouseMove,
                .x = move_start_x,
                .y = move_start_y}));
  const auto pointer_down_work = build();
  assert(pointer_down_work.laid_out_nodes == 0u);
  const auto disclosure_before_drag = accessible(ui, disclosure)->bounds;
  const auto pane_before_drag = accessible(ui, left_pane)->bounds;
  const auto content_before_drag = accessible(ui, window_content)->bounds;
  assert(SystemTestAccess::processEvent(
      ui, Event{.type = EventType::MouseMove,
                .x = move_end_x,
                .y = move_end_y}));
  const auto drag_work = build();
  assert(drag_work.reconciled_nodes == 0u);
  assert(drag_work.restyled_nodes == 0u);
  assert(drag_work.laid_out_nodes == 0u);
  const auto window_during_drag = accessible(ui, window)->bounds;
  const auto content_during_drag = accessible(ui, window_content)->bounds;
  const auto disclosure_during_drag = accessible(ui, disclosure)->bounds;
  const auto pane_during_drag = accessible(ui, left_pane)->bounds;
  assert(nearlyEqual(window_during_drag.x, moved_window_x));
  assert(nearlyEqual(window_during_drag.y, moved_window_y));
  assert(nearlyEqual(content_during_drag.x,
                     content_before_drag.x + moved_window_x - window_bounds.x));
  assert(nearlyEqual(content_during_drag.y,
                     content_before_drag.y + moved_window_y - window_bounds.y));
  assert(nearlyEqual(disclosure_during_drag.x, disclosure_before_drag.x));
  assert(nearlyEqual(disclosure_during_drag.y, disclosure_before_drag.y));
  assert(nearlyEqual(disclosure_during_drag.width,
                     disclosure_before_drag.width));
  assert(nearlyEqual(disclosure_during_drag.height,
                     disclosure_before_drag.height));
  assert(nearlyEqual(pane_during_drag.x, pane_before_drag.x));
  assert(nearlyEqual(pane_during_drag.y, pane_before_drag.y));
  assert(nearlyEqual(pane_during_drag.width, pane_before_drag.width));
  assert(nearlyEqual(pane_during_drag.height, pane_before_drag.height));
  assert(SystemTestAccess::processEvent(
      ui, Event{.type = EventType::MouseButtonUp,
                .x = move_end_x,
                .y = move_end_y}));
  const auto release_work = build();
  assert(release_work.laid_out_nodes == 0u);
  auto moved_window_bounds = accessible(ui, window)->bounds;
  assert(nearlyEqual(moved_window_bounds.x, moved_window_x));
  assert(nearlyEqual(moved_window_bounds.y, moved_window_y));

  const auto moved_window_state = ui.get(document_handle, "window_state");
  assert(moved_window_state.has_value() &&
         moved_window_state->asObject() != nullptr);
  const auto window_position =
      moved_window_state->asObject()->at("position").asArray();
  assert(window_position != nullptr && window_position->size() == 2u);
  assert(nearlyEqual(static_cast<float>((*window_position)[0].asNumber().value()),
                     moved_window_bounds.x));
  assert(nearlyEqual(static_cast<float>((*window_position)[1].asNumber().value()),
                     moved_window_bounds.y));

  const double resize_start_x =
      moved_window_bounds.x + moved_window_bounds.width - 2.0;
  const double resize_start_y =
      moved_window_bounds.y + moved_window_bounds.height - 2.0;
  const double resize_end_x = resize_start_x + 25.0;
  const double resize_end_y = resize_start_y + 15.0;
  assert(SystemTestAccess::processEvent(
      ui, Event{.type = EventType::MouseMove,
                .x = resize_start_x,
                .y = resize_start_y}));
  assert(SystemTestAccess::processEvent(
      ui, Event{.type = EventType::MouseButtonDown,
                .x = resize_start_x,
                .y = resize_start_y}));
  assert(SystemTestAccess::processEvent(
      ui, Event{.type = EventType::MouseMove,
                .x = resize_end_x,
                .y = resize_end_y}));
  build();
  assert(SystemTestAccess::processEvent(
      ui, Event{.type = EventType::MouseButtonUp,
                .x = resize_end_x,
                .y = resize_end_y}));
  build();
  const auto resized_window_bounds = accessible(ui, window)->bounds;
  assert(nearlyEqual(resized_window_bounds.width,
                     moved_window_bounds.width + 25.0f));
  assert(nearlyEqual(resized_window_bounds.height,
                     moved_window_bounds.height + 15.0f));
  const auto resized_window_state = ui.get(document_handle, "window_state");
  assert(resized_window_state.has_value() &&
         resized_window_state->asObject() != nullptr);
  const auto window_size =
      resized_window_state->asObject()->at("size").asArray();
  assert(window_size != nullptr && window_size->size() == 2u);
  assert(nearlyEqual(static_cast<float>((*window_size)[0].asNumber().value()),
                     resized_window_bounds.width));
  assert(nearlyEqual(static_cast<float>((*window_size)[1].asNumber().value()),
                     resized_window_bounds.height));
  assert(window_inputs >= 2 && window_changes == 2);

  const float initial_pane_width = accessible(ui, left_pane)->bounds.width;
  assert(accessible(ui, splitter)->role ==
         karma::ui::AccessibilityRole::Separator);
  assert(ui.focus(splitter));
  assert(SystemTestAccess::processEvent(
      ui, Event{.type = EventType::KeyDown, .key = karma::platform::Key::Right}));
  build();
  const float keyboard_pane_width = accessible(ui, left_pane)->bounds.width;
  assert(keyboard_pane_width > initial_pane_width);

  const auto splitter_bounds = accessible(ui, splitter)->bounds;
  const double splitter_x = splitter_bounds.x + splitter_bounds.width * 0.5;
  const double splitter_y = splitter_bounds.y + splitter_bounds.height * 0.5;
  assert(SystemTestAccess::processEvent(
      ui, Event{.type = EventType::MouseMove,
                .x = splitter_x,
                .y = splitter_y}));
  assert(SystemTestAccess::processEvent(
      ui, Event{.type = EventType::MouseButtonDown,
                .x = splitter_x,
                .y = splitter_y}));
  assert(SystemTestAccess::processEvent(
      ui, Event{.type = EventType::MouseMove,
                .x = splitter_x + 20.0,
                .y = splitter_y}));
  build();
  assert(SystemTestAccess::processEvent(
      ui, Event{.type = EventType::MouseButtonUp,
                .x = splitter_x + 20.0,
                .y = splitter_y}));
  build();
  assert(accessible(ui, left_pane)->bounds.width > keyboard_pane_width + 15.0f);
  assert(splitter_inputs >= 1 && splitter_changes == 1);
}

void testTabsAndTreeWidgets() {
  using karma::platform::Event;
  using karma::platform::EventType;
  using karma::ui::detail::SystemTestAccess;

  karma::assets::AssetRegistry assets;
  const std::string document = R"JSON({
    "format": "karma.ui.document",
    "version": 2,
    "root": {
      "type": "body",
      "layout": { "mode": "overlay", "width": 320, "height": 240 },
      "children": [
        {
          "type": "tabs",
          "id": "mode-tabs",
          "layout": {
            "mode": "row",
            "position": [10, 10],
            "width": 240,
            "height": 32
          },
          "props": { "value": { "bind": "active_tab", "mode": "two_way" } },
          "on": { "change": "tab-changed" },
          "children": [
            {
              "type": "tab",
              "id": "tab-a",
              "layout": { "width": 100, "height": 30 },
              "props": { "value": "a", "text": "Alpha" }
            },
            {
              "type": "tab",
              "id": "tab-b",
              "layout": { "width": 100, "height": 30 },
              "props": { "value": "b", "text": "Beta" }
            }
          ]
        },
        {
          "type": "tree",
          "id": "scene-tree",
          "layout": { "position": [10, 70], "width": 220, "height": 150 },
          "props": { "value": { "bind": "tree_selection", "mode": "two_way" } },
          "on": { "change": "tree-changed" },
          "children": [
            {
              "type": "tree-item",
              "id": "tree-parent",
              "layout": { "width": 180, "height": 30 },
              "props": {
                "value": "parent",
                "text": "Parent",
                "expanded": { "bind": "tree_expanded", "mode": "two_way" }
              },
              "children": [{
                "type": "tree-item",
                "id": "tree-child",
                "layout": { "width": 160, "height": 28 },
                "props": { "value": "child", "text": "Child" }
              }]
            },
            {
              "type": "tree-item",
              "id": "tree-sibling",
              "layout": { "width": 180, "height": 30 },
              "props": { "value": "sibling", "text": "Sibling" }
            }
          ]
        }
      ]
    }
  })JSON";
  assert(assets.registerUiDocumentAsset(
      "ui/selection-widgets", {.canonical_json_utf8 = document}));

  karma::ui::UiSystemConfig config;
  config.enabled = true;
  config.hot_reload = false;
  karma::ui::System ui(assets, nullptr, config);
  const auto opened = ui.open("ui/selection-widgets");
  assert(opened);
  assert(ui.set(opened.document, "active_tab", "a"));
  assert(ui.set(opened.document, "tree_selection", "sibling"));
  assert(ui.set(opened.document, "tree_expanded", false));

  const auto tab_a = ui.findById(opened.document, "tab-a");
  const auto tab_b = ui.findById(opened.document, "tab-b");
  const auto tree_parent = ui.findById(opened.document, "tree-parent");
  const auto tree_child = ui.findById(opened.document, "tree-child");
  const auto tree_sibling = ui.findById(opened.document, "tree-sibling");
  assert(tab_a && tab_b && tree_parent && tree_child && tree_sibling);

  int tab_changes = 0;
  int tree_changes = 0;
  assert(ui.onAction(opened.document, "tab-changed",
                     [&](const karma::ui::ActionEvent&) { ++tab_changes; }));
  assert(ui.onAction(opened.document, "tree-changed",
                     [&](const karma::ui::ActionEvent&) { ++tree_changes; }));

  karma::rendering::UIDrawData draw_data;
  const auto build = [&] {
    SystemTestAccess::buildFrame(ui, 0.0f, 320, 240, 320, 240, 1.0f, 1.0f,
                                 draw_data);
  };
  build();
  assert(accessible(ui, tab_a)->selected);
  assert(!accessible(ui, tab_b)->selected);
  assert(accessible(ui, tree_sibling)->selected);
  assert(accessible(ui, tree_child) == nullptr);

  assert(ui.focus(tab_a));
  assert(SystemTestAccess::processEvent(
      ui, Event{.type = EventType::KeyDown, .key = karma::platform::Key::Right}));
  build();
  assert(ui.get(opened.document, "active_tab") == karma::ui::Value("b"));
  assert(accessible(ui, tab_b)->focused && accessible(ui, tab_b)->selected);
  assert(tab_changes == 1);

  clickElement(ui, tab_a);
  build();
  assert(ui.get(opened.document, "active_tab") == karma::ui::Value("a"));
  assert(accessible(ui, tab_a)->selected);
  assert(tab_changes == 2);

  clickElement(ui, tree_parent);
  build();
  assert(ui.get(opened.document, "tree_selection") == karma::ui::Value("parent"));
  assert(ui.get(opened.document, "tree_expanded") == karma::ui::Value(false));
  assert(accessible(ui, tree_parent)->selected);
  assert(!accessible(ui, tree_parent)->expanded);
  assert(hasAccessibilityAction(*accessible(ui, tree_parent),
                                karma::ui::AccessibilityAction::Select));
  assert(hasAccessibilityAction(*accessible(ui, tree_parent),
                                karma::ui::AccessibilityAction::Expand));
  assert(tree_changes == 1);

  assert(SystemTestAccess::processEvent(
      ui, Event{.type = EventType::KeyDown, .key = karma::platform::Key::Right}));
  build();
  assert(ui.get(opened.document, "tree_selection") == karma::ui::Value("parent"));
  assert(ui.get(opened.document, "tree_expanded") == karma::ui::Value(true));
  assert(accessible(ui, tree_child) != nullptr);
  assert(hasAccessibilityAction(*accessible(ui, tree_parent),
                                karma::ui::AccessibilityAction::Collapse));

  assert(SystemTestAccess::processEvent(
      ui, Event{.type = EventType::KeyDown, .key = karma::platform::Key::Right}));
  build();
  assert(accessible(ui, tree_child)->focused);
  assert(SystemTestAccess::processEvent(
      ui, Event{.type = EventType::KeyDown, .key = karma::platform::Key::Enter}));
  build();
  assert(ui.get(opened.document, "tree_selection") == karma::ui::Value("child"));
  assert(ui.get(opened.document, "tree_expanded") == karma::ui::Value(true));
  assert(accessible(ui, tree_child)->selected);
  assert(tree_changes == 2);
}

void testSelectPopupMenuAndTooltipWidgets() {
  using karma::platform::Event;
  using karma::platform::EventType;
  using karma::ui::detail::SystemTestAccess;

  karma::assets::AssetRegistry assets;
  const std::string document = R"JSON({
    "format": "karma.ui.document",
    "version": 2,
    "root": {
      "type": "body",
      "layout": { "mode": "overlay", "width": 320, "height": 220 },
      "children": [
        {
          "type": "button",
          "id": "tooltip-anchor",
          "layout": { "position": [20, 20], "width": 120, "height": 30 },
          "props": { "text": "Hover me" }
        },
        {
          "type": "tooltip",
          "id": "hover-tooltip",
          "layout": { "width": 100, "height": 24 },
          "props": {
            "anchor": "tooltip-anchor",
            "delay_ms": 400,
            "text": "Helpful text"
          }
        },
        {
          "type": "select",
          "id": "quality-select",
          "layout": { "position": [20, 170], "width": 120, "height": 32 },
          "appearance": {
            "parts": {
              "popup": {
                "box": {
                  "background_color": "#010203",
                  "border_color": "#abcdef",
                  "border_width": 2,
                  "border_radius": 3
                }
              },
              "option": {
                "box": { "background_color": "#123456" },
                "states": {
                  "hover": { "box": { "background_color": "#345678" } }
                }
              }
            }
          },
          "props": {
            "value": { "bind": "quality", "mode": "two_way" },
            "open": { "bind": "select_open", "mode": "two_way" },
            "placement": "auto"
          },
          "on": { "cancel": "select-cancel" },
          "children": [
            {
              "type": "option",
              "id": "quality-low",
              "layout": { "width": 120, "height": 28 },
              "props": { "value": "low", "text": "Low" }
            },
            {
              "type": "option",
              "id": "quality-high",
              "layout": { "width": 120, "height": 28 },
              "props": { "value": "high", "text": "High" }
            }
          ]
        },
        {
          "type": "button",
          "id": "popup-anchor",
          "layout": { "position": [175, 20], "width": 120, "height": 30 },
          "props": { "text": "Popup" }
        },
        {
          "type": "popup",
          "id": "info-popup",
          "layout": { "width": 100, "height": 42 },
          "props": {
            "anchor": "popup-anchor",
            "open": { "bind": "popup_open", "mode": "two_way" }
          },
          "on": { "cancel": "popup-cancel" },
          "children": [{
            "type": "panel",
            "layout": { "width": 100, "height": 42 }
          }]
        },
        {
          "type": "button",
          "id": "menu-anchor",
          "layout": { "position": [175, 90], "width": 120, "height": 30 },
          "props": { "text": "Menu" }
        },
        {
          "type": "menu",
          "id": "action-menu",
          "layout": { "mode": "column", "width": 120, "height": 60 },
          "props": {
            "anchor": "menu-anchor",
            "open": { "bind": "menu_open", "mode": "two_way" }
          },
          "on": { "cancel": "menu-cancel" },
          "children": [
            {
              "type": "menu-item",
              "id": "menu-one",
              "layout": { "width": 120, "height": 30 },
              "props": { "text": "One" },
              "on": { "click": "menu-one-action" }
            },
            {
              "type": "menu-item",
              "id": "menu-two",
              "layout": { "width": 120, "height": 30 },
              "props": { "text": "Two" },
              "on": { "click": "menu-two-action" }
            }
          ]
        },
        {
          "type": "button",
          "id": "transient-blocker",
          "layout": {
            "position": [20, 114], "width": 120, "height": 56, "z": 999
          },
          "props": { "text": "Behind listbox" },
          "on": { "click": "blocker-click" }
        }
      ]
    }
  })JSON";
  assert(assets.registerUiDocumentAsset(
      "ui/transient-widgets", {.canonical_json_utf8 = document}));

  karma::ui::UiSystemConfig config;
  config.enabled = true;
  config.hot_reload = false;
  karma::ui::System ui(assets, nullptr, config);
  const auto opened = ui.open("ui/transient-widgets");
  assert(opened);
  assert(ui.set(opened.document, "quality", "low"));
  assert(ui.set(opened.document, "select_open", false));
  assert(ui.set(opened.document, "popup_open", false));
  assert(ui.set(opened.document, "menu_open", false));

  const auto tooltip_anchor = ui.findById(opened.document, "tooltip-anchor");
  const auto tooltip = ui.findById(opened.document, "hover-tooltip");
  const auto select = ui.findById(opened.document, "quality-select");
  const auto option_low = ui.findById(opened.document, "quality-low");
  const auto option_high = ui.findById(opened.document, "quality-high");
  const auto popup_anchor = ui.findById(opened.document, "popup-anchor");
  const auto popup = ui.findById(opened.document, "info-popup");
  const auto menu_anchor = ui.findById(opened.document, "menu-anchor");
  const auto menu = ui.findById(opened.document, "action-menu");
  const auto menu_one = ui.findById(opened.document, "menu-one");
  const auto menu_two = ui.findById(opened.document, "menu-two");
  assert(tooltip_anchor && tooltip && select && option_low && option_high &&
         popup_anchor && popup && menu_anchor && menu && menu_one && menu_two);

  int select_cancels = 0;
  int popup_cancels = 0;
  int menu_cancels = 0;
  int menu_two_actions = 0;
  int blocker_clicks = 0;
  assert(ui.onAction(opened.document, "select-cancel",
                     [&](const karma::ui::ActionEvent&) { ++select_cancels; }));
  assert(ui.onAction(opened.document, "popup-cancel",
                     [&](const karma::ui::ActionEvent&) { ++popup_cancels; }));
  assert(ui.onAction(opened.document, "menu-cancel",
                     [&](const karma::ui::ActionEvent&) { ++menu_cancels; }));
  assert(ui.onAction(opened.document, "menu-two-action",
                     [&](const karma::ui::ActionEvent&) { ++menu_two_actions; }));
  assert(ui.onAction(opened.document, "blocker-click",
                     [&](const karma::ui::ActionEvent&) { ++blocker_clicks; }));

  karma::rendering::UIDrawData draw_data;
  const auto build = [&](float dt = 0.0f) {
    SystemTestAccess::buildFrame(ui, dt, 320, 220, 320, 220, 1.0f, 1.0f,
                                 draw_data);
  };
  build();
  assert(accessible(ui, option_low) == nullptr);
  assert(accessible(ui, popup) == nullptr);
  assert(accessible(ui, menu) == nullptr);
  assert(accessible(ui, tooltip) == nullptr);

  clickElement(ui, select);
  build();
  assert(ui.get(opened.document, "select_open") == karma::ui::Value(true));
  assert(accessible(ui, select)->open);
  assert(hasAccessibilityAction(*accessible(ui, select),
                                karma::ui::AccessibilityAction::Collapse));
  assert(accessible(ui, option_low) != nullptr);
  assert(accessible(ui, option_high)->bounds.y < accessible(ui, select)->bounds.y);
  assert(containsVertexColor(draw_data, 0xff563412u));
  assert(containsVertexColor(draw_data, 0xffefcdabu));
  clickElement(ui, option_high);
  build();
  assert(blocker_clicks == 0);
  assert(ui.get(opened.document, "quality") == karma::ui::Value("high"));
  assert(ui.get(opened.document, "select_open") == karma::ui::Value(false));
  assert(accessible(ui, option_high) == nullptr);
  assert(accessible(ui, select)->focused);

  clickElement(ui, select);
  build();
  assert(accessible(ui, option_high)->focused);
  assert(ui.set(opened.document, "select_open", false));
  build();
  assert(accessible(ui, option_high) == nullptr);
  assert(accessible(ui, select)->focused);

  clickElement(ui, select);
  build();
  assert(SystemTestAccess::processEvent(
      ui, Event{.type = EventType::KeyDown, .key = karma::platform::Key::Escape}));
  build();
  assert(ui.get(opened.document, "quality") == karma::ui::Value("high"));
  assert(ui.get(opened.document, "select_open") == karma::ui::Value(false));
  assert(select_cancels == 1);

  clickElement(ui, popup_anchor);
  build();
  assert(ui.get(opened.document, "popup_open") == karma::ui::Value(true));
  const auto popup_anchor_bounds = accessible(ui, popup_anchor)->bounds;
  const auto popup_bounds = accessible(ui, popup)->bounds;
  assert(nearlyEqual(popup_bounds.x, popup_anchor_bounds.x));
  assert(nearlyEqual(popup_bounds.y,
                     popup_anchor_bounds.y + popup_anchor_bounds.height));
  assert(SystemTestAccess::processEvent(
      ui, Event{.type = EventType::MouseButtonDown, .x = 5.0, .y = 210.0}));
  build();
  assert(ui.get(opened.document, "popup_open") == karma::ui::Value(false));
  assert(popup_cancels == 1);

  clickElement(ui, popup_anchor);
  build();
  assert(SystemTestAccess::processEvent(
      ui, Event{.type = EventType::KeyDown, .key = karma::platform::Key::Escape}));
  build();
  assert(ui.get(opened.document, "popup_open") == karma::ui::Value(false));
  assert(popup_cancels == 2);

  clickElement(ui, menu_anchor);
  build();
  assert(ui.get(opened.document, "menu_open") == karma::ui::Value(true));
  assert(accessible(ui, menu_one)->focused);
  const auto menu_anchor_bounds = accessible(ui, menu_anchor)->bounds;
  const auto menu_bounds = accessible(ui, menu)->bounds;
  assert(nearlyEqual(menu_bounds.x, menu_anchor_bounds.x));
  assert(nearlyEqual(menu_bounds.y,
                     menu_anchor_bounds.y + menu_anchor_bounds.height));
  assert(SystemTestAccess::processEvent(
      ui, Event{.type = EventType::KeyDown, .key = karma::platform::Key::Down}));
  build();
  assert(accessible(ui, menu_two)->focused);
  assert(SystemTestAccess::processEvent(
      ui, Event{.type = EventType::KeyDown, .key = karma::platform::Key::Enter}));
  build();
  assert(menu_two_actions == 1);
  assert(ui.get(opened.document, "menu_open") == karma::ui::Value(false));
  assert(accessible(ui, menu_anchor)->focused);

  clickElement(ui, menu_anchor);
  build();
  assert(SystemTestAccess::processEvent(
      ui, Event{.type = EventType::MouseButtonDown, .x = 5.0, .y = 210.0}));
  build();
  assert(ui.get(opened.document, "menu_open") == karma::ui::Value(false));
  assert(menu_cancels == 1);

  const auto tooltip_anchor_bounds = accessible(ui, tooltip_anchor)->bounds;
  const double tooltip_x =
      tooltip_anchor_bounds.x + tooltip_anchor_bounds.width * 0.5;
  const double tooltip_y =
      tooltip_anchor_bounds.y + tooltip_anchor_bounds.height * 0.5;
  assert(SystemTestAccess::processEvent(
      ui, Event{.type = EventType::MouseMove, .x = tooltip_x, .y = tooltip_y}));
  build(0.39f);
  assert(accessible(ui, tooltip) == nullptr);
  build(0.02f);
  const auto* tooltip_accessible = accessible(ui, tooltip);
  assert(tooltip_accessible != nullptr);
  assert(tooltip_accessible->role == karma::ui::AccessibilityRole::Tooltip);
  assert(tooltip_accessible->bounds.y >=
         tooltip_anchor_bounds.y + tooltip_anchor_bounds.height - 0.01f);
  (void)SystemTestAccess::processEvent(
      ui, Event{.type = EventType::MouseMove, .x = 5.0, .y = 210.0});
  build();
  assert(accessible(ui, tooltip) == nullptr);
}

void testKeyedVirtualList() {
  using karma::ui::detail::SystemTestAccess;

  karma::assets::AssetRegistry assets;
  const std::string document = R"JSON({
    "format": "karma.ui.document",
    "version": 2,
    "root": {
      "type": "body",
      "layout": { "mode": "overlay", "width": 220, "height": 140 },
      "children": [{
        "type": "list",
        "id": "virtual-list",
        "layout": { "position": [10, 10], "width": 160, "height": 96 },
        "props": {
          "items": { "bind": "rows" },
          "item": "row",
          "key": { "expr": "row.id" },
          "item_extent": 24,
          "overscan": 1,
          "template": {
            "type": "button",
            "layout": { "width": "100%", "height": 24 },
            "props": { "text": { "bind": "row.name" } }
          }
        }
      }]
    }
  })JSON";
  assert(assets.registerUiDocumentAsset(
      "ui/virtual-list", {.canonical_json_utf8 = document}));

  karma::ui::UiSystemConfig config;
  config.enabled = true;
  config.hot_reload = false;
  karma::ui::System ui(assets, nullptr, config);
  const auto opened = ui.open("ui/virtual-list");
  assert(opened);

  karma::ui::Value::Array rows;
  for (int index = 0; index < 100; ++index) {
    rows.emplace_back(karma::ui::Value::Object{
        {"id", index}, {"name", "Row " + std::to_string(index)}});
  }
  assert(ui.set(opened.document, "rows", rows));
  const auto list = ui.findById(opened.document, "virtual-list");
  assert(list);

  karma::rendering::UIDrawData draw_data;
  const auto build = [&] {
    SystemTestAccess::buildFrame(ui, 0.0f, 220, 140, 220, 140, 1.0f, 1.0f,
                                 draw_data);
  };
  const auto row_handle = [&](std::string_view name) {
    for (const auto& node : ui.accessibilityTree().nodes) {
      if (node.name == name) return node.element;
    }
    return karma::ui::ElementHandle{};
  };
  const auto live_rows = [&] {
    return static_cast<std::size_t>(std::count_if(
        ui.accessibilityTree().nodes.begin(),
        ui.accessibilityTree().nodes.end(), [](const auto& node) {
          return node.role == karma::ui::AccessibilityRole::Button &&
                 node.name.rfind("Row ", 0u) == 0u;
        }));
  };

  build();
  const auto* list_accessible = accessible(ui, list);
  assert(list_accessible != nullptr);
  assert(list_accessible->role == karma::ui::AccessibilityRole::Scroll);
  assert(hasAccessibilityAction(*list_accessible,
                                karma::ui::AccessibilityAction::Scroll));
  assert(list_accessible->scroll_max_y.value_or(0.0) > 2200.0);
  assert(live_rows() >= 4u && live_rows() <= 6u);
  const auto original_row_three = row_handle("Row 3");
  assert(original_row_three);

  assert(ui.scrollTo(list, 0.0f, 48.0f));
  build();
  assert(row_handle("Row 3") == original_row_three);
  assert(row_handle("Row 1"));
  assert(live_rows() <= 6u);

  assert(ui.scrollTo(list, 0.0f, 1200.0f));
  build();
  assert(row_handle("Row 50"));
  assert(!row_handle("Row 3"));
  assert(!ui.setText(original_row_three, "stale"));
  assert(live_rows() <= 6u);

  assert(ui.scrollTo(list, 0.0f, 48.0f));
  build();
  const auto recycled_row_three = row_handle("Row 3");
  assert(recycled_row_three && recycled_row_three != original_row_three);

  std::swap(rows[2], rows[3]);
  assert(ui.set(opened.document, "rows", rows));
  build();
  assert(row_handle("Row 3") == recycled_row_three);
  assert(live_rows() <= 6u);
}

void testLooseFileOpenSandboxAndHotReload() {
  using karma::ui::detail::SystemTestAccess;

  TempDirectory temporary{makeTempDirectory("karma_ui_loose_files")};
  const std::filesystem::path root = temporary.path / "ui-root";
  std::filesystem::create_directories(root);
  const std::filesystem::path document_path = root / "menu.kui.json5";
  const std::filesystem::path theme_path = root / "theme.kstyle.json5";
  const std::filesystem::path outside_path =
      temporary.path / "outside.kui.json5";

  const std::string loose_document = R"JSON({
    "format": "karma.ui.document",
    "version": 2,
    "themes": [{ "file": "theme.kstyle.json5" }],
    "root": {
      "type": "body",
      "layout": { "width": 180, "height": 90 },
      "children": [{
        "type": "button",
        "id": "loose-button",
        "styles": ["loose-button"],
        "layout": { "width": 100, "height": 40 },
        "props": { "text": "Loose" }
      }]
    }
  })JSON";
  const std::string green_theme = R"JSON({
    "format": "karma.ui.theme",
    "version": 2,
    "styles": {
      "loose-button": {
        "appearance": { "box": { "background_color": "#00ff00" } }
      }
    }
  })JSON";
  const std::string magenta_theme = R"JSON({
    "format": "karma.ui.theme",
    "version": 2,
    "styles": {
      "loose-button": {
        "appearance": { "box": { "background_color": "#ff00ff" } }
      }
    }
  })JSON";
  writeTextFile(document_path, loose_document);
  writeTextFile(theme_path, green_theme);
  writeTextFile(outside_path, R"JSON({
    "format": "karma.ui.document",
    "version": 2,
    "root": { "type": "body" }
  })JSON");

  karma::assets::AssetRegistry assets;
  karma::ui::UiSystemConfig config;
  config.enabled = true;
  config.hot_reload = true;
  config.source_poll_interval = std::chrono::milliseconds{0};
  config.development_files.enabled = true;
  config.development_files.roots = {root};
  config.development_files.debounce = std::chrono::milliseconds{0};
  config.development_files.polling_fallback = std::chrono::milliseconds{0};
  karma::ui::System ui(assets, nullptr, config);

  const auto escaped = ui.openFile("../outside.kui.json5");
  assert(!escaped && !escaped.diagnostics.empty());
  const auto absolute = ui.openFile(outside_path);
  assert(!absolute && !absolute.diagnostics.empty());

  const auto opened = ui.openFile("menu.kui.json5");
  assert(opened && opened.diagnostics.empty());
  const auto document = opened.document;
  const auto button = ui.findById(document, "loose-button");
  assert(button);

  karma::rendering::UIDrawData draw_data;
  const auto build = [&] {
    SystemTestAccess::buildFrame(ui, 1.0f / 60.0f, 180, 90, 180, 90,
                                 1.0f, 1.0f, draw_data);
  };
  build();
  assert(containsVertexColor(draw_data, 0xff00ff00u));

  writeTextFile(theme_path,
                R"JSON({"format":"karma.ui.theme","version":2,"styles":)JSON");
  build();
  assert(ui.findById(document, "loose-button") == button);
  assert(containsVertexColor(draw_data, 0xff00ff00u));
  assert(!ui.diagnostics(document).empty());

  writeTextFile(theme_path, magenta_theme);
  build();
  assert(ui.findById(document, "loose-button") == button);
  assert(containsVertexColor(draw_data, 0xffff00ffu));
  assert(ui.diagnostics(document).empty());

  writeTextFile(document_path, R"JSON({
    "format": "karma.ui.document",
    "version": 2,
    "themes": [{ "file": "theme.kstyle.json5" }],
    "root": { "type": "script" }
  })JSON");
  build();
  assert(ui.findById(document, "loose-button") == button);
  assert(containsVertexColor(draw_data, 0xffff00ffu));
  assert(!ui.diagnostics(document).empty());

  writeTextFile(document_path, loose_document);
  build();
  assert(ui.findById(document, "loose-button"));
  assert(containsVertexColor(draw_data, 0xffff00ffu));
  assert(ui.diagnostics(document).empty());
}

void testHotReloadPreservesUnboundWidgetState() {
  using karma::platform::Event;
  using karma::platform::EventType;
  using karma::ui::detail::SystemTestAccess;

  TempDirectory temporary{makeTempDirectory("karma_ui_reload_state")};
  const std::filesystem::path root = temporary.path / "ui";
  std::filesystem::create_directories(root);
  const std::filesystem::path document_path = root / "state.kui.json5";
  const std::filesystem::path theme_path = root / "state.kstyle.json5";
  const std::string source = R"JSON({
    format: 'karma.ui.document', version: 2,
    themes: [{file: 'state.kstyle.json5'}],
    root: {
      type: 'body', layout: {mode: 'overlay', width: 260, height: 170},
      children: [
        {
          type: 'disclosure', id: 'details',
          layout: {position: [8, 8], width: 80, height: 60},
          props: {expanded: false},
          children: [
            {type: 'text', id: 'details-header', props: {text: 'Before'}},
            {type: 'text', id: 'details-content', props: {text: 'Content'}},
          ],
        },
        {
          type: 'select', id: 'chooser',
          styles: ['reload-focus'],
          layout: {position: [8, 82], width: 90, height: 30},
          props: {value: 'a'},
          children: [
            {type: 'option', id: 'choice-a', props: {value: 'a', text: 'A'}},
            {type: 'option', id: 'choice-b', props: {value: 'b', text: 'B'}},
          ],
        },
        {
          type: 'window', id: 'floating',
          layout: {position: [120, 20], width: 110, height: 90},
          props: {title: 'State', resizable: true},
        },
      ],
    },
  })JSON";
  writeTextFile(document_path, source);
  writeTextFile(theme_path, R"JSON({
    format: 'karma.ui.theme', version: 2,
    styles: {
      'reload-focus': {
        appearance: {
          box: {background_color: '#101010'},
          states: {focus: {box: {background_color: '#ff00ff'}}},
        },
      },
    },
  })JSON");

  karma::assets::AssetRegistry assets;
  karma::ui::UiSystemConfig config;
  config.enabled = true;
  config.hot_reload = true;
  config.source_poll_interval = std::chrono::milliseconds{0};
  config.development_files.enabled = true;
  config.development_files.roots = {root};
  config.development_files.debounce = std::chrono::milliseconds{0};
  config.development_files.polling_fallback = std::chrono::milliseconds{0};
  karma::ui::System ui(assets, nullptr, config);
  const auto opened = ui.openFile("state.kui.json5");
  assert(opened);
  karma::rendering::UIDrawData draw_data;
  const auto build = [&] {
    SystemTestAccess::buildFrame(ui, 1.0f / 60.0f, 260, 170, 260, 170,
                                 1.0f, 1.0f, draw_data);
  };
  build();

  const auto details = ui.findById(opened.document, "details");
  const auto window = ui.findById(opened.document, "floating");
  assert(details && window);
  clickElement(ui, details);
  build();
  assert(accessible(ui, details)->expanded);
  assert(accessible(ui, ui.findById(opened.document, "details-content")) !=
         nullptr);

  const auto before = accessible(ui, window)->bounds;
  const double start_x = before.x + 30.0;
  const double start_y = before.y + 12.0;
  assert(SystemTestAccess::processEvent(
      ui, Event{.type = EventType::MouseMove, .x = start_x, .y = start_y}));
  assert(SystemTestAccess::processEvent(
      ui, Event{.type = EventType::MouseButtonDown,
                .x = start_x, .y = start_y}));
  assert(SystemTestAccess::processEvent(
      ui, Event{.type = EventType::MouseMove,
                .x = start_x + 18.0, .y = start_y + 14.0}));
  assert(SystemTestAccess::processEvent(
      ui, Event{.type = EventType::MouseButtonUp,
                .x = start_x + 18.0, .y = start_y + 14.0}));
  build();
  const auto moved = accessible(ui, window)->bounds;
  assert(moved.x > before.x + 15.0f && moved.y > before.y + 11.0f);

  const auto chooser = ui.findById(opened.document, "chooser");
  assert(chooser);
  clickElement(ui, chooser);
  assert(ui.focus(chooser));
  build();
  assert(accessible(ui, chooser)->open);
  assert(accessible(ui, chooser)->focused);
  assert(containsVertexColor(draw_data, 0xffff00ffu));
  assert(accessible(ui, ui.findById(opened.document, "choice-a")) != nullptr);

  std::string changed = source;
  const std::size_t text = changed.find("Before");
  assert(text != std::string::npos);
  changed.replace(text, 6u, "After");
  writeTextFile(document_path, changed);
  build();

  const auto reloaded_details = ui.findById(opened.document, "details");
  const auto reloaded_chooser = ui.findById(opened.document, "chooser");
  const auto reloaded_window = ui.findById(opened.document, "floating");
  assert(reloaded_details && reloaded_chooser && reloaded_window);
  assert(reloaded_details != details && reloaded_window != window);
  assert(accessible(ui, reloaded_details)->expanded);
  assert(accessible(ui,
                    ui.findById(opened.document, "details-content")) != nullptr);
  assert(accessible(ui, reloaded_chooser)->open);
  assert(accessible(ui, reloaded_chooser)->focused);
  assert(containsVertexColor(draw_data, 0xffff00ffu));
  assert(accessible(ui, ui.findById(opened.document, "choice-a")) != nullptr);
  const auto restored = accessible(ui, reloaded_window)->bounds;
  assert(nearlyEqual(restored.x, moved.x) && nearlyEqual(restored.y, moved.y));
  assert(nearlyEqual(restored.width, moved.width) &&
         nearlyEqual(restored.height, moved.height));
}

void testFileBackedPackageHotReload() {
  using karma::ui::detail::SystemTestAccess;

  TempDirectory temporary{makeTempDirectory("karma_ui_source_reload")};
  const std::filesystem::path manifest =
      temporary.path / "assets.package.json";
  const std::filesystem::path document_path =
      temporary.path / "live.kui.json5";
  const std::filesystem::path theme_path =
      temporary.path / "live.kstyle.json5";
  const std::filesystem::path alternate_theme_path =
      temporary.path / "alternate.kstyle.json5";
  writeTextFile(manifest, R"JSON({
    "version": 1,
    "assets": [
      { "type": "ui_document", "key": "ui/live", "path": "live.kui.json5" },
      { "type": "ui_theme", "key": "ui/live-style", "path": "live.kstyle.json5" },
      { "type": "ui_theme", "key": "ui/alternate-style", "path": "alternate.kstyle.json5" }
    ]
  })JSON");
  writeTextFile(document_path, R"JSON({
    "format": "karma.ui.document",
    "version": 2,
    "themes": [{ "asset": "ui/live-style" }],
    "root": {
      "type": "body",
      "children": [{
        "type": "button",
        "id": "old",
        "styles": ["old"],
        "props": { "text": "Old" }
      }]
    }
  })JSON");
  writeTextFile(theme_path, R"JSON({
    "format": "karma.ui.theme",
    "version": 2,
    "defaults": {
      "body": { "layout": { "width": 160, "height": 90 } }
    },
    "styles": {
      "old": {
        "layout": { "width": 80, "height": 30 },
        "appearance": { "box": { "background_color": "#00ff00" } }
      }
    }
  })JSON");
  writeTextFile(alternate_theme_path, R"JSON({
    "format": "karma.ui.theme",
    "version": 2,
    "defaults": {
      "body": { "layout": { "width": 160, "height": 90 } }
    },
    "styles": {
      "new": {
        "layout": { "width": 80, "height": 30 },
        "appearance": { "box": { "background_color": "#ff00ff" } }
      }
    }
  })JSON");

  karma::assets::AssetPackageOptions package_options;
  package_options.cache.enabled = true;
  package_options.cache.root = temporary.path / "cache";
  std::string diagnostic;
  karma::assets::AssetRegistry cache_warm_assets;
  const auto cache_warm_package = karma::assets::importAssetPackage(
      cache_warm_assets, manifest, package_options, &diagnostic);
  assert(cache_warm_package.has_value() && diagnostic.empty());

  // Import a second registry from the package cache. A source-package cache
  // hit must recover development paths without serializing them into blobs.
  karma::assets::AssetRegistry assets;
  diagnostic.clear();
  const auto package = karma::assets::importAssetPackage(
      assets, manifest, package_options, &diagnostic);
  assert(package.has_value() && diagnostic.empty());

  const auto* registered_document = assets.findUiDocumentAsset("ui/live");
  const auto* registered_theme = assets.findUiThemeAsset("ui/live-style");
  assert(registered_document != nullptr && registered_theme != nullptr);
  const std::string packaged_document_source =
      registered_document->canonical_json_utf8;
  const std::string packaged_document_hash = registered_document->content_hash;
  const std::string packaged_theme_source = registered_theme->canonical_json_utf8;
  const std::string packaged_theme_hash = registered_theme->content_hash;

  karma::ui::UiSystemConfig config;
  config.enabled = true;
  config.hot_reload = true;
  config.source_poll_interval = std::chrono::milliseconds{0};
  karma::ui::System ui(assets, nullptr, config);
  const auto opened = ui.open("ui/live");
  assert(opened);

  karma::rendering::UIDrawData draw_data;
  const auto build = [&] {
    SystemTestAccess::buildFrame(ui, 1.0f / 60.0f, 160, 90, 160, 90,
                                 1.0f, 1.0f, draw_data);
  };
  build();
  const auto old = ui.findById(opened.document, "old");
  assert(old && containsVertexColor(draw_data, 0xff00ff00u));

  // Invalid direct theme JSON retains the last-good rules and is not written
  // into the
  // registry. The next valid edit is staged by the existing worker.
  writeTextFile(theme_path,
                R"JSON({"format":"karma.ui.theme","version":2,"styles":)JSON");
  build();
  assert(ui.findById(opened.document, "old") == old);
  assert(containsVertexColor(draw_data, 0xff00ff00u));
  assert(!ui.diagnostics(opened.document).empty());

  writeTextFile(theme_path, R"JSON({
    "format": "karma.ui.theme",
    "version": 2,
    "defaults": {
      "body": { "layout": { "width": 160, "height": 90 } }
    },
    "styles": {
      "old": {
        "layout": { "width": 80, "height": 30 },
        "appearance": { "box": { "background_color": "#ff00ff" } }
      },
      "new": {
        "layout": { "width": 80, "height": 30 },
        "appearance": { "box": { "background_color": "#ff00ff" } }
      }
    }
  })JSON");
  build();
  assert(containsVertexColor(draw_data, 0xffff00ffu));
  assert(ui.diagnostics(opened.document).empty());

  // Invalid direct document JSON retains the tree, handle, model, and
  // last-good theme.
  writeTextFile(document_path, R"JSON({
    "format": "karma.ui.document",
    "version": 2,
    "root": { "type": "script" }
  })JSON");
  build();
  assert(ui.findById(opened.document, "old") == old);
  const std::size_t invalid_diagnostic_count =
      ui.diagnostics(opened.document).size();
  assert(invalid_diagnostic_count > 0u);
  build();
  assert(ui.diagnostics(opened.document).size() == invalid_diagnostic_count);

  writeTextFile(document_path, R"JSON({
    "format": "karma.ui.document",
    "version": 2,
    "themes": [{ "asset": "ui/alternate-style" }],
    "root": {
      "type": "body",
      "children": [{
        "type": "button",
        "id": "new",
        "styles": ["new"],
        "props": { "text": "New" }
      }]
    }
  })JSON");
  // The first worker pass discovers a newly referenced registered theme;
  // the next poll snapshots that source and commits the complete transaction.
  build();
  assert(ui.findById(opened.document, "old") == old);
  build();
  assert(!ui.findById(opened.document, "old"));
  assert(ui.findById(opened.document, "new"));
  assert(ui.diagnostics(opened.document).empty());

  // Runtime polling consumes file snapshots only; the packaged registry asset
  // remains the immutable source used by registry-only and baked consumers.
  registered_document = assets.findUiDocumentAsset("ui/live");
  registered_theme = assets.findUiThemeAsset("ui/live-style");
  assert(registered_document->canonical_json_utf8 == packaged_document_source);
  assert(registered_document->content_hash == packaged_document_hash);
  assert(registered_theme->canonical_json_utf8 == packaged_theme_source);
  assert(registered_theme->content_hash == packaged_theme_hash);

  // Baked blobs intentionally omit development source ownership. Editing the
  // original package after restore must not affect a baked UI instance.
  karma::assets::AssetPackageBakeOptions bake_options;
  bake_options.package_id = "ui-source-isolation";
  bake_options.import_options.cache.enabled = false;
  bake_options.import_options.cache.root.clear();
  const std::filesystem::path baked_path = temporary.path / "baked";
  diagnostic.clear();
  assert(karma::assets::bakeAssetPackage(
      manifest, baked_path, bake_options, &diagnostic));
  assert(diagnostic.empty());

  karma::assets::AssetRegistry baked_assets;
  const auto baked_package = karma::assets::importBakedAssetPackage(
      baked_assets, baked_path, &diagnostic);
  assert(baked_package.has_value() && diagnostic.empty());
  karma::ui::System baked_ui(baked_assets, nullptr, config);
  const auto baked_opened = baked_ui.open("ui/live");
  assert(baked_opened && baked_ui.findById(baked_opened.document, "new"));
  writeTextFile(document_path, R"JSON({
    "format": "karma.ui.document",
    "version": 2,
    "themes": [{ "asset": "ui/live-style" }],
    "root": {
      "type": "body",
      "children": [{
        "type": "button",
        "id": "after-bake",
        "styles": ["old"],
        "props": { "text": "After bake" }
      }]
    }
  })JSON");
  SystemTestAccess::buildFrame(baked_ui, 1.0f / 60.0f, 160, 90, 160, 90,
                               1.0f, 1.0f, draw_data);
  assert(baked_ui.findById(baked_opened.document, "new"));
  assert(!baked_ui.findById(baked_opened.document, "after-bake"));
}

}  // namespace

int main() {
  testValueSemantics();
  testNativeCursorShapes();
  testRetainedFrameWorkDiagnostics();
  testActiveMotionNodeTracking();
  testRetainedRuntimeChildOrder();
  testVerticalSliderInput();
  testJsonPropMappingsAndAxisScrolling();
  testRtlHorizontalSliderInput();
  testDocumentsAndGenerationalHandles();
  testDocumentControllerOwnership();
  testNativeDemoPackage();
  testPointerHoverRouting();
  testRetainedRuntimeIntegration();
  testConfigurableGamepadNavigationBindings();
  testScrollbarsAndNestedScrolling();
  testDisclosureWindowAndSplitterWidgets();
  testTabsAndTreeWidgets();
  testSelectPopupMenuAndTooltipWidgets();
  testKeyedVirtualList();
  testLooseFileOpenSandboxAndHotReload();
  testHotReloadPreservesUnboundWidgetState();
  testFileBackedPackageHotReload();
  std::cout << "ui tests passed\n";
  return 0;
}
