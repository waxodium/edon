#include "ffi/api.hpp"

#include "ffi/errors.hpp"
#include "ffi/module.hpp"

namespace edon { namespace ffi {

void registerNamespace(JSGlobalContextRef context) {
  (void)context;
  initializeModuleClasses();
}

JSValueRef requireModule(JSContextRef context, const std::string &name, JSValueRef *error) {
  if (name != "edon:ffi") {
    throwError(context, error, ErrorCode::ModuleNotFound, name);

    return JSValueMakeUndefined(context);
  }

  return requireFFIModule(context, error);
}

}} // namespace edon::ffi
