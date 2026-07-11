#pragma once

#include "karma/math.h"
#include "karma/platform.h"
#include "karma/rendering.h"

#include <chrono>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <filesystem>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <type_traits>
#include <vector>

namespace karma::app {
class EngineApp;
}

namespace karma::assets {
class AssetRegistry;
}

namespace karma::ui {

/// Provider-independent controller bindings used by native UI navigation.
/// Set an individual binding to `Unknown` to disable it.
struct GamepadNavigationBindings {
  platform::GamepadButton up = platform::GamepadButton::DpadUp;
  platform::GamepadButton right = platform::GamepadButton::DpadRight;
  platform::GamepadButton down = platform::GamepadButton::DpadDown;
  platform::GamepadButton left = platform::GamepadButton::DpadLeft;
  platform::GamepadAxis horizontal_axis = platform::GamepadAxis::LeftX;
  platform::GamepadAxis vertical_axis = platform::GamepadAxis::LeftY;
  platform::GamepadButton accept = platform::GamepadButton::A;
  platform::GamepadButton cancel = platform::GamepadButton::B;
  platform::GamepadButton page_previous = platform::GamepadButton::LeftShoulder;
  platform::GamepadButton page_next = platform::GamepadButton::RightShoulder;
};

/// Sandboxed loose-file authoring used by the native UI development runtime.
/// The public API remains present in shipping builds, but calls fail with a
/// diagnostic when this capability is disabled.
struct DevelopmentUiFilesConfig {
#if defined(NDEBUG)
  bool enabled = false;
#else
  bool enabled = true;
#endif
  std::vector<std::filesystem::path> roots;
  std::chrono::milliseconds debounce{75};
  std::chrono::milliseconds polling_fallback{250};
};

/// Configuration for Karma's retained-mode native UI runtime.
struct UiSystemConfig {
#if defined(KARMA_HEADLESS) || !defined(KARMA_ENABLE_NATIVE_UI)
  bool enabled = false;
#else
  bool enabled = true;
#endif
#if defined(NDEBUG)
  bool hot_reload = false;
#else
  bool hot_reload = true;
#endif
  std::string locale = "en";
  DevelopmentUiFilesConfig development_files{};
  /// Registry-backed assets without development source ownership are checked
  /// at this interval. Loose files use development_files.polling_fallback when
  /// a native watcher is unavailable.
  std::chrono::milliseconds source_poll_interval{250};
  int glyph_atlas_page_width = 1024;
  int glyph_atlas_page_height = 1024;
  std::size_t glyph_atlas_budget_bytes = 64u * 1024u * 1024u;
  std::size_t svg_raster_budget_bytes = 64u * 1024u * 1024u;
  /// Maximum CPU memory retained by cached native-UI paint fragments.
  std::size_t retained_paint_budget_bytes = 32u * 1024u * 1024u;
  float motion_scale = 1.0f;
  GamepadNavigationBindings gamepad_navigation{};
};

namespace detail {
inline constexpr std::uint32_t kInvalidHandleIndex =
    std::numeric_limits<std::uint32_t>::max();
[[nodiscard]] constexpr bool validHandleFields(std::uint32_t index,
                                               std::uint32_t generation) noexcept {
  return index != kInvalidHandleIndex && generation != 0;
}
struct SystemTestAccess;
struct SystemLifetime;
}

/// Generational handle for an open document instance.
struct DocumentHandle {
  std::uint32_t index = detail::kInvalidHandleIndex;
  std::uint32_t generation = 0;

  [[nodiscard]] constexpr bool valid() const noexcept {
    return detail::validHandleFields(index, generation);
  }
  explicit constexpr operator bool() const { return valid(); }
  friend constexpr bool operator==(DocumentHandle, DocumentHandle) = default;
};

/// Generational handle for an element in a document instance.
struct ElementHandle {
  std::uint32_t index = detail::kInvalidHandleIndex;
  std::uint32_t generation = 0;

  [[nodiscard]] constexpr bool valid() const noexcept {
    return detail::validHandleFields(index, generation);
  }
  explicit constexpr operator bool() const { return valid(); }
  friend constexpr bool operator==(ElementHandle, ElementHandle) = default;
};

/// Generational handle for a registered callback.
struct ListenerHandle {
  std::uint32_t index = detail::kInvalidHandleIndex;
  std::uint32_t generation = 0;

