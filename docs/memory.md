## Native Memory

### Table of Contents

- [Overview](#overview)
- [Allocating Memory](#allocating-memory)
- [Accessing Memory](#accessing-memory)
- [Native Addresses](#native-addresses)
- [Freeing Memory](#freeing-memory)
- [Memory Ownership](#memory-ownership)
- [Errors](#errors)
- [Example Usage](#example-usage)

## Overview

`edon:ffi` provides native memory allocation and access through:

```js
const {
  allocateSharedBuffer,
  addressOf,
  free
} = require("edon:ffi");
```

Native memory allocated by `allocateSharedBuffer()` can be accessed directly from JavaScript using `ArrayBuffer`, `DataView`, and typed arrays.

---

## Allocating Memory

`allocateSharedBuffer()` allocates native memory and returns an `ArrayBuffer`.

```js
const buffer = allocateSharedBuffer(64);
```

The returned buffer can be used with standard JavaScript memory APIs.

```js
const view = new DataView(buffer);
view.setInt32(0, 123, true);
console.log(view.getInt32(0, true));
```

The allocation remains valid until it is explicitly released with `free()`.

---

## Accessing Memory

Native memory can be accessed using `DataView` and typed arrays.

```js
const buffer = allocateSharedBuffer(16);
const view = new DataView(buffer);

view.setInt32(0, 100, true);
view.setInt32(4, 200, true);

console.log(view.getInt32(0, true));
console.log(view.getInt32(4, true));
```

The same memory can also be accessed by native functions when its address is passed to compatible native code.

---

## Native Addresses

`addressOf()` returns the native address associated with an allocation.

```js
const buffer = allocateSharedBuffer(64);
const address = addressOf(buffer);
console.log(address);
```

The address remains associated with the allocation while the memory is alive.

Native addresses can be used when interfacing with native APIs that require pointers.

---

## Freeing Memory

`free()` releases memory allocated by `allocateSharedBuffer()`.

```js
const buffer = allocateSharedBuffer(64);
free(buffer);
```

After the allocation has been freed, the buffer must not be used for native memory access.

```js
const buffer = allocateSharedBuffer(64);
free(buffer);

// Do not access buffer after this point.
```

An allocation must only be freed once.

---

## Memory Ownership

`edon:ffi` tracks native memory ownership.

A buffer returned by `allocateSharedBuffer()` owns its native allocation and can be released with `free()`.

Independent allocations have independent ownership:

```js
const first = allocateSharedBuffer(32);
const second = allocateSharedBuffer(32);

const firstAddress = addressOf(first);
const secondAddress = addressOf(second);
```

The allocations are separate native memory regions.

Using a freed allocation is invalid.

---

## Errors

Invalid memory operations produce JavaScript exceptions.

Typical errors include:

- Invalid memory allocation
- Invalid pointer or buffer
- Double free
- Use after free
- Invalid memory ownership
- Invalid native address

Native memory errors are reported before an invalid operation is performed where the runtime can detect the condition.

---

## Example Usage

```js
const {
  allocateSharedBuffer,
  addressOf,
  free
} = require("edon:ffi");

const buffer = allocateSharedBuffer(32);

const view = new DataView(buffer);

view.setInt32(0, 123, true);
view.setFloat64(8, 3.14, true);

console.log(view.getInt32(0, true));
console.log(view.getFloat64(8, true));

const address = addressOf(buffer);

console.log(address);

free(buffer);
```
