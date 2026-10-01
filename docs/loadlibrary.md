## `loadLibrary()`

### Table of Contents

- [Overview](#overview)
- [Syntax](#syntax)
- [Loading a Library](#loading-a-library)
- [Resolving Functions](#resolving-functions)
- [Supported Libraries](#supported-libraries)
- [Errors](#errors)
- [Example](#example-usage)

## Overview

`loadLibrary()` loads a native shared library and returns a native module.

```js
const { loadLibrary } = require("edon:ffi");

const libc = loadLibrary("libc.so.6");
```

The returned module can be used to resolve native functions through [`cfunction()`](./cfunction.md).

```js
const puts = libc.cfunction("puts", {
  returns: "int32",
  args: ["const char*"]
});

puts("hello from edon");
```

---

## Syntax

```js
loadLibrary(path)
```

### Parameters

`path` is the path to the native shared library.

```js
const native = loadLibrary("/path/to/library.so");
```

The exact library format and filename conventions depend on the host platform.

---

## Loading a Library

A library is loaded once `loadLibrary()` is called successfully.

```js
const native = loadLibrary("libexample.so");
```

The returned native module represents the loaded library and provides [`cfunction()`](./cfunction.md).

```js
const add = native.cfunction("add", {
  returns: "int32",
  args: ["int32", "int32"]
});

console.log(add(10, 20));
```

The function signature supplied to `cfunction()` determines how arguments and return values are converted between JavaScript and native code.

---

## Resolving Functions

Native symbols are resolved by name through `cfunction()`.

```js
const libc = loadLibrary("libc.so.6");

const puts = libc.cfunction("puts", {
  returns: "int32",
  args: ["const char*"]
});
```

A symbol must exist in the loaded library and its signature must be valid.

For complete information about native function signatures, see [`cfunction()`](./cfunction.md).

---

## Supported Libraries

Supported native library formats depend on the host platform.

Common formats include:

```text
.so
.dylib
.dll
```

Examples:

```js
const linux = loadLibrary("libc.so.6");
const macos = loadLibrary("/usr/lib/libSystem.B.dylib");
const windows = loadLibrary("example.dll");
```

Platform-specific library names and paths are determined by the operating system.

---

## Errors

`loadLibrary()` produces a JavaScript exception when the library cannot be loaded.

Typical failures include:

- Invalid library path
- Missing library
- Unsupported library
- Invalid native module
- Native loader failure

Symbol and signature errors are reported when `cfunction()` is used on the returned module.

---

## Example Usage

```js
const {
  loadLibrary
} = require("edon:ffi");

const libc = loadLibrary("libc.so.6");

const puts = libc.cfunction("puts", {
  returns: "int32",
  args: ["const char*"]
});

puts("hello from edon");
```

A loaded library can expose multiple native functions:

```js
const libc = loadLibrary("libc.so.6");

const puts = libc.cfunction("puts", {
  returns: "int32",
  args: ["const char*"]
});

const abs = libc.cfunction("abs", {
  returns: "int32",
  args: ["int32"]
});

puts("hello from edon");
console.log(abs(-42));
```