  [[nodiscard]] constexpr bool valid() const noexcept {
    return detail::validHandleFields(index, generation);
  }
  explicit constexpr operator bool() const { return valid(); }
  friend constexpr bool operator==(ListenerHandle, ListenerHandle) = default;
};

/// Generational handle for a texture owned by the native UI resource cache.
/// The creating System retains ownership until destroyImage() or shutdown;
/// copying this handle does not share or extend that lifetime.
struct DynamicImageHandle {
  std::uint32_t index = detail::kInvalidHandleIndex;
  std::uint32_t generation = 0;

  [[nodiscard]] constexpr bool valid() const noexcept {
    return detail::validHandleFields(index, generation);
  }
  explicit constexpr operator bool() const { return valid(); }
  friend constexpr bool operator==(DynamicImageHandle, DynamicImageHandle) = default;
};

/// Image reference used by element properties and reactive values.
/// Asset keys are copied, dynamic-image handles remain owned by their System,
/// and render-target IDs are borrowed from the renderer.
struct ImageSource {
  enum class Kind : std::uint8_t {
    None,
    Asset,
    Dynamic,
    RenderTarget,
  };

  Kind kind = Kind::None;
  std::string asset_key;
  DynamicImageHandle dynamic_image{};
  rendering::RenderTargetId render_target = rendering::kDefaultRenderTarget;

  static ImageSource asset(std::string key);
  /// Does not transfer ownership or extend the dynamic image's lifetime.
  static ImageSource dynamic(DynamicImageHandle image);
  /// Borrows a render target. The caller must keep it valid while referenced;
  /// native UI never destroys the target.
  static ImageSource renderTarget(rendering::RenderTargetId target);

  explicit operator bool() const { return kind != Kind::None; }
  friend bool operator==(const ImageSource&, const ImageSource&) = default;
};

/// Dynamically typed value used by the reactive document model.
class Value {
 public:
  using Integer = std::int64_t;
  using Array = std::vector<Value>;
  using Object = std::unordered_map<std::string, Value>;

  enum class Type : std::uint8_t {
    Null,
    Boolean,
    Integer,
    Number,
    String,
    Color,
    Image,
    Array,
    Object,
  };

  Value() = default;
  Value(std::nullptr_t) {}
  Value(bool value);
  Value(Integer value);
  template <std::integral T>
    requires(!std::same_as<std::remove_cv_t<T>, bool> &&
             !std::same_as<std::remove_cv_t<T>, Integer>)
  Value(T value) {
    if constexpr (std::is_unsigned_v<T>) {
      if (value > static_cast<std::make_unsigned_t<Integer>>(
                      std::numeric_limits<Integer>::max())) {
        *this = Value(static_cast<double>(value));
        return;
      }
    }
    *this = Value(static_cast<Integer>(value));
  }
  Value(float value) : Value(static_cast<double>(value)) {}
  Value(double value);
  Value(const char* value);
  Value(std::string value);
  Value(std::string_view value);
  Value(math::Color value);
  Value(ImageSource value);
  Value(Array value);
  Value(Object value);

  [[nodiscard]] Type type() const;
  [[nodiscard]] bool isNull() const { return type() == Type::Null; }
  [[nodiscard]] bool truthy() const;
  [[nodiscard]] std::optional<bool> asBoolean() const;
  [[nodiscard]] std::optional<Integer> asInteger() const;
  [[nodiscard]] std::optional<double> asNumber() const;
  [[nodiscard]] const std::string* asString() const;
  [[nodiscard]] const math::Color* asColor() const;
  [[nodiscard]] const ImageSource* asImage() const;
  [[nodiscard]] const Array* asArray() const;
  [[nodiscard]] Array* asArray();
  [[nodiscard]] const Object* asObject() const;
  [[nodiscard]] Object* asObject();
  [[nodiscard]] std::string toString() const;

  friend bool operator==(const Value& left, const Value& right);
  friend bool operator!=(const Value& left, const Value& right) {
    return !(left == right);
  }

