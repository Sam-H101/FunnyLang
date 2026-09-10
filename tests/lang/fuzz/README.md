# Fuzz golden

A port of `tests/test_fuzz.py`: throw garbage and near-garbage at the front
end and assert that nothing but a clean FunnyLang error ever comes back.

**The seed is fixed and printed.** A fuzzer you cannot re-run is a bug report
you cannot act on, so the run is reproducible by construction and the seed is
in the output rather than buried in the source. `rizz` is xoshiro256** seeded
through splitmix64 — deterministic and identical on every platform — so this
is a golden and not a coin flip.

Each case goes through the real command line (`sus.toolchain()` plus
`sus.run_bytecode`), so what is fuzzed is the shipped pipeline rather than a
library call into it. A build costs about 6 ms, which is what sets the case
counts: 200 token soups and 100 grammar-aware programs, about 2.4 seconds.
The pytest version did 2000 and 500 against an in-process compiler.

The assertion is the pytest one restated: a clean rejection is fine — garbage
*should* be rejected — and anything else is the bug. Concretely, the CLI must
exit 0 or 1 and no error may escape it. A 69 would be a `ComputerExploded`;
a non-`ghost` flavor would be an error the CLI failed to catch. Either prints
the offending source, so a failure is actionable without re-running anything.

**When the counts change, look before updating the golden.** "1 compiled, 199
rejected" is a property of the grammar and the seed together. A change to
either moves it legitimately; a change to neither means something started
accepting or rejecting input it did not before.
