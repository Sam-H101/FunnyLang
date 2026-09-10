# The differential corpus, kept

These 52 programs were `tests/native/programs/`, the corpus the differential
suite ran. That suite had **no** `.expected` files: it compiled each program
with both the C runtime and the Python reference, ran both VMs, and diffed
stdout live — which is why it could never drift out of date, and also why
deleting Python would have deleted the corpus outright.

So the expected side was captured from the **Python VM**, once, on the last
day it existed (`build/n4/save_differential_corpus.py`). Every program was run
twice and only kept if it agreed with itself, because a program that disagrees
with itself was never a golden candidate. All 53 agreed; 52 are here.

The provenance is the point: these files say what the *reference*
implementation printed, not what this one prints. They are the same
programs, covering arithmetic and bitwise ops, comparisons, control flow,
closures, deep recursion, globals and locals, squads, pointers, bignums,
string and collection methods, every stdlib module, error flavors and GC
stress.

## What was dropped, and one line that was changed

`modules_internet.funny` is not here. Its output depends on `FUNNY_NO_NET` and
on whether the host can reach the network, so it is not a golden in any
environment — `NATIVE_PLAN.md` §5 puts network behaviour in the
"tested on properties" bucket, and the same call was made for
`test_internet_is_it_up_false_when_disabled`.

`cli_primitives.funny` lost one line: it asserted that `computer.env("PATH")`
is set. CI runs part of this corpus under `env -i`, where PATH genuinely is
unset, so the assertion was about the ambient environment rather than about
the language. The half that matters — a name nothing sets reads back as
`ghost` — is still there.
