#include "function.hpp"

#include <utility>

namespace edon { namespace ffi {

NativeFunctionState::NativeFunctionState(std::shared_ptr<NativeModuleState> nativeModule,
                                         void *functionAddress, std::string functionName)
    : module(std::move(nativeModule)),
      address(functionAddress),
      name(std::move(functionName)) {}

}} // namespace edon::ffi
