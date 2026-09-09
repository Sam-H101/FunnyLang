"""The entry point frozen into the native runtime stub (PLAN.md §5.4/§M10).

Reads its own trailing payload (a linked `.funnypak`), loads it, and runs
it — no temp files, no extraction. This file becomes `funnyrt`'s `__main__`
when PyInstaller freezes it; it must not import anything that pulls in the
compiler/parser toolchain (a shipped `.exe` never compiles source, only runs
already-linked bytecode).
"""
from __future__ import annotations

import sys

MAGIC = b"FUNNYYEET"
TRAILER_LEN = 17  # 9-byte magic + 8-byte payload_len (big-endian u64)


def _self_path() -> str:
    # sys.executable is the frozen binary itself when running under
    # PyInstaller's --onefile bootloader; fall back to argv[0] otherwise
    # (running stub_main.py directly, e.g. for local testing).
    return sys.executable if getattr(sys, "frozen", False) else sys.argv[0]


def _read_payload() -> bytes | None:
    path = _self_path()
    try:
        with open(path, "rb") as f:
            f.seek(0, 2)
            size = f.tell()
            if size < TRAILER_LEN:
                return None
            f.seek(size - TRAILER_LEN)
            trailer = f.read(TRAILER_LEN)
            magic, payload_len = trailer[:9], int.from_bytes(trailer[9:], "big")
            if magic != MAGIC:
                return None
            if size < TRAILER_LEN + payload_len:
                return None
            f.seek(size - TRAILER_LEN - payload_len)
            return f.read(payload_len)
    except OSError:
        return None


def main() -> int:
    # Absolute imports, deliberately: PyInstaller's bootloader runs this
    # file as a bare top-level script with no package context, so `from
    # .errors import ...` fails with "attempted relative import with no
    # known parent package" the moment it's frozen — `funnylang` itself is
    # still on sys.path inside the bundle, so the absolute form resolves.
    from funnylang.errors import ComputerExploded, FunnyError, render_diagnostic
    from funnylang.modules import CanonicalSource, make_pak_module_loader
    from funnylang.serializer import load_funnypak
    from funnylang.stdlib import install_stdlib
    from funnylang.vm import VM

    for stream_name in ("stdout", "stderr"):
        stream = getattr(sys, stream_name, None)
        reconfigure = getattr(stream, "reconfigure", None)
        if callable(reconfigure):
            try:
                reconfigure(encoding="utf-8", errors="replace")
            except (ValueError, OSError):
                pass

    payload = _read_payload()
    if payload is None:
        print("this stub is naked. it has no program. sad.", file=sys.stderr)
        return 2

    modules, entry_name = load_funnypak(payload)
    vm = VM()
    install_stdlib(vm)
    vm.module_loader = make_pak_module_loader(modules, entry_name)
    vm.program_args = list(sys.argv[1:])

    try:
        vm.interpret(modules[entry_name], CanonicalSource(entry_name))
    except ComputerExploded as err:
        print(render_diagnostic(err), file=sys.stderr, end="")
        return 69
    except FunnyError as err:
        print(render_diagnostic(err), file=sys.stderr, end="")
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
