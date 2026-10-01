## C Type Declarations

### Table of Contents

- [Overview](#overview)
- [Primitive Types](#primitive-types)
- [Pointers](#pointers)
- [Arrays](#arrays)
- [Qualifiers](#qualifiers)
- [Function Pointers](#function-pointers)
- [Using Types](#using-types)
- [Example Usage](#example-usage)

## Overview

`edon:ffi` uses C-style type declarations to describe native function arguments, return values, and callback signatures.

Types are used by [`cfunction()`](./cfunction.md) and [`createCallback()`](./callbacks.md).

```js
const add = native.cfunction("add", {
  returns: "int32",
  args: ["int32", "int32"]
});
```

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

---

## Arrays

Arrays are expressed using a type followed by a fixed element count.

```text
int32[4]
double[8]
uint8[256]
```

The element type and array size are part of the native type declaration.

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

---

## Function Pointers

Function pointers use standard C declarator syntax.

```text
int (*)(int)
int (*)(int, double)
void (*)(const char*)
```

For example:

```js
const call = native.cfunction("call_callback", {
  returns: "int32",
  args: ["int (*)(int)", "int32"]
});
```

A compatible callback can then be created with [`createCallback()`](./callbacks.md).

```js
const callback = createCallback(
  value => value + 1,
  {
    returns: "int32",
    args: ["int32"]
  }
);
```

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

The declared types determine native ABI layout and JavaScript-to-native value conversion.

---

## Example Usage

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
  args: ["int (*)(int)", "int32"]
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