 private:
  struct Storage;
  std::shared_ptr<Storage> storage_;
};

/// One transactional update to a retained document model.
struct ModelUpdate {
  std::string property_path;
  Value value;
};

enum class DiagnosticSeverity : std::uint8_t {
  Info,
  Warning,
  Error,
};

/// Source-located parser, binding, style, or runtime diagnostic.
struct Diagnostic {
  DiagnosticSeverity severity = DiagnosticSeverity::Error;
  std::string code;
  std::string message;
  std::string asset_key;
  std::size_t line = 0;
  std::size_t column = 0;
};

struct OpenDocumentOptions {
  int layer = 0;
  bool visible = true;
  bool modal = false;
};

struct OpenDocumentResult {
  DocumentHandle document{};
  std::vector<Diagnostic> diagnostics;

  [[nodiscard]] bool success() const { return document.valid(); }
  explicit operator bool() const { return success(); }
};

enum class ScrollAlignment : std::uint8_t {
  Nearest,
  Start,
  Center,
  End,
};

enum class EventType : std::uint8_t {
  PointerEnter,
  PointerLeave,
  PointerMove,
  PointerDown,
  PointerUp,
  Click,
  Scroll,
  KeyDown,
  KeyUp,
  GamepadButtonDown,
  GamepadButtonUp,
  GamepadAxisMotion,
  Focus,
  Blur,
  Change,
  Input,
  Cancel,
};

enum class EventPhase : std::uint8_t {
  Capture,
  Target,
  Bubble,
};

/// DOM event passed synchronously to element listeners.
class Event {
 public:
  EventType type = EventType::Click;
  EventPhase phase = EventPhase::Target;
  DocumentHandle document{};
  ElementHandle target{};
  ElementHandle current_target{};
  platform::Key key = platform::Key::Unknown;
  platform::MouseButton pointer_button = platform::MouseButton::Left;
  int gamepad = -1;
  platform::GamepadButton gamepad_button = platform::GamepadButton::Unknown;
  platform::GamepadAxis gamepad_axis = platform::GamepadAxis::Unknown;
  float gamepad_value = 0.0f;
  platform::Modifiers modifiers{};
  double x = 0.0;
  double y = 0.0;
  double delta_x = 0.0;
  double delta_y = 0.0;
  Value value{};

  void stopPropagation() { propagation_stopped_ = true; }
  void preventDefault() { default_prevented_ = true; }
  [[nodiscard]] bool propagationStopped() const { return propagation_stopped_; }
  [[nodiscard]] bool defaultPrevented() const { return default_prevented_; }

  // Runtime cancellation state. Public for value-like event transport; mutate
  // through stopPropagation()/preventDefault().
  bool propagation_stopped_ = false;
  bool default_prevented_ = false;
};

struct ActionEvent {
  DocumentHandle document{};
  ElementHandle target{};
  std::string action;
  Value value{};
};

using ActionCallback = std::function<void(const ActionEvent&)>;
using EventCallback = std::function<void(Event&)>;

struct EventListenerOptions {
  bool capture = false;
};

/// Game-owned localization resolver. Returning nullopt reports a missing key.
/// System borrows a provider passed to setLocalizationProvider(); it never
/// deletes it, so the provider must remain alive until replaced or shutdown.
class LocalizationProvider {
 public:
  virtual ~LocalizationProvider() = default;
  virtual std::optional<std::string> localize(
      std::string_view locale,
      std::string_view key,
      const Value::Object& arguments) = 0;
};

struct AccessibilityBounds {
  float x = 0.0f;
  float y = 0.0f;
  float width = 0.0f;
  float height = 0.0f;
};

enum class AccessibilityRole : std::uint8_t {
  Document,
  Group,
  Text,
  Image,
  Button,
  Toggle,
  Slider,
  Select,
  Option,
  Progress,
  Scroll,
  Window,
  TabList,
  Tab,
  Disclosure,
  Tree,
  TreeItem,
  Separator,
  Menu,
  MenuItem,
  Tooltip,
};

enum class AccessibilityAction : std::uint8_t {
  Focus,
  Press,
  Toggle,
  Increment,
  Decrement,
  SetValue,
  Scroll,
  Expand,
  Collapse,
  Select,
  Dismiss,
};

struct AccessibilityNode {
  ElementHandle element{};
  AccessibilityRole role = AccessibilityRole::Group;
  std::string name;
  std::string description;
  AccessibilityBounds bounds{};
  int focus_order = -1;
  bool focusable = false;
  bool focused = false;
  bool disabled = false;
  bool checked = false;
  bool expanded = false;
  bool selected = false;
  bool open = false;
  std::optional<double> value;
  std::optional<double> minimum;
  std::optional<double> maximum;
  std::optional<double> scroll_x;
  std::optional<double> scroll_y;
  std::optional<double> scroll_max_x;
  std::optional<double> scroll_max_y;
  std::vector<AccessibilityAction> actions;
  std::vector<std::size_t> children;
};

class System;

/// Move-only owner for a retained document and listeners registered through it.
/// Controllers safely become inert when their System is destroyed.
class DocumentController {
 public:
  using ActionMap = std::unordered_map<std::string, ActionCallback>;

