"""The `funny` command-line interface.

M5 wires up just enough of `run` to execute a `.funny` file end to end.
The real CLI (subcommands, flags, REPL, disassembler, formatter, bootstrap)
lands in M8.
"""
from __future__ import annotations

import sys

from . import __version__
from .compiler import Compiler
from .errors import ComputerExploded, FunnyError, ParseErrorBundle, render_diagnostic, render_parse_error_bundle
from .parser import parse_source
from .resolver import resolve_program
from .source import SourceFile
from .stdlib import install_stdlib
from .vm import VM

BANNER = r"""
   ███████╗██╗   ██╗███╗   ██╗███╗   ██╗██╗   ██╗
   ██╔════╝██║   ██║████╗  ██║████╗  ██║╚██╗ ██╔╝
   █████╗  ██║   ██║██╔██╗ ██║██╔██╗ ██║ ╚████╔╝
   ██╔══╝  ██║   ██║██║╚██╗██║██║╚██╗██║  ╚██╔╝
   ██║     ╚██████╔╝██║ ╚████║██║ ╚████║   ██║
   ╚═╝      ╚═════╝ ╚═╝  ╚═══╝╚═╝  ╚═══╝   ╚═╝
        FunnyLang v{version} — it compiles. somehow.
"""


def banner() -> str:
    return BANNER.format(version=__version__)


def _ensure_utf8_stdio() -> None:
    """FunnyLang is full of emoji. Windows consoles default to cp1252 and
    will crash on them unless we force UTF-8 on the standard streams."""
    for stream_name in ("stdout", "stderr"):
        stream = getattr(sys, stream_name, None)
        reconfigure = getattr(stream, "reconfigure", None)
        if callable(reconfigure):
            try:
                reconfigure(encoding="utf-8", errors="replace")
            except (ValueError, OSError):
                pass


def cmd_run(path: str) -> int:
    try:
        with open(path, encoding="utf-8") as f:
            text = f.read()
    except OSError as exc:
        print(f"couldn't read '{path}': {exc}", file=sys.stderr)
        return 1
    source = SourceFile(path, text)
    try:
        program = parse_source(source)
        resolved = resolve_program(program, source)
        unit = Compiler(resolved, source).compile_program(program, path)
        vm = VM()
        install_stdlib(vm)
        vm.interpret(unit, source)
    except ParseErrorBundle as bundle:
        print(render_parse_error_bundle(bundle), file=sys.stderr, end="")
        return 1
    except ComputerExploded as err:
        print(render_diagnostic(err), file=sys.stderr, end="")
        return 69
    except FunnyError as err:
        print(render_diagnostic(err), file=sys.stderr, end="")
        return 1
    return 0


def main(argv: list[str] | None = None) -> int:
    _ensure_utf8_stdio()
    if argv is None:
        argv = sys.argv[1:]
    if argv and argv[0] == "run":
        if len(argv) < 2:
            print("funny run needs a file to run.", file=sys.stderr)
            return 1
        return cmd_run(argv[1])
    print(banner())
    return 0


if __name__ == "__main__":
    sys.exit(main())
