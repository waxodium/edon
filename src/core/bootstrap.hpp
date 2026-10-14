#pragma once

#include "embedjs.hpp"
#include "runtime.hpp"

#include <string>

inline void EdonRuntime::bootstrapCoreLibraries() {
  if (!executeSource(std::string(edon::core::util), "util.js")) return;

  executeSource(std::string(edon::core::console), "console.js");
}
