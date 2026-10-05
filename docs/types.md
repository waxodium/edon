# C Type Declarations

## Table of Contents

- [Overview](#overview)
- [Primitive Types](#primitive-types)
- [Pointers](#pointers)
- [Arrays](#arrays)
- [Qualifiers](#qualifiers)
- [Function Pointers](#function-pointers)
- [Using Types](#using-types)
- [Example Usage](#example-usage)

---

## Overview

`edon:ffi` uses C-style type declarations to describe native function arguments, return values, and callback signatures.

Types are used by [`cfunction()`](./cfunction.md) and [`createCallback()`](./callbacks.md).

```js
const add = native.cfunction("add", {
  returns: "int32",
  args: ["int32", "int32"]
});
```

Type declarations follow C declarator syntax, including pointers, arrays, qualifiers, and function pointers.

---

## Primitive Types

Supported primitive types include:

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
```

Example:

```js
const add = native.cfunction("add", {
  returns: "int32",
  args: ["int32", "int32"]
});
```

---

## Pointers

Pointers are expressed using `*`.

```text
int32*
uint8*
void*
char*
```

Qualified pointers can also be declared:

```text
const char*
const int32*
volatile int32*
```

Pointers are commonly used when native functions operate on memory or strings.

Pointer declarators can also be combined with other C declarators.

```text
int32**
char**
int32*[4]
```

---

## Arrays

Arrays are expressed using a type followed by a fixed element count.

```text
int32[4]
double[8]
uint8[256]
```

The element type and array size are part of the native type declaration.

Arrays can be combined with pointers and other declarators where supported by the C declarator syntax.

---

## Qualifiers

`edon:ffi` supports common C type qualifiers:

```text
const
volatile
restrict
```

Examples:

```text
const char*
const int32*
volatile int32*
restrict int32*
```

Qualifiers are preserved as part of the parsed type declaration.

Pointer qualifiers can also be specified directly on the pointer declarator:

```text
int32 *const
int32 *volatile
int32 *restrict
```

---

## Function Pointers

Function pointers use standard C declarator syntax.

A function pointer consists of:

1. A return type
2. A parenthesized `*` declarator
3. A parameter list

Examples:

```text
int (*)(int)
int (*)(int, double)
void (*)(const char*)
```

These describe pointers to functions with the following signatures:

```text
int(int)
int(int, double)
void(const char*)
```

The parentheses around `*` are significant.

For example:

```text
int (*)(int)
```

is a pointer to a function returning `int32`, while:

```text
int*(int)
```

describes a different declarator: a function returning a pointer.

### Named Function Pointers

Function pointers can also appear as named C declarations:

```text
int (*callback)(int)
```

This declares `callback` as a pointer to a function returning `int32` and accepting one `int32` argument.

Multiple parameters are supported:

```text
int (*callback)(int, double)
```

A function with no parameters can be declared using `void`:

```text
int (*callback)(void)
```

This represents a function pointer whose function takes no arguments.

### Function Pointers as Arguments

Function pointers can be supplied as arguments to `cfunction()`.

```js
const call = native.cfunction("call_callback", {
  returns: "int32",
  args: [
    "int (*)(int)",
    "int32"
  ]
});
```

The first argument is a pointer to a function taking an `int32` and returning an `int32`.

A compatible callback can then be passed to the native function:

```js
const callback = createCallback(
  value => value + 1,
  {
    returns: "int32",
    args: ["int32"]
  }
);

console.log(call(callback, 41));
```

The callback's signature must match the function-pointer type expected by the native function.

### Function Pointers as Return Values

Function pointers can also be used as return types.

For example:

```text
int (*)(int)
```

can describe a native function that returns a pointer to a function taking an `int32` and returning an `int32`.

A native declaration might look like:

```c
int (*get_callback(void))(int);
```

The corresponding `cfunction()` declaration is:

```js
const getCallback = native.cfunction("get_callback", {
  returns: "int (*)(int)",
  args: []
});
```

The returned value represents a native function pointer and can be invoked using its declared signature.

### Parenthesized Declarators

Function pointers rely on C's parenthesized declarator syntax.

For example:

```text
int (*)(int)
```

contains a pointer declarator inside parentheses.

More complex declarations can combine function pointers with additional pointer or array declarators:

```text
int (**)(int)
int (*[4])(int)
```

The parentheses determine how the declarator is grouped and therefore which type is constructed.

`edon:ffi` preserves this declarator structure when parsing function-pointer types.

---

## Using Types

Types are supplied as strings in function signatures.

```js
const fn = native.cfunction("process", {
  returns: "int32",
  args: [
    "const char*",
    "uint32",
    "void*"
  ]
});
```

The same type syntax is used for callback signatures:

```js
const callback = createCallback(
  value => value,
  {
    returns: "int32",
    args: ["int32"]
  }
);
```

Function-pointer types use the same declaration syntax:

```js
const call = native.cfunction("call_callback", {
  returns: "int32",
  args: [
    "int (*)(int)",
    "int32"
  ]
});
```

The declared types determine native ABI layout and JavaScript-to-native value conversion.

---

## Example Usage

A native function can accept a function pointer and invoke it:

```js
const {
  c,
  createCallback,
  destroyCallback
} = require("edon:ffi");

const native = c(`
  int call_callback(int (*callback)(int), int value) {
    return callback(value);
  }
`);

const callCallback = native.cfunction("call_callback", {
  returns: "int32",
  args: [
    "int (*)(int)",
    "int32"
  ]
});

const callback = createCallback(
  value => value * 2,
  {
    returns: "int32",
    args: ["int32"]
  }
);

console.log(callCallback(callback, 21));

destroyCallback(callback);
```

Output:

```text
42
```

The native function expects:

```text
int (*)(int)
```

and the JavaScript callback provides the compatible signature:

```text
returns: "int32"
args: ["int32"]
```
