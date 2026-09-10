# Command-line goldens

`tests/test_cli.py` drove the CLI as a subprocess 34 times. There is no
process-spawn primitive in FunnyLang — deliberately, and adding one to test
the CLI would be a large new capability bought for a small reason.

`sus.run_bytecode` already does the interesting part: it runs a bundle in an
isolated VM with argv, captured stdout, captured stderr and an exit code,
which is exactly what a CLI invocation is. The only missing piece was getting
hold of the toolchain's bytes, and `sus.toolchain()` supplies them — the same
bytes `main.c` runs, so these goldens exercise the shipped dispatch rather
than a re-linked copy of it.

## Nesting had to be fixed first

A run inside a run used to leak. `sus.run_program` sent the user program's
output to the real stdout unconditionally, and `yell` plus the uncaught-error
diagnostic went to the real stderr, so driving the CLI from inside a captured
child VM sprayed both into the test runner's own output. `RunnerOptions` now
carries `out` and `err`, and the VM carries an `err` stream beside its `out`.
At the top level those *are* stdout and stderr, so nothing changed there.

## These goldens are anchored to the repository root

They pass paths like `examples/hello.funny` to the CLI, so they run under
`funny test tests/lang` (and `funny test .`) from the repository root. That is
how CI, `docs/NATIVE.md` and the release checks invoke it.

`dispatch.expected` contains the CLI's usage banner, so changing the help text
updates this golden — which is the point: the help text is user-facing and
nothing else pinned it.
