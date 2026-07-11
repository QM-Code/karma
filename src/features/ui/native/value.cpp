#include "karma/ui.h"

#include <cmath>
#include <iomanip>
#include <sstream>
#include <type_traits>
#include <utility>
#include <variant>

namespace karma::ui {

struct Value::Storage {
  using Variant = std::variant<std::monostate,
                               bool,
                               Integer,
                               double,
                               std::string,
                               math::Color,
                               ImageSource,
                               Array,
                               Object>;
  Variant value;

  Storage() = default;
  Storage(const Storage&) = default;
  Storage(Storage&&) noexcept = default;
  Storage& operator=(const Storage&) = default;
  Storage& operator=(Storage&&) noexcept = default;

  template <typename T>
    requires(!std::is_same_v<std::remove_cvref_t<T>, Storage>)
  explicit Storage(T&& input) : value(std::forward<T>(input)) {}
};

ImageSource ImageSource::asset(std::string key) {
  ImageSource result;
  result.kind = Kind::Asset;
  result.asset_key = std::move(key);
  return result;
}

ImageSource ImageSource::dynamic(DynamicImageHandle image) {
  ImageSource result;
  result.kind = Kind::Dynamic;
  result.dynamic_image = image;
  return result;
}

ImageSource ImageSource::renderTarget(rendering::RenderTargetId target) {
  ImageSource result;
  result.kind = Kind::RenderTarget;
  result.render_target = target;
  return result;
}

Value::Value(bool value) : storage_(std::make_shared<Storage>(value)) {}
Value::Value(Integer value) : storage_(std::make_shared<Storage>(value)) {}
Value::Value(double value) : storage_(std::make_shared<Storage>(value)) {}
Value::Value(const char* value)
    : Value(value == nullptr ? std::string{} : std::string(value)) {}
Value::Value(std::string value)
    : storage_(std::make_shared<Storage>(std::move(value))) {}
Value::Value(std::string_view value) : Value(std::string(value)) {}
Value::Value(math::Color value) : storage_(std::make_shared<Storage>(value)) {}
Value::Value(ImageSource value)
    : storage_(std::make_shared<Storage>(std::move(value))) {}
Value::Value(Array value) : storage_(std::make_shared<Storage>(std::move(value))) {}
Value::Value(Object value) : storage_(std::make_shared<Storage>(std::move(value))) {}

Value::Type Value::type() const {
  if (!storage_) {
    return Type::Null;
  }
  switch (storage_->value.index()) {
    case 0: return Type::Null;
    case 1: return Type::Boolean;
    case 2: return Type::Integer;
    case 3: return Type::Number;
    case 4: return Type::String;
    case 5: return Type::Color;
    case 6: return Type::Image;
    case 7: return Type::Array;
    case 8: return Type::Object;
    default: return Type::Null;
  }
}

bool Value::truthy() const {
  switch (type()) {
    case Type::Null:
      return false;
    case Type::Boolean:
      return std::get<bool>(storage_->value);
    case Type::Integer:
      return std::get<Integer>(storage_->value) != 0;
    case Type::Number: {
      const double value = std::get<double>(storage_->value);
      return value != 0.0 && !std::isnan(value);
    }
    case Type::String:
      return !std::get<std::string>(storage_->value).empty();
    case Type::Array:
      return !std::get<Array>(storage_->value).empty();
    case Type::Object:
      return !std::get<Object>(storage_->value).empty();
    case Type::Color:
    case Type::Image:
      return true;
  }
  return false;
}

std::optional<bool> Value::asBoolean() const {
  if (type() != Type::Boolean) {
    return std::nullopt;
  }
  return std::get<bool>(storage_->value);
}

std::optional<Value::Integer> Value::asInteger() const {
  if (type() == Type::Integer) {
    return std::get<Integer>(storage_->value);
  }
  if (type() == Type::Number) {
    const double value = std::get<double>(storage_->value);
    constexpr double exclusive_upper = 9223372036854775808.0;
    if (std::isfinite(value) &&
        value >= static_cast<double>(std::numeric_limits<Integer>::min()) &&
        value < exclusive_upper &&
        std::floor(value) == value) {
      return static_cast<Integer>(value);
    }
  }
  return std::nullopt;
}

std::optional<double> Value::asNumber() const {
  if (type() == Type::Number) {
    return std::get<double>(storage_->value);
  }
  if (type() == Type::Integer) {
    return static_cast<double>(std::get<Integer>(storage_->value));
  }
  return std::nullopt;
}

const std::string* Value::asString() const {
  return type() == Type::String ? &std::get<std::string>(storage_->value) : nullptr;
}

const math::Color* Value::asColor() const {
  return type() == Type::Color ? &std::get<math::Color>(storage_->value) : nullptr;
}

const ImageSource* Value::asImage() const {
  return type() == Type::Image ? &std::get<ImageSource>(storage_->value) : nullptr;
}

const Value::Array* Value::asArray() const {
  return type() == Type::Array ? &std::get<Array>(storage_->value) : nullptr;
}

Value::Array* Value::asArray() {
  if (type() != Type::Array) {
    return nullptr;
  }
  if (!storage_.unique()) {
    storage_ = std::make_shared<Storage>(*storage_);
  }
  return &std::get<Array>(storage_->value);
}

const Value::Object* Value::asObject() const {
  return type() == Type::Object ? &std::get<Object>(storage_->value) : nullptr;
}

Value::Object* Value::asObject() {
  if (type() != Type::Object) {
    return nullptr;
  }
  if (!storage_.unique()) {
    storage_ = std::make_shared<Storage>(*storage_);
  }
  return &std::get<Object>(storage_->value);
}

std::string Value::toString() const {
  switch (type()) {
    case Type::Null:
      return {};
    case Type::Boolean:
      return std::get<bool>(storage_->value) ? "true" : "false";
    case Type::Integer:
      return std::to_string(std::get<Integer>(storage_->value));
    case Type::Number: {
      std::ostringstream stream;
      stream << std::setprecision(15) << std::get<double>(storage_->value);
      return stream.str();
    }
    case Type::String:
      return std::get<std::string>(storage_->value);
    case Type::Color: {
      const math::Color& color = std::get<math::Color>(storage_->value);
      std::ostringstream stream;
      stream << "rgba(" << color.r << ',' << color.g << ',' << color.b << ',' << color.a
             << ')';
      return stream.str();
    }
    case Type::Image: {
      const ImageSource& image = std::get<ImageSource>(storage_->value);
      return image.kind == ImageSource::Kind::Asset ? image.asset_key : std::string{};
    }
    case Type::Array:
      return "[array]";
    case Type::Object:
      return "[object]";
  }
  return {};
}

namespace {
bool equalColors(const math::Color& left, const math::Color& right) {
  return left.r == right.r && left.g == right.g && left.b == right.b && left.a == right.a;
}
}

bool operator==(const Value& left, const Value& right) {
  if (left.type() != right.type()) {
    const Value* integer_value = nullptr;
    const Value* number_value = nullptr;
    if (left.type() == Value::Type::Integer && right.type() == Value::Type::Number) {
      integer_value = &left;
      number_value = &right;
    } else if (right.type() == Value::Type::Integer && left.type() == Value::Type::Number) {
      integer_value = &right;
      number_value = &left;
    }
    if (integer_value == nullptr) return false;
    const auto exact_integer = number_value->asInteger();
    return exact_integer.has_value() &&
           *integer_value->asInteger() == *exact_integer;
  }
  switch (left.type()) {
    case Value::Type::Null:
      return true;
    case Value::Type::Boolean:
      return std::get<bool>(left.storage_->value) ==
             std::get<bool>(right.storage_->value);
    case Value::Type::Integer:
      return std::get<Value::Integer>(left.storage_->value) ==
             std::get<Value::Integer>(right.storage_->value);
    case Value::Type::Number:
      return std::get<double>(left.storage_->value) ==
             std::get<double>(right.storage_->value);
    case Value::Type::String:
      return std::get<std::string>(left.storage_->value) ==
             std::get<std::string>(right.storage_->value);
    case Value::Type::Color:
      return equalColors(std::get<math::Color>(left.storage_->value),
                         std::get<math::Color>(right.storage_->value));
    case Value::Type::Image:
      return std::get<ImageSource>(left.storage_->value) ==
             std::get<ImageSource>(right.storage_->value);
    case Value::Type::Array:
      return std::get<Value::Array>(left.storage_->value) ==
             std::get<Value::Array>(right.storage_->value);
    case Value::Type::Object:
      return std::get<Value::Object>(left.storage_->value) ==
             std::get<Value::Object>(right.storage_->value);
  }
  return false;
}

}  // namespace karma::ui
