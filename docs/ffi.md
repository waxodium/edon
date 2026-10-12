# **edon:ffi**

`edon:ffi` provides a foreign function interface for compiling C code at runtime, loading native libraries, calling C functions, creating JavaScript callbacks, and managing native memory. This API is intentionally unsafe. Misuse can cause crashes, memory corruption, or undefined behavior.

## **Table of Contents**

- [Import](#import)
- [Runtime C Compilation](#runtime-c-compilation)
  - [`c(source)`](#csource)
- [Native Libraries](#native-libraries)
  - [`loadLibrary(path)`](#loadlibrarypath)
- [Calling Native Code](#calling-native-code)
  - [Native Functions](#native-functions)
    - [`module.cfunction(name, signature)`](#modulecfunctionnamesignature)
  - [Callbacks](#callbacks)
    - [`createCallback(fn, signature)`](#createcallbackfnsignature)
    - [`destroyCallback(callback)`](#destroycallbackcallback)
- [Native Memory](#native-memory)
  - [`allocateSharedBuffer(size)`](#allocatesharedbuffersize)
  - [`addressOf(buffer)`](#addressofbuffer)
  - [`free(buffer)`](#freebuffer)
- [C Type Declarations](#c-type-declarations)
  - [Primitive Types](#primitive-types)
  - [Pointers, Arrays, and Aggregates](#pointers-arrays-and-aggregates)
  - [Function Pointers](#function-pointers)
  - [Qualifiers and Typedefs](#qualifiers-and-typedefs)
- [Examples](#examples)
- [Troubleshooting](#troubleshooting)

---

## **Import**
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

## **Runtime C Compilation**

### `c(source)`

**Type:** Function

Compiles C source code at runtime and returns a native module. TCC is the default compiler; alternative compilers may be selected through runtime configuration when supported.

**Parameters**

- `source` `<string>`: C source code to compile.

**Returns:** A native module exposing compiled functions.

```js
const native = c(`
  int add(int a, int b) {
    return a + b;
  }
`);
```

## **Native Libraries**

### `loadLibrary(path)`

**Type:** Function

Loads a precompiled native shared library and returns a native module.

**Parameters**

- `path` `<string>`: Library path or name.

**Returns:** A native module exposing the library's symbols.

```js
const libc = loadLibrary("libc.so.6"); // Linux
```

| Supported Platform | Library Format | 
|---|---|
| Linux    | `"libc.so.6"` |
| macOS    | `"libc.dylib"` |
| Windows  | `"msvcrt.dll"` |

## **Calling Native Code**

### **Native Functions**

#### `module.cfunction(name, signature)`

**Type:** Method

Creates a JavaScript callable for a native function exported by a compiled module or loaded library.

**Parameters**

- `name` `<string>`: Native function name.
- `signature` `<Object>`: Function signature describing the return and argument types.

**Signature properties**

- `returns` `<string>`: Native return type.
- `args` `<Array<string>>`: Argument types in declaration order.

**Returns:** A JavaScript function that invokes the native function.

```js
const add = native.cfunction("add", {
  returns: "int32",
  args: ["int32", "int32"]
});

console.log(add(2, 3)); // 5
```

For `void` return types, the JavaScript call returns `undefined`.

**Variadic functions:** Use `"..."` as the final entry in `args`.

```js
const printf = libc.cfunction("printf", {
  returns: "int",
  args: ["const char*", "..."]
});

printf("Value: %d\n", 42);
```

Variadic arguments follow C's default argument-promotion rules.

### **Callbacks**

#### `createCallback(fn, signature)`

**Type:** Function

Creates a native function pointer backed by a JavaScript function. The pointer can be passed to native functions expecting a compatible callback signature.

**Parameters**

- `fn` `<Function>`: JavaScript function invoked when native code calls the callback. Arguments are converted to JavaScript values according to the signature.
- `signature` `<Object>`: Configuration object describing the native callback signature.
  - `returns` `<string>`: Native return type.
  - `args` `<Array<string>>`: Native argument types in declaration order.

**Returns:** A native callback handle that can be passed to compatible native functions.

```js
const callback = createCallback(
  value => value * 2,
  {
    returns: "int32",
    args: ["int32"]
  }
);
```

The signature must match the native function pointer's expected types and calling convention.

#### `destroyCallback(callback)`

**Type:** Function

Destroys a callback and releases its associated resources.

**Parameters**

- `callback` (callback handle): Callback previously created with `createCallback()`.

**Returns:** No meaningful return value.

```js
destroyCallback(callback);
```

The callback must no longer be used by native code before it is destroyed. Using a destroyed callback can cause a crash or undefined behavior.

## **Native Memory**

### `allocateSharedBuffer(size)`

**Type:** Function

Allocates native memory and exposes it through a JavaScript `ArrayBuffer`.

**Parameters**

- `size` `<number>`: Number of bytes to allocate.

**Returns:** `ArrayBuffer` backed by the native allocation.

```js
const buffer = allocateSharedBuffer(64);
const view = new DataView(buffer);

view.setInt32(0, 123, true);
```

### `addressOf(buffer)`

**Type:** Function

Obtains the native memory address associated with an allocated buffer.

**Parameters**

- `buffer` `<ArrayBuffer>`: Buffer backed by an FFI-managed native allocation.

**Returns:** The native address representation supported by the implementation.

```js
const address = addressOf(buffer);
```

### `free(buffer)`

**Type:** Function

Releases an FFI-managed native memory allocation.

**Parameters**

- `buffer` `<ArrayBuffer>`: Buffer whose native allocation should be released.

**Returns:** No meaningful return value.

```js
free(buffer);
```

An allocation must only be freed once. Do not access its native memory after it has been freed.

## **C Type Declarations**

Type strings describe native values used by function signatures and callbacks. They must match the actual native ABI.

### **Primitive Types**

| Type | Description | JavaScript representation |
|---|---|---|
| `void` | No return value | `undefined` |
| `bool` | Boolean | `boolean` |
| `char` | C character type | Integer or character representation supported by the implementation |
| `int8`, `uint8` | 8-bit integers | `number` |
| `int16`, `uint16` | 16-bit integers | `number` |
| `int32`, `uint32` | 32-bit integers | `number` |
| `int64`, `uint64` | 64-bit integers | `bigint` or supported safe integer `number` |
| `size`, `ssize` | Platform-sized integers | Numeric representation supported by the implementation |
| `float` | Single-precision floating point | `number` |
| `double` | Double-precision floating point | `number` |

C-style type spellings and fixed-width integer names may be accepted by the parser where supported.

### **Pointers, Arrays, and Aggregates**

#### **Pointers**

Pointers represent native memory addresses.

```c
void*
int32*
int32**
const char*
```

Pointer values must be valid for the expected native operation and remain valid for the duration of use.

#### **Arrays**

Fixed-size arrays specify an element type and count.

```c
int32[4]
double[8]
uint8[256]
```

Arrays can be combined with pointers and parenthesized declarators where supported.

#### **Structs, Unions, and Enums**

The type system supports named `struct`, `union`, and `enum` declarations, along with typedef aliases, subject to the parser and ABI implementation.

```c
typedef struct {
  int x;
  int y;
} Point;
```

Aggregate layouts must match the native compiler's layout, including field alignment and padding.

### **Function Pointers**

Function pointer declarations use C-style syntax.

```c
int (*)(int)
int (*)(int, double)
void (*)(void*)
```

They can be used as native arguments or return types when the signature is supported.

### **Qualifiers and Typedefs**

Supported qualifiers include `const`, `volatile`, and `restrict`.

Typedef declarations and chained aliases can refer to supported primitive, pointer, array, aggregate, and function-pointer types.

## **Examples**

### **1. Calling a Compiled Function**

```js
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

### **2. Passing a Callback to Native Code**

```js
const native = c(`
  int apply(int (*fn)(int), int value) {
    return fn(value);
  }
`);

const apply = native.cfunction("apply", {
  returns: "int32",
  args: ["int (*)(int)", "int32"]
});

const callback = createCallback(value => value * 2, {
  returns: "int32",
  args: ["int32"]
});

console.log(apply(callback, 21)); // 42

destroyCallback(callback);
```

### **3. Native Memory**

```js
const buffer = allocateSharedBuffer(64);
const view = new DataView(buffer);

view.setInt32(0, 123, true);

console.log(view.getInt32(0, true)); // 123

const address = addressOf(buffer);

free(buffer);
```

## **Troubleshooting**
Lists of common usage issues of `edon:ffi`: 

| Issue | Recommendation |
|---|---|
| Segmentation fault | Check pointer validity, callback extent, and function signatures. |
| Memory corruption | Verify native types, alignment, and buffer bounds. |
| Memory leaks | Release allocations and destroy callbacks when no longer needed. |
| Type mismatch | Verify that the declared signature matches the native function. |
| Unsupported type | Use a supported declaration. Do not substitute an incompatible type. |

