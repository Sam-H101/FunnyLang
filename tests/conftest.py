"""Shared pytest fixtures/helpers for the FunnyLang test suite."""
from __future__ import annotations

import io
import os
import platform
import subprocess
from pathlib import Path

import pytest

from funnylang.ast_nodes import Program, dump_ast
from funnylang.chunk import CompiledUnit
from funnylang.compiler import Compiler
from funnylang.errors import FunnyError
from funnylang.lexer import Lexer
from funnylang.parser import parse_source
from funnylang.resolver import resolve_program
from funnylang.source import SourceFile
from funnylang.tokens import Token, TokenKind
from funnylang.vm import VM


def pytest_sessionfinish(session, exitstatus):
    # An empty test suite (M0, before M1 lands) should not fail CI.
    if exitstatus == 5:  # NO_TESTS_COLLECTED
        session.exitstatus = 0


_REPO_ROOT = Path(__file__).resolve().parent.parent


@pytest.fixture(scope="session")
def native_binary(tmp_path_factory):
    """Builds `native/` the way build.sh does and returns the binary's path.

    Lives in this shared conftest rather than a `tests/native/` one because
    two suites under tests/native/ need the same binary -- the differential
    suite and N5b's live-HTTPS gate -- and a second conftest.py in that
    subdirectory would shadow *this* module for the bare `import conftest`
    that eleven other test files already do.

    Skips rather than fails when there's no C toolchain: windows-latest CI
    runners have no `cc` on PATH without an explicit setup step, unlike the
    `native` ci.yml job.
    """
    out = tmp_path_factory.mktemp("native_build") / "funny_native_test"
    # native/ has two entry points; this fixture builds the CLI one, so
    # stub_main.c is left out or the link fails on a duplicate main().
    # (`native_stub_binary` below builds the other.)
    srcs = sorted(str(p) for p in (_REPO_ROOT / "native").glob("*.c") if p.name != "stub_main.c")
    cc = os.environ.get("CC", "cc")  # e.g. `CC=clang python3 -m pytest ...`
    cmd = [cc, "-std=c11", "-O2", "-Wall", "-Wextra", "-Werror", "-o", str(out), *srcs, "-lm"]
    # N5b: platform.c dlopen()s OpenSSL on Linux/BSD and uses Security.framework
    # on macOS -- mirrors build.sh's own link line.
    if platform.system() == "Darwin":
        cmd += ["-framework", "Security", "-framework", "CoreFoundation"]
    elif os.name != "nt":
        cmd += ["-ldl"]
    try:
        result = subprocess.run(cmd, capture_output=True, text=True)
    except OSError as exc:
        pytest.skip(f"no C compiler on PATH: {exc}")
    if result.returncode != 0:
        pytest.skip(f"no C toolchain available to build the native binary:\n{result.stdout}\n{result.stderr}")

    # NATIVE_PLAN.md N8 task 6: `funny` is now a loader — every subcommand
    # lives in selfhost/cli.funny, and the binary finds that bundle *beside
    # itself*. This fixture builds into a temp directory, so the sidecars
    # have to come along. Built fresh from selfhost/ rather than copied out
    # of bootstrap/, so a test never runs against a stale checked-in bundle.
    from funnylang.modules import build_bundle
    from funnylang.serializer import dump_funnypak

    sidecars = out.parent / "bootstrap"
    sidecars.mkdir(exist_ok=True)
    for entry, name in ((_REPO_ROOT / "selfhost" / "cli.funny", "cli.funnypak"),
                        (_REPO_ROOT / "selfhost" / "funnyc.funny", "funnyc.funnypak")):
        units, entry_canonical = build_bundle(str(entry))
        (sidecars / name).write_bytes(dump_funnypak(units, entry_canonical))
    return out


