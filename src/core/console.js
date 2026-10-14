(function() {
const {format} = globalThis.util;

globalThis.console = {
  log : (...args) => __edon_write(1, format(...args) + "\n"),

  error : (...args) => __edon_write(2, format(...args) + "\n"),

  warn : (...args) => __edon_write(2, format(...args) + "\n"),

  info : (...args) => __edon_write(1, format(...args) + "\n"),

  assert:
      (assertion, ...args) => {
        if (!assertion) {
          const err =
              new Error("Assertion failed" + (args.length > 0 ? ": " + format(...args) : ""));

          __edon_write(2, err.stack + "\n");
        }
      }
};
})();