  DocumentController() = default;
  ~DocumentController();
  DocumentController(const DocumentController&) = delete;
  DocumentController& operator=(const DocumentController&) = delete;
  DocumentController(DocumentController&& other) noexcept;
  DocumentController& operator=(DocumentController&& other) noexcept;

  [[nodiscard]] DocumentHandle handle() const { return document_; }
  [[nodiscard]] bool valid() const;
  explicit operator bool() const { return valid(); }

  bool close();
  /// Relinquishes document ownership without closing it. Listeners registered
  /// through this controller remain owned by the document and are removed when
  /// that document closes; the controller simply stops tracking them.
  DocumentHandle release();
  bool show();
  bool hide();
  bool setModal(bool modal);
  bool set(std::string_view property_path, Value value);
  bool setMany(std::vector<ModelUpdate> updates);
  [[nodiscard]] std::optional<Value> get(std::string_view property_path) const;
  [[nodiscard]] ElementHandle findById(std::string_view id) const;
  bool focus(ElementHandle element);
  bool scrollTo(ElementHandle element, float x, float y);
  bool scrollBy(ElementHandle element, float x, float y);
  bool scrollIntoView(ElementHandle element,
                      ScrollAlignment horizontal = ScrollAlignment::Nearest,
                      ScrollAlignment vertical = ScrollAlignment::Nearest);
  ListenerHandle onAction(std::string_view action, ActionCallback callback);
  ListenerHandle on(ElementHandle element,
                    EventType type,
                    EventCallback callback,
                    const EventListenerOptions& options = {});
  /// Registers entries in iteration order. A false result leaves any callbacks
  /// registered before the failing entry active and owned by this controller.
  bool bindActions(ActionMap actions);

 private:
  DocumentController(std::weak_ptr<detail::SystemLifetime> lifetime,
                     DocumentHandle document);
  [[nodiscard]] System* system() const;

  std::weak_ptr<detail::SystemLifetime> lifetime_;
  DocumentHandle document_{};
  std::vector<ListenerHandle> listeners_;

  friend class System;
};

struct OpenControllerResult {
  DocumentController controller;
  std::vector<Diagnostic> diagnostics;

  [[nodiscard]] bool success() const { return controller.valid(); }
  explicit operator bool() const { return success(); }
};

/// Read-only semantic snapshot rebuilt with the retained UI tree.
struct AccessibilityTree {
  std::uint64_t generation = 0;
  std::vector<AccessibilityNode> nodes;
  std::vector<std::size_t> roots;
};

/// Work and native-UI stage timings captured for the most recently built frame.
/// Counters are deterministic; timings are diagnostic wall-clock samples.
struct UiFrameDiagnostics {
  std::uint64_t frame = 0u;
  double reconcile_ms = 0.0;
  double style_ms = 0.0;
  double layout_ms = 0.0;
  double placement_ms = 0.0;
  double paint_ms = 0.0;
  double accessibility_ms = 0.0;
  std::size_t reconciled_nodes = 0u;
  std::size_t restyled_nodes = 0u;
  /// Nodes with active transition or keyframe tracks sampled this frame.
  std::size_t advanced_motion_nodes = 0u;
  std::size_t laid_out_nodes = 0u;
  std::size_t placed_nodes = 0u;
  std::size_t rebuilt_fragments = 0u;
  std::size_t accessibility_nodes = 0u;
  std::size_t output_vertices = 0u;
  std::size_t output_commands = 0u;
};

/// First-party retained-mode user-interface system.
class System {
 public:
  /// Borrows the asset registry and optional graphics device. Both must
  /// outlive this System; passing nullptr disables GPU-backed image creation.
  explicit System(assets::AssetRegistry& assets,
                  rendering::GraphicsDevice* graphics = nullptr,
                  UiSystemConfig config = {});
  ~System();

