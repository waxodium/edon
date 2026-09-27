const { loadLibrary } = require("edon:ffi");

const libc = loadLibrary("libc.so.6");

const puts = libc.symbol("puts", {
    returns: "int32",
    args: ["cstring"]
});

puts("hello from libc");
