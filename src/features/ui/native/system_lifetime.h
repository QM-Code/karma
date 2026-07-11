#pragma once

namespace karma::ui {

class System;

namespace detail {

struct SystemLifetime {
  System* system = nullptr;
};

}  // namespace detail
}  // namespace karma::ui
