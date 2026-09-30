#pragma once

#include "library.hpp"

#include <memory>
#include <string>

namespace edon {
namespace ffi {

struct NativeFunctionState {
  NativeFunctionState(
      std::shared_ptr<NativeModuleState> module,
      void *address,
      std::string name);

  std::shared_ptr<NativeModuleState> module;
  void *address = nullptr;
  std::string name;
};

} // namespace ffi
} // namespace edon
