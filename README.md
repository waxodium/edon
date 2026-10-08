# Edon.js

A *distinct* **JavaScript**, **TypeScript** runtime powered by [JavaScriptCore]((https://github.com/WebKit/WebKit/tree/main/Source/JavaScriptCore)), built to cross and push the limits into native code.

#### Q1: What does it do?
Compile C. Load native libraries (`.dll`, `.so`, `.dylib`). Call C functions. Work with memory, pointers, and structs. Interact directly with the native ABI.

Write your application in JavaScript or TypeScript, embed native C where you need it, and compile the whole thing into a standalone native executable or **optinally run it** as a runtime.

Edon is a bridge between JavaScript and native C.

#### Q2: What's the difference?
`edon` is designed around a different idea from Node.js, Bun, Deno, and WebAssembly.

Node.js, Bun, and Deno primarily provide JavaScript runtimes with large runtime APIs and huge development enviroment.

Edon is a full runtime and compile apps into standalone executables, while providing APIs to interact with C programming language natively. And keeping compatibility with the [node.js](https://nodejs.org/) programs.

#### Q3: Why should use it?
Choose edon for direct JavaScript integration with C code, native libraries, OS APIs, native memory, and platform capabilities. A lower-level alternative to web-focused development.

#### Q4: How to use it?
Take some look at [examples](./examples) folder and read edon documentation through
[docs](./docs) directory.

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
Edon is MIT licensed, check the license file at root source code for more details:
[LICENSE](./LICENSE)

