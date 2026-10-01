## Callbacks

### Table of Contents

- [Overview](#overview)
- [Creating Callbacks](#creating-callbacks)
- [Callback Signatures](#callback-signatures)
- [Passing Callbacks to Native Functions](#passing-callbacks-to-native-functions)
- [Destroying Callbacks](#destroying-callbacks)
- [Errors](#errors)
- [Example Usage](#example-usage)

## Overview

`createCallback()` creates a native function pointer backed by a JavaScript function.

```js
const {
  createCallback,
  destroyCallback
} = require("edon:ffi");
```

Callbacks allow native C code to call JavaScript functions through compatible function-pointer signatures.

---

## Creating Callbacks

```js
const callback = createCallback(
  value => value + 10,
  {
    returns: "int32",
    args: ["int32"]
  }
);
```

The first argument is the JavaScript function.

The second argument describes the native function-pointer signature.

---

## Callback Signatures

A callback signature uses the same C-style type declarations used by [`cfunction()`](./cfunction.md).

```js
const callback = createCallback(
  function (value) {
    return value * 2;
  },
  {
    returns: "int32",
    args: ["int32"]
  }
);
```

The declared signature determines how native arguments are converted to JavaScript values and how the JavaScript return value is converted back to the native return type.

---

## Passing Callbacks to Native Functions

Callbacks can be passed to native functions that accept compatible function pointers.

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
  value => value + 10,
  {
    returns: "int32",
    args: ["int32"]
  }
);

console.log(callCallback(callback, 32));
```

The callback is represented as a native function pointer when passed to native code.

---

## Destroying Callbacks

Callbacks hold native resources and should be released when they are no longer needed.

```js
destroyCallback(callback);
```

After a callback has been destroyed, it must not be passed to native code.

---

## Errors

Invalid callback functions and signatures produce JavaScript exceptions.

Callback operations can also fail when:

- The callback argument is not a function
- The callback signature is invalid
- The native callback cannot be created
- A destroyed callback is used
- Callback arguments or return values cannot be converted

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

const result = callCallback(callback, 21);

console.log(result);

destroyCallback(callback);
```

