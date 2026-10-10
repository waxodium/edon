# FFI

`edon:ffi` provides a foreign function interface for compiling C code at runtime, loading native libraries, calling C functions, creating JavaScript callbacks, and managing native memory.

## Table of Contents
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
- [Example](#example)

## Import

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

## Runtime C Compilation

`c()` compiles C source code at runtime and returns a native module.

```js
const native = c(`
  int add(int a, int b) {
    return a + b;
  }
`);
```

Functions exported by the compiled module can be accessed through `cfunction()`.

## Native Libraries

`loadLibrary()` loads a native shared library and returns a native module.

```js
const libc = loadLibrary("libc.so.6");
```

Library paths and formats depend on the host platform.

## Native Functions

`cfunction()` creates a JavaScript callable for a native function exported by a compiled module or loaded library.

### Syntax

```js
module.cfunction(name, signature)
```

- `name`: function name.
- `signature`: object describing the return type and argument types.

```js
const add = native.cfunction("add", {
  returns: "int32",
  args: ["int32", "int32"]
});

console.log(add(2, 3)); // 5
```

`returns` specifies the native return type. `args` lists argument types in declaration order. For `void` functions, the JavaScript call returns `undefined`.

### Variadic Functions

Variadic signatures use `"..."` as the final entry in `args`.

```js
const printf = libc.cfunction("printf", {
  returns: "int32",
  args: ["const char*", "..."]
});

printf("value = %d\n", 42);
```

Variadic arguments follow C's default argument-promotion rules.

## Callbacks

`createCallback()` creates a native function pointer backed by a JavaScript function.

```js
const callback = createCallback(
  value => value * 2,
  {
    returns: "int32",
    args: ["int32"]
  }
);
```

The signature determines how arguments and return values are converted between JavaScript and native code. A callback can be passed to a native function that expects a compatible function pointer.

Use `destroyCallback()` when the callback is no longer needed.

```js
destroyCallback(callback);
```

A destroyed callback must not be passed to native code.

## Native Memory

`allocateSharedBuffer()` allocates native memory and returns an `ArrayBuffer`.

```js
const buffer = allocateSharedBuffer(64);
const view = new DataView(buffer);

view.setInt32(0, 123, true);

const address = addressOf(buffer);

free(buffer);
```

- `allocateSharedBuffer(size)`: allocates native memory.
- `addressOf(buffer)`: obtains the allocation's native address.
- `free(buffer)`: releases the allocation.

Native memory must not be accessed after it has been freed. An allocation must only be freed once.

## C Type Declarations

`edon:ffi` uses C-style type declarations for native function signatures and callbacks.

### Primitive Types

| Type | Description |
|---|---|
| `void` | No return value |
| `bool` | Boolean |
| `char` | Character |
| `int8`, `uint8` | 8-bit integers |
| `int16`, `uint16` | 16-bit integers |
| `int32`, `uint32` | 32-bit integers |
| `int64`, `uint64` | 64-bit integers |
| `size`, `ssize` | Size types |
| `float` | Single-precision floating point |
| `double` | Double-precision floating point |

`int64` and `uint64` support JavaScript `BigInt` values. Safe integer `Number` values are also supported.

### Pointers

Pointers use standard C declarator syntax.

```text
void*
int32*
int32**
const char*
volatile int32*
int32 *const
```

Supported qualifiers include `const`, `volatile`, and `restrict`.

### Arrays

Fixed-size arrays use a type followed by an element count.

```text
int32[4]
double[8]
uint8[256]
```

Arrays support combinations with pointers and parenthesized declarators.

### Function Pointers

Function pointers use standard C syntax, including parenthesized and nested declarators.

```text
int (*)(int)
int (*)(int, double)
void (*)(const char*)
```

Function pointers can be arguments or return types and can be combined with pointers, arrays, and variadic signatures.

### Structs, Unions, and Enums

The type system supports C struct, union, and enum declarations, including named types and typedef aliases.

### Typedefs

Typedef declarations and chained aliases are supported within the type context. Aliases can refer to supported primitive, pointer, array, aggregate, and function-pointer types.

## Example

```js
const { c } = require("edon:ffi");

const native = c(`
  int multiply(int a, int b) {
    return a * b;
  }
`);

const multiply = native.cfunction("multiply", {
  returns: "int32",
  args: ["int32", "int32"]
});

console.log(multiply(6, 7)); // 42
```
