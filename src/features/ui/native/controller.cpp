#include "karma/ui.h"

#include "features/ui/native/system_lifetime.h"

#include <utility>

namespace karma::ui {
namespace {

template <typename Callback>
auto forwardToSystem(const std::weak_ptr<detail::SystemLifetime>& weak_lifetime,
                     Callback&& callback) {
  using Result = std::invoke_result_t<Callback, System&>;
  const auto lifetime = weak_lifetime.lock();
  if (!lifetime || lifetime->system == nullptr) return Result{};
  return std::invoke(std::forward<Callback>(callback), *lifetime->system);
}

template <typename Adopt>
OpenControllerResult adoptController(OpenDocumentResult opened, Adopt&& adopt) {
  OpenControllerResult result;
  result.diagnostics = std::move(opened.diagnostics);
  if (opened.document) {
    result.controller =
        std::invoke(std::forward<Adopt>(adopt), opened.document);
  }
  return result;
}

}  // namespace

DocumentController::DocumentController(
    std::weak_ptr<detail::SystemLifetime> lifetime,
    DocumentHandle document)
    : lifetime_(std::move(lifetime)), document_(document) {}

DocumentController::~DocumentController() {
  (void)close();
}

DocumentController::DocumentController(DocumentController&& other) noexcept
    : lifetime_(std::move(other.lifetime_)),
      document_(std::exchange(other.document_, {})),
      listeners_(std::move(other.listeners_)) {}

DocumentController& DocumentController::operator=(DocumentController&& other) noexcept {
  if (this != &other) {
    (void)close();
    lifetime_ = std::move(other.lifetime_);
    document_ = std::exchange(other.document_, {});
    listeners_ = std::move(other.listeners_);
  }
  return *this;
}

System* DocumentController::system() const {
  const auto lifetime = lifetime_.lock();
  return lifetime ? lifetime->system : nullptr;
}

bool DocumentController::valid() const {
  System* current = system();
  return current != nullptr && current->isOpen(document_);
}

bool DocumentController::close() {
  System* current = system();
  if (!document_.valid()) return false;
  const DocumentHandle closing = std::exchange(document_, {});
  listeners_.clear();
  return current != nullptr && current->close(closing);
}

DocumentHandle DocumentController::release() {
  listeners_.clear();
  lifetime_.reset();
  return std::exchange(document_, {});
}

bool DocumentController::show() {
  return forwardToSystem(lifetime_,
                         [&](System& current) { return current.show(document_); });
}

bool DocumentController::hide() {
  return forwardToSystem(lifetime_,
                         [&](System& current) { return current.hide(document_); });
}

bool DocumentController::setModal(bool modal) {
  return forwardToSystem(lifetime_, [&](System& current) {
    return current.setModal(document_, modal);
  });
}

bool DocumentController::set(std::string_view property_path, Value value) {
  return forwardToSystem(lifetime_, [&](System& current) {
    return current.set(document_, property_path, std::move(value));
  });
}

bool DocumentController::setMany(std::vector<ModelUpdate> updates) {
  return forwardToSystem(lifetime_, [&](System& current) {
    return current.setMany(document_, std::move(updates));
  });
}

std::optional<Value> DocumentController::get(std::string_view property_path) const {
  return forwardToSystem(lifetime_, [&](System& current) {
    return current.get(document_, property_path);
  });
}

ElementHandle DocumentController::findById(std::string_view id) const {
  return forwardToSystem(lifetime_,
                         [&](System& current) { return current.findById(document_, id); });
}

bool DocumentController::focus(ElementHandle element) {
  return forwardToSystem(lifetime_,
                         [&](System& current) { return current.focus(element); });
}

bool DocumentController::scrollTo(ElementHandle element, float x, float y) {
  return forwardToSystem(lifetime_, [&](System& current) {
    return current.scrollTo(element, x, y);
  });
}

bool DocumentController::scrollBy(ElementHandle element, float x, float y) {
  return forwardToSystem(lifetime_, [&](System& current) {
    return current.scrollBy(element, x, y);
  });
}

bool DocumentController::scrollIntoView(ElementHandle element,
                                        ScrollAlignment horizontal,
                                        ScrollAlignment vertical) {
  return forwardToSystem(lifetime_, [&](System& current) {
    return current.scrollIntoView(element, horizontal, vertical);
  });
}

ListenerHandle DocumentController::onAction(std::string_view action,
                                            ActionCallback callback) {
  System* current = system();
  if (current == nullptr) return {};
  ListenerHandle listener =
      current->onAction(document_, action, std::move(callback));
  if (listener) listeners_.push_back(listener);
  return listener;
}

ListenerHandle DocumentController::on(ElementHandle element,
                                      EventType type,
                                      EventCallback callback,
                                      const EventListenerOptions& options) {
  System* current = system();
  if (current == nullptr) return {};
  ListenerHandle listener =
      current->on(element, type, std::move(callback), options);
  if (listener) listeners_.push_back(listener);
  return listener;
}

bool DocumentController::bindActions(ActionMap actions) {
  for (auto& [action, callback] : actions) {
    if (!onAction(action, std::move(callback))) return false;
  }
  return true;
}

OpenControllerResult System::openController(
    std::string_view asset_key,
    const OpenDocumentOptions& options) {
  return adoptController(open(asset_key, options), [&](DocumentHandle document) {
    return DocumentController{lifetime_, document};
  });
}

OpenControllerResult System::openFileController(
    const std::filesystem::path& relative_path,
    const OpenDocumentOptions& options) {
  return adoptController(openFile(relative_path, options),
                         [&](DocumentHandle document) {
                           return DocumentController{lifetime_, document};
                         });
}

}  // namespace karma::ui
