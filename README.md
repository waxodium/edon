# Edon.js

**JavaScript, with a way into the native code.**

A JavaScript runtime powered by [JavaScriptCore](https://github.com/WebKit/WebKit/tree/main/Source/JavaScriptCore). This runtime is the closest ever for JS to interact with bare-metal C programming language. It is an independent runtime from [node.js](nodejs.org)

[Getting Started](./docs) · [Examples](./examples) · [License](./LICENSE)

---

## What is Edon?
Edon is a computer software, created for computer programming. It is technically known as a **"Distinctive and minimalist JavaScript runtime"** made for interacting with C language natively. 

The Edon.js project attempts to replicate Go-inspired concepts for JavaScript
## Get started

### Requirements

Edon currently depends on three libraries:

- JavaScriptCoreGTK 4.1
- libffi
- TinyCC

You may need to install these development packages manually using your system's package manager.

### Build from source

```sh
git clone https://github.com/waxodium/edon.git
cd edon
make
```

Run Edon:

```sh
./build/edon
```

## Explore

Want to see what Edon can do?

- Browse [`examples/`](./examples) to explore example programs.
- Read the [`docs/`](./docs) to learn more about the runtime and its features.

## Project status

Edon is a work in progress. Features and APIs are still evolving..

We follows the standard conventional Github contributions for our project. 

## License

Edon is open source and available under the [MIT License](./LICENSE).
