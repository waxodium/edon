const { c, allocateSharedBuffer, free } = require("edon:ffi");

const native = c`
void write_int(int *ptr, int value) {
    *ptr = value;
}

int read_int(int *ptr) {
    return *ptr;
}
`;

const writeInt = native.cfunction("write_int", {
    returns: "void",
    args: ["pointer", "int32"]
});

const readInt = native.cfunction("read_int", {
    returns: "int32",
    args: ["pointer"]
});

const buffer = allocateSharedBuffer(4);

writeInt(buffer, 1234);

console.log(readInt(buffer));

free(buffer);