  System(const System&) = delete;
  System& operator=(const System&) = delete;
  System(System&&) noexcept;
  System& operator=(System&&) noexcept;

  OpenDocumentResult open(std::string_view asset_key,
                          const OpenDocumentOptions& options = {});
  OpenDocumentResult openFile(const std::filesystem::path& relative_path,
                              const OpenDocumentOptions& options = {});
  OpenControllerResult openController(
      std::string_view asset_key,
      const OpenDocumentOptions& options = {});
  OpenControllerResult openFileController(
      const std::filesystem::path& relative_path,
      const OpenDocumentOptions& options = {});
  [[nodiscard]] bool isOpen(DocumentHandle document) const;
  bool close(DocumentHandle document);
  bool show(DocumentHandle document);
  bool hide(DocumentHandle document);
  bool setModal(DocumentHandle document, bool modal);

  bool set(DocumentHandle document, std::string_view property_path, Value value);
  bool setMany(DocumentHandle document, std::vector<ModelUpdate> updates);
  [[nodiscard]] std::optional<Value> get(
      DocumentHandle document,
      std::string_view property_path) const;

  [[nodiscard]] ElementHandle findById(DocumentHandle document,
                                       std::string_view id) const;
  bool addClass(ElementHandle element, std::string_view class_name);
  bool removeClass(ElementHandle element, std::string_view class_name);
  bool setText(ElementHandle element, std::string_view text);
  bool setImage(ElementHandle element, const ImageSource& image);
  bool focus(ElementHandle element);
  bool scrollTo(ElementHandle element, float x, float y);
  bool scrollBy(ElementHandle element, float x, float y);
  bool scrollIntoView(ElementHandle element,
                      ScrollAlignment horizontal = ScrollAlignment::Nearest,
                      ScrollAlignment vertical = ScrollAlignment::Nearest);
  bool bringToFront(ElementHandle element);

  ListenerHandle onAction(DocumentHandle document,
                          std::string_view action,
                          ActionCallback callback);
  ListenerHandle on(ElementHandle element,
                    EventType type,
                    EventCallback callback,
                    const EventListenerOptions& options = {});
  bool removeListener(ListenerHandle listener);

  /// Creates a System-owned texture. Upload bytes are consumed synchronously.
  DynamicImageHandle createImage(const rendering::TextureDesc& desc,
                                 const rendering::TextureUploadData& upload);
  /// Replaces a System-owned texture's content synchronously.
  bool updateImage(DynamicImageHandle image,
                   const rendering::TextureUploadData& upload);
  /// Releases the owned texture; copied handles/sources become stale and fail.
  bool destroyImage(DynamicImageHandle image);

  void setLocale(std::string_view locale);
  [[nodiscard]] std::string_view locale() const;
  /// Borrows provider until another provider is set or this System shuts down.
  /// Passing nullptr restores key-as-text fallback localization.
  void setLocalizationProvider(LocalizationProvider* provider);
  /// Returns a System-owned snapshot. Do not retain this reference across a
  /// semantic-tree rebuild or beyond the System lifetime.
  [[nodiscard]] const AccessibilityTree& accessibilityTree() const;
  [[nodiscard]] const UiFrameDiagnostics& frameDiagnostics() const;
  [[nodiscard]] std::vector<Diagnostic> diagnostics(DocumentHandle document) const;

  [[nodiscard]] const UiSystemConfig& config() const;
  void setMotionScale(float scale);

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
  std::shared_ptr<detail::SystemLifetime> lifetime_;

  enum class InputDisposition : std::uint8_t {
    Ignored,
    Consumed,
    CaptureKeyboard,
    CapturePointer,
    CaptureAll,
  };

  struct InputCapture {
    bool keyboard = false;
    bool pointer = false;
    bool gamepad = false;
  };

  InputDisposition processEvent(const platform::Event& event);
  [[nodiscard]] InputCapture inputCapture() const;
  [[nodiscard]] platform::CursorShape cursorShape() const;
  void buildFrame(float dt,
                  int logical_width,
                  int logical_height,
                  int framebuffer_width,
                  int framebuffer_height,
                  float scale_x,
                  float scale_y,
                  rendering::UIDrawData& output,
                  platform::SafeAreaInsets safe_area = {});
  void shutdown();

  friend class app::EngineApp;
  friend class DocumentController;
  friend struct detail::SystemTestAccess;
};

}  // namespace karma::ui