@pytest.fixture(scope="session")
def native_stub_binary(tmp_path_factory):
    """The yeet runtime stub (`funnyrt`): the same sources as `funny` but
    entered through stub_main.c instead of main.c, so it carries no compiler
    -- a shipped executable only ever runs already-compiled bytecode."""
    out = tmp_path_factory.mktemp("native_stub") / "funnyrt"
    srcs = sorted(str(p) for p in (_REPO_ROOT / "native").glob("*.c") if p.name != "main.c")
    cc = os.environ.get("CC", "cc")
    cmd = [cc, "-std=c11", "-O2", "-Wall", "-Wextra", "-Werror", "-o", str(out), *srcs, "-lm"]
    if platform.system() == "Darwin":
        cmd += ["-framework", "Security", "-framework", "CoreFoundation"]
    elif os.name != "nt":
        cmd += ["-ldl"]
    try:
        result = subprocess.run(cmd, capture_output=True, text=True)
    except OSError as exc:
        pytest.skip(f"no C compiler on PATH: {exc}")
    if result.returncode != 0:
        pytest.skip(f"no C toolchain available to build the stub:\n{result.stdout}\n{result.stderr}")
    return out


def make_source(src: str, path: str = "<test>") -> SourceFile:
    return SourceFile(path, src)


def lex(src: str, path: str = "<test>") -> list[Token]:
    """Tokenize `src` and return every token, EOF included."""
    return Lexer(make_source(src, path)).tokenize()


def lex_kinds(src: str) -> list[TokenKind]:
    """Tokenize `src` and return just the TokenKinds, EOF excluded — handy for
    compact assertions in tests."""
    return [t.kind for t in lex(src) if t.kind != TokenKind.EOF]


def parse_prog(src: str, path: str = "<test>") -> Program:
    return parse_source(make_source(src, path))


def dump_prog(src: str, path: str = "<test>") -> str:
    return dump_ast(parse_prog(src, path))


def parse_one(src: str):
    """Parse `src` as a program and return its single top-level statement's AST."""
    prog = parse_prog(src)
    assert len(prog.statements) == 1, f"expected exactly 1 statement, got {len(prog.statements)}"
    return prog.statements[0]


def resolve_prog(src: str, path: str = "<test>"):
    source = make_source(src, path)
    prog = parse_source(source)
    result = resolve_program(prog, source)
    return prog, result


def compile_prog(src: str, path: str = "<test>", fold_constants: bool = True) -> CompiledUnit:
    source = make_source(src, path)
    prog = parse_source(source)
    result = resolve_program(prog, source)
    return Compiler(result, source, fold_constants=fold_constants).compile_program(prog, path)


def run_funny(src: str, path: str = "<test>", fold_constants: bool = True) -> str:
    """Compile and run `src` in-process, returning captured stdout."""
    from funnylang.stdlib import install_stdlib

    unit = compile_prog(src, path, fold_constants=fold_constants)
    vm = VM(stdout=io.StringIO())
    install_stdlib(vm)
    vm.interpret(unit, make_source(src, path))
    return vm.stdout.getvalue()


def expect_error(src: str, path: str = "<test>") -> FunnyError:
    """Run `src` and return the FunnyError it raises, failing the test if
    it doesn't raise one. A ParseErrorBundle's first error is unwrapped —
    most callers just want to check the flavor of the first syntax error."""
    from funnylang.errors import ParseErrorBundle

    try:
        run_funny(src, path)
    except ParseErrorBundle as bundle:
        return bundle.errors[0]
    except FunnyError as exc:
        return exc
    raise AssertionError(f"expected a FunnyError, but this ran clean:\n{src}")


def entry_proto(unit: CompiledUnit):
    return unit.protos[unit.entry_proto]


def op_sequence(unit: CompiledUnit, proto=None):
    """The list of opcode names (no operands) in a proto's code, in order —
    for compact assertions in compiler tests."""
    from funnylang.opcodes import OPERANDS, Op

    proto = proto if proto is not None else entry_proto(unit)
    names = []
    code = proto.code
    ip = 0
    while ip < len(code):
        op = Op(code[ip])
        names.append(op.name)
        if op == Op.CLOSURE:
            const_idx = int.from_bytes(code[ip + 1:ip + 3], "big")
            tag, ref = unit.const_pool.entries[const_idx]
            n_upvals = unit.protos[ref].upvalue_count if tag == 5 else 0
            ip = ip + 3 + 2 * n_upvals
        else:
            ip += 1 + sum(OPERANDS[op])
    return names
