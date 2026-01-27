# Lama Iterative Stack Machine Interpreter

## Regression testing and performance comparison

The repository provides two Makefile targets that run regression tests on the
standard toolchain and then re-run them on the VM to compare performance:

- `make vm-regression` — runs `regression` test suite on `vm`.
- `make vm-regression-expressions` — runs `regression_long` (expressions) suite on `vm`.

Each target prints timings in milliseconds for both runs (standard dune tests
and VM execution). Example output:

```
Removing tests that don't compile with standard compiler...
Running standard dune regression tests...
Running standard dune regression tests... finished in 971ms
Applying dune patch for regression...
Patching regression/test*.t files...
Running regression tests on vm...
Running regression tests on vm... finished in 8742ms
Done! regression is now configured to use vm.
```