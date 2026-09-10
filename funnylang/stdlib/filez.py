"""`filez` — file I/O (PLAN.md §M6 task 4). Required by M12 self-hosting."""
from __future__ import annotations

import os
from pathlib import Path

from ..errors import SkillIssue, TypeVibeMismatch
from ..values import GHOST, Module, NativeFn, Stash, type_name


def _nf(name, fn, lo, hi=None):
    return NativeFn(name, fn, lo, hi)


def _path_str(v, fn_name):
    if not isinstance(v, str):
        raise TypeVibeMismatch(f"'{fn_name}' needs a yapstring path, not a {type_name(v)}.")
    return v


def _io_wrap(fn_name, path, fn):
    try:
        return fn()
    except OSError as exc:
        raise SkillIssue(
            f"'{fn_name}' on '{path}' failed: {exc.strerror or exc}.",
            roast=f"couldn't {fn_name} `{path}`. the filesystem said no.",
        )


def _slurp(vm, a):
    path = _path_str(a[0], "slurp")
    return _io_wrap("slurp", path, lambda: Path(path).read_text(encoding="utf-8"))


def _yeet_out(vm, a):
    path = _path_str(a[0], "yeet_out")
    text = a[1]

    def _do():
        # newline="" so a "\n" stays a "\n": the default would rewrite it to
        # os.linesep, making the bytes on disk depend on which OS wrote them.
        # `slurp` translates on the way *in* (universal newlines) precisely
        # so that reading is platform-independent; writing has to be too.
        with open(path, "w", encoding="utf-8", newline="") as f:
            f.write(text)

    _io_wrap("yeet_out", path, _do)
    return len(text)


def _append_to(vm, a):
    path = _path_str(a[0], "append_to")
    text = a[1]

    def _do():
        with open(path, "a", encoding="utf-8", newline="") as f:
            f.write(text)

    _io_wrap("append_to", path, _do)
    return len(text)


def _append_bytes(vm, a):
    """The raw-bytes counterpart to `append_to`. `write_bytes` already
    existed; without this a program could create a binary but never add to
    one, which is exactly what `funny yeet` does to the runtime stub."""
    path = _path_str(a[0], "append_bytes")
    data = a[1]
    if not isinstance(data, Stash):
        raise TypeVibeMismatch("'append_bytes' needs a stash of numbas.")
    data = bytes(int(x) & 0xFF for x in data.items)

    def _do():
        with open(path, "ab") as f:
            f.write(data)

    _io_wrap("append_bytes", path, _do)
    return len(data)


def _make_executable(vm, a):
    """Marks `path` runnable: the executable bits on POSIX, a no-op on
    Windows, where being executable is a matter of the extension."""
    import stat

    path = _path_str(a[0], "make_executable")
    if os.name != "nt":
        mode = os.stat(path).st_mode
        os.chmod(path, mode | stat.S_IXUSR | stat.S_IXGRP | stat.S_IXOTH)
    return GHOST


def _temp_file(vm, a):
    """A path in the OS temp directory that nothing else is using. The file
    is the caller's to remove -- `obliterate` when done."""
    import tempfile

    from ..errors import TypeVibeMismatch
    from ..values import type_name

    prefix = "funny"
    if a and a[0] is not GHOST:
        if not isinstance(a[0], str):
            raise TypeVibeMismatch(f"'temp_file' needs a yapstring prefix, not a {type_name(a[0])}.")
        prefix = a[0]
    fd, path = tempfile.mkstemp(prefix=prefix)
    os.close(fd)
    return path


def _exists(vm, a):
    return Path(_path_str(a[0], "exists")).exists()


def _is_dir(vm, a):
    # `exists` alone cannot drive a recursive walk, which is what
    # selfhost/test.funny needs to find *.funny/*.expected pairs under a
    # directory tree (NATIVE_PLAN.md N8 task 4). Both of these answer `cap`
    # for a path that does not exist, rather than raising -- same as
    # Python's own `Path.is_dir()`/`is_file()`.
    return Path(_path_str(a[0], "is_dir")).is_dir()


def _is_file(vm, a):
    return Path(_path_str(a[0], "is_file")).is_file()


def _obliterate(vm, a):
    path = _path_str(a[0], "obliterate")

    def _do():
        p = Path(path)
        if p.is_dir():
            p.rmdir()
        else:
            p.unlink()

    _io_wrap("obliterate", path, _do)
    return True


def _list_dir(vm, a):
    path = _path_str(a[0], "list_dir")
    return Stash(_io_wrap("list_dir", path, lambda: sorted(p.name for p in Path(path).iterdir())))


def _mkdir(vm, a):
    path = _path_str(a[0], "mkdir")
    _io_wrap("mkdir", path, lambda: Path(path).mkdir(parents=True, exist_ok=True))
    return True


def _read_bytes(vm, a):
    path = _path_str(a[0], "read_bytes")
    data = _io_wrap("read_bytes", path, lambda: Path(path).read_bytes())
    return Stash(list(data))


def _write_bytes(vm, a):
    path = _path_str(a[0], "write_bytes")
    data = a[1]
    if not isinstance(data, Stash):
        raise TypeVibeMismatch("'write_bytes' needs a stash of numbas.")
    payload = bytes(int(x) & 0xFF for x in data.items)
    _io_wrap("write_bytes", path, lambda: Path(path).write_bytes(payload))
    return len(payload)


def _abs_path(vm, a):
    return str(Path(_path_str(a[0], "abs_path")).resolve())


def _join_path(vm, a):
    parts = [_path_str(p, "join_path") for p in a]
    result = Path(parts[0])
    for p in parts[1:]:
        result = result / p
    return str(result)


def _dir_of(vm, a):
    return str(Path(_path_str(a[0], "dir_of")).parent)


def _base_of(vm, a):
    return Path(_path_str(a[0], "base_of")).name


def _ext_of(vm, a):
    return Path(_path_str(a[0], "ext_of")).suffix


def build() -> Module:
    members = {
        "slurp": _nf("slurp", _slurp, 1),
        "yeet_out": _nf("yeet_out", _yeet_out, 2),
        "append_to": _nf("append_to", _append_to, 2),
        "exists": _nf("exists", _exists, 1),
        "append_bytes": _nf("append_bytes", _append_bytes, 2),
        "make_executable": _nf("make_executable", _make_executable, 1),
        "temp_file": _nf("temp_file", _temp_file, 0, 1),
        "is_dir": _nf("is_dir", _is_dir, 1),
        "is_file": _nf("is_file", _is_file, 1),
        "obliterate": _nf("obliterate", _obliterate, 1),
        "list_dir": _nf("list_dir", _list_dir, 1),
        "mkdir": _nf("mkdir", _mkdir, 1),
        "read_bytes": _nf("read_bytes", _read_bytes, 1),
        "write_bytes": _nf("write_bytes", _write_bytes, 2),
        "abs_path": _nf("abs_path", _abs_path, 1),
        "join_path": _nf("join_path", _join_path, 1, 255),
        "dir_of": _nf("dir_of", _dir_of, 1),
        "base_of": _nf("base_of", _base_of, 1),
        "ext_of": _nf("ext_of", _ext_of, 1),
    }
    return Module("filez", members)
