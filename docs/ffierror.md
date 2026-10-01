## FFI Errors

### Table of Contents

- [Overview](#overview)
- [Error Categories](#error-categories)
- [Runtime C Compilation](#runtime-c-compilation)
- [Native Libraries](#native-libraries)
- [Function Signatures](#function-signatures)
- [Arguments and Returns](#arguments-and-returns)
- [Callbacks](#callbacks)
- [Native Memory](#native-memory)
- [Native Faults](#native-faults)
- [Example](#example)

## Overview

`edon:ffi` reports invalid native operations as JavaScript exceptions.

Errors can occur while compiling C code, loading libraries, resolving symbols, preparing signatures, converting values, managing callbacks, or accessing native memory.

---

## Error Categories

Common error categories include:

```text
C compilation errors
Native library errors
Symbol errors
Signature errors
Argument errors
Return value errors
Callback errors
Memory errors
Native fault errors
```

The exact error message identifies the failed operation and provides additional context where available.

---

## Runtime C Compilation

Invalid C source produces a JavaScript exception.

```js
const { c } = require("edon:ffi");

const native = c(`
  int broken( {
    return 1;
  }
`);
```

Compilation failures are reported instead of returning an invalid native module.

---

## Native Libraries

`loadLibrary()` throws when a native library cannot be loaded.

```js
const {
  loadLibrary
} = require("edon:ffi");

const library = loadLibrary("missing.so");
```

Typical causes include:

- Invalid path
- Missing library
- Unsupported library
- Native loader failure

---

## Function Signatures

Invalid signatures are rejected when creating native functions or callbacks.

```js
const fn = native.cfunction("add", {
  returns: "unknown",
  args: ["int32"]
});
```

Signature errors can include:

- Missing return type
- Invalid return type
- Invalid argument list
- Invalid argument type
- Invalid declarator
- Unsupported type

---

## Arguments and Returns

Arguments are validated and converted according to the declared native types.

```js
const add = native.cfunction("add", {
  returns: "int32",
  args: ["int32", "int32"]
});

add("not an integer", 10);
```

Invalid values can produce JavaScript exceptions rather than being passed to native code.

Return values are also converted according to the declared return type.

---

## Callbacks

Invalid callback operations produce JavaScript exceptions.

```js
createCallback(
  123,
  {
    returns: "int32",
    args: ["int32"]
  }
);
```

Callback errors can occur when:

- The callback value is not a function
- The callback signature is invalid
- Callback arguments cannot be converted
- The callback return value cannot be converted
- A destroyed callback is used

---

## Native Memory

Invalid memory operations are rejected.

```js
const buffer = allocateSharedBuffer(32);

free(buffer);
free(buffer);
```

Memory errors can include:

- Invalid allocation
- Invalid pointer
- Invalid ownership
- Double free
- Use after free

Native memory must only be accessed while its allocation is valid.

---

## Native Faults

Native faults occurring during protected native calls can be converted into JavaScript exceptions where supported.

Protected native calls include faults such as:

```text
SIGSEGV
SIGBUS
SIGFPE
```

This protection does not make arbitrary native code safe. Native functions can still terminate the process in situations that cannot be recovered by the runtime.

---

## Example

```js
const {
  c,
  loadLibrary
} = require("edon:ffi");

try {
  const native = c(`
    int add(int a, int b) {
      return a + b;
    }
  `);

  const add = native.cfunction("add", {
    returns: "int32",
    args: ["int32", "int32"]
  });

  console.log(add(10, 20));
} catch (error) {
  console.error(error);
}
```

