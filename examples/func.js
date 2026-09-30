/*
    Simple compilation C at runtime
*/

const { c } = require("edon:ffi");

const math = c`
int add(int a, int b) {
    return a + b;
}
`;

const add = math.cfunction("add", {
    returns: "int32",
    args: ["int32", "int32"]
});

// add function from `const add` JS
console.log(add(20, 22)); 
