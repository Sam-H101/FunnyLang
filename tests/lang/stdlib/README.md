# Standard-library goldens

Converted from `tests/test_stdlib.py`, `tests/test_vm.py` and
`tests/test_squads.py`, which were already `.funny`/`.expected` pairs wearing
a pytest disguise:

```python
def test_mafs_sqrt():
    assert _r("gimme mafs\nyap mafs.sqrt(16)\n") == "4.0\n"
```

Both halves are literals a person wrote as a specification, so the conversion
is a transcription — nothing here was captured from a run, and a disagreement
with the native VM is a finding rather than a golden to regenerate. Three
disagreed, and each is recorded below rather than quietly adjusted.

## `.pending` files: blocked on Unicode tables

`yapper_is_letter` and `yapper_is_alnum` are correct and **the implementation
is wrong**, so they are parked with a `.pending` suffix instead of being
deleted or weakened: `funny test` only pairs `*.funny` with `*.expected`, so
they sit inert with their expected output intact, and activating them is
dropping the suffix.

`yapper.is_letter("中")` is `fax` in the reference and `cap` natively, because
the native implementation is `isalpha()` over bytes. That is the N4 AGENT
CHOICE logged in `native/yapper.h` — real Unicode category tables were
deferred, and `native/unicode_tbl.c` with its generator is `NATIVE_PLAN.md`
N11 task 3. The same gap is why `yo 変数 = 1` does not lex; see
`tests/lang/lexer/README.md`.

## Dropped: `internet.is_it_up` when networking is disabled

`tests/test_stdlib.py::test_internet_is_it_up_false_when_disabled` sets
`FUNNY_NO_NET=1` with `monkeypatch` and asserts the call returns `cap`. A
golden cannot set an environment variable for the program it runs, and
`NATIVE_PLAN.md` §5 already puts network behaviour in the "tested on
properties, not goldens" bucket along with randomness, timing and hardware.
Making it a golden would mean either an `!ENV` directive that mutates the
runner's own process environment, or a hostname chosen to fail DNS — a test
that hangs on a slow resolver. Neither is worth it for one assertion.
