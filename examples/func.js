const { c } = require("edon:ffi");

const math = c`
int add(int a, int b) {
    return a + b;
}
`;

const add = math.cfunction("add", {
    returns: "int",
    args: ["int", "int"]
});

console.log(add(20, 22));
