## `edon:ffi` JavaScript Runtime API

### Table of Contents
- [Runtime C Compilation](#runtime-c-compilation)
- [Native Libraries](#native-libraries)
- [Native Functions](#native-functions)
- [Callbacks](#callbacks)
- [Native Memory](#native-memory)
- [C Type Declarations](#c-type-declarations)
  - [Pointers](#pointers)
  - [Arrays](#arrays)
  - [Function Pointers](#function-pointers)
  - [Qualifiers](#qualifiers)
- [Errors](#errors)
- [Example](#example)

### Table of Related Contents
- [`cfunction()`](./cfunction.md)
- [`loadLibrary()`](./loadlibrary.md)
- [callbacks](./callbacks.md)
- [memory](./memory.md)
- [types](./types.md)
- [ffi errors](./ffierrors.md)

`edon:ffi` provides a JavaScript foreign function interface for `edon`.

It allows JavaScript code to:

- Compile C code at runtime
- Load native libraries
- Call native C functions
- Define native function signatures
- Create callbacks from JavaScript functions
- Allocate and manage native memory
- Access native memory addresses

```js
const {
  c,
  loadLibrary,
  createCallback,
  destroyCallback,
  allocateSharedBuffer,
  addressOf,
  free
} = require("edon:ffi");
```

> [`cfunction()`](./cfunction.md) is a method provided by native modules returned by `c()` or `loadLibrary()`.]

---

## Runtime C Compilation

`c()` compiles C source code at runtime and returns a native module.

```js
const native = c(`
  int add(int a, int b) {
    return a + b;
  }
`);
```

The resulting module can expose compiled C functions through `cfunction()`.

```js
const add = native.cfunction("add", {
  returns: "int32",
  args: ["int32", "int32"]
});

console.log(add(2, 3));
```

---

## Native Libraries

[`loadLibrary()`](./loadlibrary.md) loads a native shared library and returns a module.

Supported formats depend on the host platform and include:

- `.so`
- `.dylib`
- `.dll`

```js
const libc = loadLibrary("libc.so.6");
```

Native functions can then be accessed through `cfunction()`.

```js
const puts = libc.cfunction("puts", {
  returns: "int32",
  args: ["const char*"]
});

puts("hello from edon");
```

---

## Native Functions

[`cfunction()`](./cfunction.md) creates a JavaScript callable for a native function.

```js
const add = native.cfunction("add", {
  returns: "int32",
  args: ["int32", "int32"]
});

const result = add(10, 20);
```

The signature describes the native function:

```js
{
  returns: "int32",
  args: ["int32", "int32"]
}
```

The type system supports primitive C types, pointers, arrays, qualifiers, parenthesized declarators, and function-pointer declarations.

Common types include:

```text
void
bool
char

int8
uint8
int16
uint16
int32
uint32
int64
uint64

size
ssize

float
double

T*
const T*
```

The declared signature determines how JavaScript arguments and native return values are converted.

---

## Callbacks

`createCallback()` creates a native function pointer backed by a JavaScript function.

```js
const callback = createCallback(
  value => value + 10,
  {
    returns: "int32",
    args: ["int32"]
  }
);
```

The callback can be passed to native functions that accept compatible function pointers.

```js
const native = c(`
  int call_callback(int (*callback)(int), int value) {
    return callback(value);
  }
`);

const callCallback = native.cfunction("call_callback", {
  returns: "int32",
  args: ["int (*)(int)", "int32"]
});

console.log(callCallback(callback, 32));
```

Callbacks can be released with:

```js
destroyCallback(callback);
```

---

## Native Memory

`allocateSharedBuffer()` allocates native memory that can be accessed from JavaScript.

```js
const buffer = allocateSharedBuffer(64);

const view = new DataView(buffer);

view.setInt32(0, 123, true);
```

`addressOf()` obtains the native address associated with an allocation.

```js
const address = addressOf(buffer);
```

`free()` releases an allocation.

```js
free(buffer);
```

Native memory must not be accessed after it has been freed.

---

## C Type Declarations

`edon:ffi` uses C-style type declarations for native signatures.

### Pointers

```text
int32*
uint8*
void*
const char*
```

### Arrays

```text
int32[4]
double[8]
```

### Function pointers

```text
int (*)(int)
int (*)(int, double)
void (*)(const char*)
```

### Qualifiers

```text
const char*
const int32*
volatile int32*
```

These declarations are used consistently by `cfunction()` and `createCallback()`.

---

## Errors

Invalid source code, libraries, symbols, signatures, arguments, pointers, callbacks, and memory operations produce JavaScript exceptions.

Native faults occurring during protected native calls are also handled by the runtime where supported.

---

## Example

```js
const {
  c,
  loadLibrary,
  createCallback,
  destroyCallback,
  allocateSharedBuffer,
  addressOf,
  free
} = require("edon:ffi");

const native = c(`
  int add(int a, int b) {
    return a + b;
  }

  int call_callback(int (*callback)(int), int value) {
    return callback(value);
  }
`);

const add = native.cfunction("add", {
  returns: "int32",
  args: ["int32", "int32"]
});

console.log(add(2, 3));

const callback = createCallback(
  value => value * 2,
  {
    returns: "int32",
    args: ["int32"]
  }
);

const callCallback = native.cfunction("call_callback", {
  returns: "int32",
  args: ["int (*)(int)", "int32"]
});

console.log(callCallback(callback, 21));

destroyCallback(callback);

const buffer = allocateSharedBuffer(32);

console.log(addressOf(buffer));

free(buffer);

const libc = loadLibrary("libc.so.6");

const puts = libc.cfunction("puts", {
  returns: "int32",
  args: ["const char*"]
});

puts("hello from edon");
```
