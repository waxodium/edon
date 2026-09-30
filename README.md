# Edon

A *distinct* **JavaScript**, **TypeScript** runtime powered by [JavaScriptCore]((https://github.com/WebKit/WebKit/tree/main/Source/JavaScriptCore)), built to cross and push the limits into native code.


#### Q1: What does it do?
Compile C. Load native libraries (`.dll`, `.so`, `.dylib`). Call C functions. Work with memory, pointers, and structs. Interact directly with the native ABI.

Write your application in JavaScript or TypeScript, embed native C where you need it, and compile the whole thing into a standalone native executable or **optinally run it** as a runtime.

Compatible with [node.js](https://nodejs.org/) platforms.

( And it is essentially a bridge )


### Code Examples

#### Allocation: 
```js
const { c, allocateSharedBuffer, addressOf } = require("edon:ffi");

const native = c`
    #include <stdint.h>

    void transform(uint8_t *data, int size) {
        for (int i = 0; i < size; i++)
            data[i] ^= 0xFF;
    }
`;
const transform = native.cfunction("transform", {
    returns: "void",
    args: ["pointer", "int"]
});

const buffer = allocateSharedBuffer(16);
const bytes = new Uint8Array(buffer);
bytes.fill(42);
transform(addressOf(buffer), bytes.length);

console.log(bytes[0], bytes[15]);
```

#### Library Calls:
```js
const { loadLibrary } = require("edon:ffi");

const libc = loadLibrary("libc.so.6");

const puts = libc.cfunction("puts", {
    returns: "int32",
    args: ["cstring"]
});

puts("hello from libc");
```


## Install

Development packages:
- JavaScriptCoreGTK 4.1
- libffi
- TinyCC

(there are no install scripts, may inconveniently install these packages manually)

```sh
git clone https://github.com/waxodium/edon.git && cd ./edon
make
./build/edon
```

## License
MIT license, check the license file at root source code for more details:
[LICENSE](./LICENSE)


