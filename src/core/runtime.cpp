#include "runtime.hpp"
#include "bootstrap.hpp"
#include "require.hpp"

#include "ffi/api.hpp"
#include "module_loader.hpp"
#include "native_io.hpp"
#include "runtime_output.hpp"

#include <iostream>
#include <memory>
#include <string>

EdonRuntime::EdonRuntime() {
  edon::initSystemIO();

  context = JSGlobalContextCreate(nullptr);

  moduleLoader = std::make_unique<ModuleLoader>(
      context, [this](const std::string &parentFilename) { return makeRequire(parentFilename); });

  edon::registerNativeIO(context);
  edon::ffi::registerNamespace(context);

  JSObjectRef global = JSContextGetGlobalObject(context);
  JSObjectRef function = makeRequire("");

  JSStringRef name = JSStringCreateWithUTF8CString("require");

  if (function) {
    JSObjectSetProperty(context, global, name, function, kJSPropertyAttributeNone, nullptr);
  }

  JSStringRelease(name);

  bootstrapCoreLibraries();
}

EdonRuntime::~EdonRuntime() {
  moduleLoader.reset();

  if (!context) return;

  JSGlobalContextRelease(context);
  context = nullptr;
}

bool EdonRuntime::executeSource(const std::string &source, const std::string &filename) {
  JSStringRef jsSource = JSStringCreateWithUTF8CString(source.c_str());

  JSStringRef jsFilename = JSStringCreateWithUTF8CString(filename.c_str());

  if (!jsSource || !jsFilename) {
    if (jsSource) JSStringRelease(jsSource);

    if (jsFilename) JSStringRelease(jsFilename);

    std::cerr << "Uncaught Error: Failed to create JavaScript source strings\n";

    return false;
  }

  JSValueRef error = nullptr;

  (void)JSEvaluateScript(context, jsSource, nullptr, jsFilename, 1, &error);

  JSStringRelease(jsSource);
  JSStringRelease(jsFilename);

  if (error) {
    edon::printDiagnostic(source, filename, context, error);
    return false;
  }

  return true;
}

bool EdonRuntime::executeFile(const std::string &filepath) {
  JSValueRef error = nullptr;

  moduleLoader->loadFile(filepath, &error);

  if (!error) return true;

  edon::printDiagnostic("", filepath, context, error);
  return false;
}
