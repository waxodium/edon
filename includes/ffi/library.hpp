#pragma once

#include <memory>
#include <string>

namespace edon { namespace ffi {

class TypeContext;

enum class NativeModuleKind { TCC, DynamicLibrary };

struct NativeCompileDiagnostic {
  std::string source;
  std::string message;
};

class NativeModuleState {
public:
  explicit NativeModuleState(NativeModuleKind kind);
  ~NativeModuleState();

  NativeModuleState(const NativeModuleState &) = delete;
  NativeModuleState &operator=(const NativeModuleState &) = delete;

  void *resolve(const std::string &name) const;

  static std::shared_ptr<NativeModuleState> compile(const std::string &source, std::string &error,
                                                    NativeCompileDiagnostic &diagnostic);

  static std::shared_ptr<NativeModuleState> load(const std::string &path, std::string &error);

  NativeModuleKind kind;
  void *handle = nullptr;
  std::shared_ptr<TypeContext> typeContext;
};

}} // namespace edon::ffi
