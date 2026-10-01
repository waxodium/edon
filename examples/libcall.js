const { loadLibrary } = require("edon:ffi");

const libc = loadLibrary("libc.so.6");

const puts = libc.cfunction("puts", {
    returns: "int",
    args: ["const char*"]
});

puts("hello from libc");
