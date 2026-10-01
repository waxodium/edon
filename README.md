# Edon

A *distinct* **JavaScript**, **TypeScript** runtime powered by [JavaScriptCore]((https://github.com/WebKit/WebKit/tree/main/Source/JavaScriptCore)), built to cross and push the limits into native code.

#### Q1: What does it do?
Compile C. Load native libraries (`.dll`, `.so`, `.dylib`). Call C functions. Work with memory, pointers, and structs. Interact directly with the native ABI.

Write your application in JavaScript or TypeScript, embed native C where you need it, and compile the whole thing into a standalone native executable or **optinally run it** as a runtime.

Compatible with [node.js](https://nodejs.org/) platforms.

Edon is a bridge between JavaScript and native C.

#### Q2: How does it works?
Take some look at [examples](./examples) folder

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
