const { c } = require("edon:ffi");

const native = c`
#include <stdio.h>

void hello(const char *name) {
    printf("Hello, %s!\\n", name)
}
`;

const hello = native.cfunction("hello", {
    returns: "void",
    args: ["cstring"]
});

hello("edon");
