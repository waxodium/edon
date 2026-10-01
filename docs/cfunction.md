## `cfunction()`

Creates a JavaScript callable for a native C function exported by an [`edon:ffi`](./edonffi.md) module.

`cfunction()` can be used with modules created by [`c()`](./c.md) and modules loaded with [`loadLibrary()`](./loadlibrary.md).

### Syntax

```js
module.cfunction(name, signature)
```

### Parameters

#### `name`

The name of the native function to resolve.

Type: `string`

#### `signature`

An object describing the native function signature.

```js
{
  returns: "int32",
  args: ["int32", "int32"]
}
```

| Property | Type | Description |
|---|---|---|
| `returns` | `string` | Native return type |
| `args` | `string[]` | Native argument types in declaration order |

### Example

```js
const native = c(`
  int add(int a, int b) {
    return a + b;
  }
`);

const add = native.cfunction("add", {
  returns: "int32",
  args: ["int32", "int32"]
});

console.log(add(2, 3));
```

Output:

```text
5
```

## Return Types

`returns` specifies the C type returned by the native function.

Supported primitive types include:

| Type | Description |
|---|---|
| `void` | No return value |
| `bool` | Boolean |
| `char` | Character |
| `int8` | Signed 8-bit integer |
| `uint8` | Unsigned 8-bit integer |
| `int16` | Signed 16-bit integer |
| `uint16` | Unsigned 16-bit integer |
| `int32` | Signed 32-bit integer |
| `uint32` | Unsigned 32-bit integer |
| `int64` | Signed 64-bit integer |
| `uint64` | Unsigned 64-bit integer |
| `size` | `size_t` |
| `ssize` | Signed size type |
| `float` | Single-precision floating point |
| `double` | Double-precision floating point |

Pointer types can be specified using standard C syntax:

```js
"void*"
"int32*"
"uint8*"
"const char*"
```

## Arguments

`args` contains the native argument types in the same order as the C function declaration.

For example:

```js
const add = native.cfunction("add", {
  returns: "int32",
  args: ["int32", "int32"]
});
```

The resulting function expects two JavaScript arguments:

```js
add(10, 20);
```

Each JavaScript argument is converted according to its declared native type.

## Strings

C string parameters can be declared with `char*` or `const char*`.

```js
const puts = libc.cfunction("puts", {
  returns: "int32",
  args: ["const char*"]
});

puts("hello from edon");
```

A `char*` or `const char*` return value can be converted back to a JavaScript string when returned by a native function.

## 64-bit Integers

`int64` and `uint64` support JavaScript `BigInt` values.

```js
const getValue = native.cfunction("get_value", {
  returns: "uint64",
  args: []
});

const value = getValue();
```

Safe JavaScript `Number` values can also be used where the value can be represented exactly.

## Pointers

Pointer types use standard C pointer syntax.

```js
const readValue = native.cfunction("read_value", {
  returns: "int32",
  args: ["int32*"]
});
```

Pointers can be used with native memory allocated through `edon:ffi` and with compatible native pointer values.

## Function Pointers

Function pointer declarations use standard C declarator syntax.

```js
const callCallback = native.cfunction("call_callback", {
  returns: "int32",
  args: ["int (*)(int)", "int32"]
});
```

A JavaScript callback can be created with `createCallback()` and passed to the native function.

```js
const callback = createCallback(
  value => value + 10,
  {
    returns: "int32",
    args: ["int32"]
  }
);

const result = callCallback(callback, 32);
```

The function pointer declaration describes the complete native callback signature.

Examples:

```text
int (*)(int)
int (*)(int, double)
void (*)(const char*)
```

## Arrays and Declarators

`cfunction()` uses the `edon:ffi` C type parser, which supports C declarator syntax including arrays and parenthesized declarators.

Examples:

```text
int32[4]
double[8]
int (*)(int)
```

## Native Modules

`cfunction()` works with both runtime-compiled C modules and loaded native libraries.

### Runtime-compiled C

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
```

### Native library

```js
const libc = loadLibrary("libc.so.6");

const puts = libc.cfunction("puts", {
  returns: "int32",
  args: ["const char*"]
});
```

## Validation

`cfunction()` validates the function name and signature before creating the callable.

The following conditions produce errors:

- The native function does not exist
- The return type is missing
- An argument type is missing
- A type name is invalid
- The argument list is invalid
- The native signature cannot be prepared

For example:

```js
native.cfunction("missing_function", {
  returns: "int32",
  args: []
});
```

throws because the requested native symbol cannot be resolved.


## Return Value

The value returned by `cfunction()` is directly callable from JavaScript.

```js
const result = add(2, 3);
```

The JavaScript return value corresponds to the native return type declared by `returns`.
For `void` functions, the call produces the JavaScript `undefined` value.

