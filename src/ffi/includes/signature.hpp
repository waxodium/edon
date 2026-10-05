#pragma once

#include <ffi.h>

#include <memory>
#include <vector>

namespace edon { namespace ffi {

struct Type;

struct Signature {
  std::shared_ptr<Type> returns;
  std::vector<std::shared_ptr<Type>> args;

  ffi_cif cif{};
  std::vector<ffi_type *> ffiArgs;

  bool prepared = false;
};

bool prepareSignature(Signature &signature);

}} // namespace edon::ffi
