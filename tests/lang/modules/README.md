# Module goldens

One directory per case, so two cases can both have a `lib.funny` without
colliding. `funny test` pairs `main.funny` with `main.expected`; the imported
files have no golden of their own, so they are never run as tests but are
still there to be imported — which is the behaviour
`test_a_funny_file_with_no_golden_is_ignored` pins.

Converted from `tests/test_modules.py`. Writing them found a crash and a
wrong flavor, both in the native runtime:

- **A circular import segfaulted.** `.funnypak` modules were added to the
  loader's cache only after they *finished*, so a module reached again while
  still running re-entered `vm_run_module` forever and took the C stack with
  it. `gimme "main.funny"` from `main.funny` — the smallest cycle there is —
  was enough. `funnylang/modules.py` has always kept a stack of modules
  currently loading and reported the cycle from it; the native loader now does
  too, entry module included, with the same `circular import: a → b → a`
  message.
- **A missing import was a `SkillIssue`.** `selfhost/bundler.funny` reported
  it with `chuck "<string>"`, and a chucked string is always a `SkillIssue`.
  It is an `ImportSkillIssue` in the reference. Third instance of this
  particular mistake, after the parser's two.

## `walkup` is parked: bundles cannot address a module above the entry

`.pending`, like the Unicode goldens were — the case is written and correct,
and the limitation is real.

A `.funnypak`'s module keys are relative to the **entry file's** directory, so
a module found by walking *up* to a `funny_modules/` directory has no
expressible key: the bundler resolves it fine, keys it by absolute path
because the prefix does not match, and then the runtime loader — which
normalises a quoted `gimme` against the importing module's directory — looks
for `shared.funny` and does not find it. Both implementations have the
limitation; Python's *runtime* loader passes the equivalent pytest only
because it goes back to the filesystem, which a bundle by definition cannot.

The fix is to root keys at the common ancestor of every module in the bundle
rather than at the entry, which changes every key in every bundle. That is
deliberately deferred while byte-identity against the Python bundler is still
one of the checks holding this work together. When `funnylang/` goes, so does
that constraint, and this golden's suffix comes off.

`FUNNYPATH` resolution (`tests/test_modules.py::test_funnypath_resolution`)
is not here at all: it needs an environment variable set for the program under
test, which a golden cannot do, and `selfhost/bundler.funny` documents not
implementing FUNNYPATH in the first place.
