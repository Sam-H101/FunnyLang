"""The `funny` command-line interface (PLAN.md §M8)."""
from __future__ import annotations

import argparse
import io
import os
import random
import sys
import time
from pathlib import Path

from . import BYTECODE_VERSION, __version__
from .ast_nodes import ExprStmt, dump_ast
from .compiler import Compiler
from .disasm import disassemble, disassemble_proto
from .errors import (
    ComputerExploded, FunnyError, ParseErrorBundle, render_diagnostic,
    render_parse_error_bundle,
)
from .formatter import format_program
from .lexer import Lexer
from .modules import CanonicalSource, build_bundle, make_pak_module_loader
from .parser import parse_source
from .resolver import BUILTIN_GLOBAL_NAMES, STDLIB_MODULE_NAMES, Resolver, resolve_program
from .serializer import dump_funnyc, dump_funnypak, load_funnyc, load_funnypak
from .source import SourceFile
from .stdlib import install_stdlib
from .tokens import TokenKind as TK
from .values import to_repr
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

VIBES_QUIPS = [
    "cooking...", "no cap, almost there...", "vibing through the bytecode...",
    "asking the compiler nicely...", "channeling big brain energy...",
    "it's giving compiler...", "manifesting correct syntax...",
    "lowkey grinding...", "bet.", "sending it...", "one sec, fr fr...",
    "doing the most (the necessary amount)...", "skill issue prevention in progress...",
    "yeeting bytes around...", "trust the process...",
]

HELP_TEXT = """\
funny vibe — the REPL.
  .help          show this
  .exit          leave (or Ctrl-D)
  .clear         reset the session (fresh globals)
  .xray <expr>   disassemble an expression
  .time <expr>   time how long an expression takes to run
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


def _quip(vibes: bool, phase: str) -> None:
    if vibes:
        print(f"[{phase}] {random.choice(VIBES_QUIPS)}")


def _read_text(path: str) -> str:
    with open(path, encoding="utf-8") as f:
        return f.read()


# ---------------------------------------------------------------------------
# run
# ---------------------------------------------------------------------------


def cmd_run(path: str, extra_args: list[str], *, color: bool = True, vibes: bool = False, show_time: bool = False) -> int:
    ext = Path(path).suffix
    vm = VM()
    install_stdlib(vm)
    vm.program_args = list(extra_args)  # the_args() wraps this in a Stash
    t0 = time.perf_counter()
    try:
        if ext == ".funnyc":
            unit = load_funnyc(Path(path).read_bytes())
            source = None
        elif ext == ".funnypak":
            modules, entry_name = load_funnypak(Path(path).read_bytes())
            vm.module_loader = make_pak_module_loader(modules, entry_name)
            unit = modules[entry_name]
            source = CanonicalSource(entry_name)
        else:
            text = _read_text(path)
            _quip(vibes, "lexing")
            source = SourceFile(path, text)
            program = parse_source(source)
            _quip(vibes, "resolving")
            resolved = resolve_program(program, source)
            _quip(vibes, "compiling")
            unit = Compiler(resolved, source).compile_program(program, path)
        t_compile = time.perf_counter() - t0
        _quip(vibes, "running")
        t1 = time.perf_counter()
        vm.interpret(unit, source)
        t_run = time.perf_counter() - t1
    except ParseErrorBundle as bundle:
        print(render_parse_error_bundle(bundle, color=color), file=sys.stderr, end="")
        return 1
    except ComputerExploded as err:
        print(render_diagnostic(err, color=color), file=sys.stderr, end="")
        return 69
    except FunnyError as err:
        print(render_diagnostic(err, color=color), file=sys.stderr, end="")
        return 1
    except OSError as exc:
        print(f"couldn't read '{path}': {exc}", file=sys.stderr)
        return 1
    if show_time:
        print(f"compiled in {t_compile * 1000:.0f}ms, ran in {t_run * 1000:.0f}ms. blazingly fast (probably)")
    return 0


# ---------------------------------------------------------------------------
# build
# ---------------------------------------------------------------------------


def cmd_build(path: str, out: str, *, vibes: bool = False) -> int:
    _quip(vibes, "compiling")
    try:
        units, entry_canonical = build_bundle(path)
    except ParseErrorBundle as bundle:
        print(render_parse_error_bundle(bundle), file=sys.stderr, end="")
        return 1
    except FunnyError as err:
        print(render_diagnostic(err), file=sys.stderr, end="")
        return 1
    _quip(vibes, "linking")
    if len(units) == 1 and out.endswith(".funnyc"):
        data = dump_funnyc(units[entry_canonical])
    else:
        data = dump_funnypak(units, entry_canonical)
    Path(out).write_bytes(data)
    print(f"built {out} ({len(data)} bytes, {len(units)} module{'s' if len(units) != 1 else ''}).")
    return 0


# ---------------------------------------------------------------------------
# xray
# ---------------------------------------------------------------------------


def cmd_xray(path: str, *, tokens: bool = False, ast: bool = False, pak: bool = False) -> int:
    if pak or path.endswith(".funnypak"):
        modules, entry_name = load_funnypak(Path(path).read_bytes())
        print(f"entry: {entry_name}")
        for name, unit in modules.items():
            marker = " (entry)" if name == entry_name else ""
            print(f"=== module: {name}{marker} ===")
            print(disassemble(unit))
        return 0
    if path.endswith(".funnyc"):
        print(disassemble(load_funnyc(Path(path).read_bytes())))
        return 0
    try:
        text = _read_text(path)
    except OSError as exc:
        print(f"couldn't read '{path}': {exc}", file=sys.stderr)
        return 1
    source = SourceFile(path, text)
    try:
        if tokens:
            for tok in Lexer(source).tokenize():
                print(tok)
            return 0
        program = parse_source(source)
        if ast:
            print(dump_ast(program))
            return 0
        resolved = resolve_program(program, source)
        unit = Compiler(resolved, source).compile_program(program, path)
        print(disassemble(unit))
    except ParseErrorBundle as bundle:
        print(render_parse_error_bundle(bundle), file=sys.stderr, end="")
        return 1
    except FunnyError as err:
        print(render_diagnostic(err), file=sys.stderr, end="")
        return 1
    return 0


# ---------------------------------------------------------------------------
# fmt
# ---------------------------------------------------------------------------


def cmd_fmt(path: str, *, check: bool = False) -> int:
    try:
        text = _read_text(path)
    except OSError as exc:
        print(f"couldn't read '{path}': {exc}", file=sys.stderr)
        return 1
    source = SourceFile(path, text)
    try:
        program = parse_source(source)
    except ParseErrorBundle as bundle:
        print(render_parse_error_bundle(bundle), file=sys.stderr, end="")
        return 1
    except FunnyError as err:
        print(render_diagnostic(err), file=sys.stderr, end="")
        return 1
    formatted = format_program(program)
    if check:
        if formatted == text:
            return 0
        print(f"{path} isn't formatted. run 'funny fmt {path}'.", file=sys.stderr)
        return 1
    if formatted != text:
        with open(path, "w", encoding="utf-8", newline="\n") as f:
            f.write(formatted)
        print(f"formatted {path}.")
    return 0


# ---------------------------------------------------------------------------
# test
# ---------------------------------------------------------------------------


def _run_one_test(funny_path: Path) -> tuple[bool, str]:

    src = funny_path.read_text(encoding="utf-8")
    expected = funny_path.with_suffix(".expected").read_text(encoding="utf-8")
    source = SourceFile(str(funny_path), src)
    if expected.startswith("!ERROR"):
        wanted_flavor = expected.splitlines()[0].split(maxsplit=1)[1].strip()
        vm = VM(stdout=io.StringIO())
        install_stdlib(vm)
        try:
            program = parse_source(source)
            resolved = resolve_program(program, source)
            unit = Compiler(resolved, source).compile_program(program, str(funny_path))
            vm.interpret(unit, source)
        except SystemExit:
            # `dip(n)` inside a test is that *test* exiting, not the runner.
            # Left uncaught it takes the whole `funny test` process with it,
            # so one test could silently truncate the run.
            pass
        except ParseErrorBundle as bundle:
            got = bundle.errors[0].flavor if bundle.errors else "?"
            return got == wanted_flavor, f"expected !ERROR {wanted_flavor}, got {got}"
        except FunnyError as err:
            return err.flavor == wanted_flavor, f"expected !ERROR {wanted_flavor}, got {err.flavor}"
        return False, f"expected !ERROR {wanted_flavor}, but nothing was raised"
    vm = VM(stdout=io.StringIO())
    install_stdlib(vm)
    try:
        program = parse_source(source)
        resolved = resolve_program(program, source)
        unit = Compiler(resolved, source).compile_program(program, str(funny_path))
        vm.interpret(unit, source)
    except SystemExit:
        pass  # see above: the test exited, the runner did not
    except (ParseErrorBundle, FunnyError) as exc:
        return False, f"unexpected error: {exc}"
    actual = vm.stdout.getvalue()
    return actual == expected, "" if actual == expected else f"expected {expected!r}, got {actual!r}"


def cmd_test(dir_path: str) -> int:
    base = Path(dir_path)
    files = sorted(p for p in base.rglob("*.funny") if p.with_suffix(".expected").exists())
    if not files:
        print(f"no *.funny/*.expected pairs found under {dir_path}.")
        return 0
    passed = failed = 0
    for f in files:
        try:
            ok, detail = _run_one_test(f)
        except Exception as exc:  # a genuine harness bug, not a language error
            ok, detail = False, f"harness error: {exc}"
        if ok:
            passed += 1
            print(f"PASS {f}")
        else:
            failed += 1
            print(f"FAIL {f} — {detail}")
    total = passed + failed
    print(f"\n{passed}/{total} passed.")
    return 0 if failed == 0 else 1


# ---------------------------------------------------------------------------
# vibe (the REPL)
# ---------------------------------------------------------------------------


def _needs_continuation(text: str) -> bool:
    try:
        tokens = Lexer(SourceFile("<vibe>", text)).tokenize()
    except Exception:
        return True
    depth = 0
    for tok in tokens:
        if tok.kind in (TK.LPAREN, TK.LBRACKET, TK.LBRACE):
            depth += 1
        elif tok.kind in (TK.RPAREN, TK.RBRACKET, TK.RBRACE):
            depth -= 1
    return depth > 0


def _repl_xray(expr_text: str) -> None:
    source = SourceFile("<xray>", expr_text)
    try:
        program = parse_source(source)
        resolved = resolve_program(program, source)
        unit = Compiler(resolved, source).compile_program(program, "<xray>")
        print(disassemble(unit))
    except ParseErrorBundle as bundle:
        print(render_parse_error_bundle(bundle), end="")
    except FunnyError as err:
        print(render_diagnostic(err), end="")


def cmd_vibe(*, color: bool = True) -> int:

    try:
        import readline  # noqa: F401  (enables history/line-editing when available)
    except ImportError:
        pass

    vm = VM()
    install_stdlib(vm)
    known_globals = set(BUILTIN_GLOBAL_NAMES) | set(STDLIB_MODULE_NAMES)
    const_globals: set[str] = set()
    module_globals: dict = {}
    module_exports: dict = {}

    print(banner())
    print("type .help for help, .exit to leave.")
    buffer_lines: list[str] = []
    while True:
        prompt = "funny> " if not buffer_lines else ".....> "
        try:
            line = input(prompt)
        except EOFError:
            print()
            break
        except KeyboardInterrupt:
            print()
            buffer_lines = []
            continue

        if not buffer_lines:
            stripped = line.strip()
            if stripped in (".exit", ".quit"):
                break
            if stripped == ".help":
                print(HELP_TEXT)
                continue
            if stripped == ".clear":
                known_globals = set(BUILTIN_GLOBAL_NAMES) | set(STDLIB_MODULE_NAMES)
                const_globals = set()
                module_globals = {}
                module_exports = {}
                print("session cleared.")
                continue
            if stripped.startswith(".xray "):
                _repl_xray(stripped[len(".xray "):])
                continue
            if stripped.startswith(".time "):
                line = stripped[len(".time "):]
                is_timing = True
            else:
                is_timing = False
        else:
            is_timing = False

        was_continuing = len(buffer_lines) > 0
        buffer_lines.append(line)
        buffer_text = "\n".join(buffer_lines)
        if not buffer_text.strip():
            buffer_lines = []
            continue
        # A blank line while continuing forces an attempt instead of waiting
        # forever — some inputs (an unterminated single-line string, say)
        # can never become balanced no matter how much more you add.
        if not line.strip() and was_continuing:
            pass
        elif _needs_continuation(buffer_text):
            continue
        buffer_lines = []

        source = SourceFile("<vibe>", buffer_text)
        try:
            program = parse_source(source)
        except ParseErrorBundle as bundle:
            print(render_parse_error_bundle(bundle, color=color), end="")
            continue
        except FunnyError as err:
            print(render_diagnostic(err, color=color), end="")
            continue

        is_expr = bool(program.statements) and isinstance(program.statements[-1], ExprStmt)
        try:
            resolver = Resolver(source, known_globals, const_globals)
            resolved = resolver.resolve(program)
            unit = Compiler(resolved, source).compile_program(program, "<vibe>", repl_capture_last=is_expr)
            start = time.perf_counter() if is_timing else None
            value = vm.run_repl_unit(unit, source, module_globals, module_exports)
            if is_timing:
                print(f"ran in {(time.perf_counter() - start) * 1000:.2f}ms")
        except ParseErrorBundle as bundle:
            print(render_parse_error_bundle(bundle, color=color), end="")
            continue
        except FunnyError as err:
            print(render_diagnostic(err, color=color), end="")
            continue
        if is_expr:
            print(to_repr(value, vm))
    return 0


# ---------------------------------------------------------------------------
# stubs for later milestones
# ---------------------------------------------------------------------------


def cmd_yeet(args) -> int:
    from . import packager

    out = args.out or (str(Path(args.file).with_suffix(".exe" if sys.platform == "win32" else "")))
    try:
        units, entry_canonical = build_bundle(args.file)
    except ParseErrorBundle as bundle:
        print(render_parse_error_bundle(bundle), file=sys.stderr, end="")
        return 1
    except FunnyError as err:
        print(render_diagnostic(err), file=sys.stderr, end="")
        return 1
    pak_bytes = dump_funnypak(units, entry_canonical)
    try:
        size = packager.yeet(
            pak_bytes, out,
            icon=args.icon,
            rebuild_stub=args.rebuild_stub,
        )
    except packager.StubBuildError as exc:
        print(f"couldn't build the stub: {exc}", file=sys.stderr)
        return 1
    mb = size / (1024 * 1024)
    print(f"yeeted {mb:.1f} MB of pure comedy into {out}. it runs anywhere. no python. no cap.")
    return 0


def _bootstrap_run_stage(compiler_path, in_path, out_path, selfhost_dir) -> tuple[bool, str]:
    """Runs the compiler at `compiler_path` (a .funnypak for stage2, a bare
    .funnyc for stage3+) with args [in_path, out_path] — exactly what
    `funny run compiler_path -- in_path out_path` does, just in-process so
    the whole bootstrap runs as one `funny bootstrap` call. `funnypath`
    makes a bare .funnyc's own `gimme {...} from "compiler.funny"` (it has
    no embedded source path to resolve relative to) find its siblings in
    selfhost/ regardless of where the compiled artifact itself lives."""
    from .modules import ModuleResolver

    vm = VM()
    install_stdlib(vm)
    vm.module_resolver = ModuleResolver(vm, funnypath=str(selfhost_dir))
    vm.program_args = [str(in_path), str(out_path)]
    data = Path(compiler_path).read_bytes()
    try:
        if str(compiler_path).endswith(".funnypak"):
            modules, entry_name = load_funnypak(data)
            vm.module_loader = make_pak_module_loader(modules, entry_name)
            unit = modules[entry_name]
            source = CanonicalSource(entry_name)
        else:
            unit = load_funnyc(data)
            source = None
        vm.interpret(unit, source)
    except FunnyError as err:
        return False, render_diagnostic(err)
    return True, ""


def _bootstrap_diff(stage3_path, stage4_path) -> str:
    """Disassembles the first proto where stage3 and stage4 diverge and
    prints both versions, for `--diff`."""
    unit3 = load_funnyc(Path(stage3_path).read_bytes())
    unit4 = load_funnyc(Path(stage4_path).read_bytes())
    if len(unit3.protos) != len(unit4.protos):
        return f"proto count differs: stage3 has {len(unit3.protos)}, stage4 has {len(unit4.protos)}."
    for i, (p3, p4) in enumerate(zip(unit3.protos, unit4.protos)):
        if p3 != p4:
            label = f"proto #{i} ({p3.name})"
            out = [f"first divergent proto: {label}", "", "-- stage3 --", disassemble_proto(unit3, p3, label)]
            out += ["", "-- stage4 --", disassemble_proto(unit4, p4, label)]
            return "\n".join(out)
    return "protos are equal but the raw bytes differ (const pool or header mismatch)."


def cmd_bootstrap(args) -> int:
    verify = getattr(args, "verify", False)
    keep = getattr(args, "keep", False)
    show_diff = getattr(args, "diff", False)
    if not verify:
        print("funny bootstrap needs --verify.", file=sys.stderr)
        return 1

    selfhost_dir = Path(__file__).resolve().parent.parent / "selfhost"
    funnyc_path = selfhost_dir / "funnyc.funny"

    tmp_ctx = None
    if keep:
        build_dir = Path("build") / "bootstrap"
        build_dir.mkdir(parents=True, exist_ok=True)
    else:
        import tempfile

        tmp_ctx = tempfile.TemporaryDirectory(prefix="funny-bootstrap-")
        build_dir = Path(tmp_ctx.name)

    try:
        try:
            units, entry_canonical = build_bundle(str(funnyc_path))
        except ParseErrorBundle as bundle:
            print(render_parse_error_bundle(bundle), file=sys.stderr, end="")
            return 1
        except FunnyError as err:
            print(render_diagnostic(err), file=sys.stderr, end="")
            return 1
        stage2_path = build_dir / "stage2.funnypak"
        stage2_path.write_bytes(dump_funnypak(units, entry_canonical))
        print("🥁 stage 2... compiled.")

        stage3_path = build_dir / "stage3.funnyc"
        ok, err_text = _bootstrap_run_stage(stage2_path, funnyc_path, stage3_path, selfhost_dir)
        if not ok:
            print(f"stage 2 -> stage 3 failed:\n{err_text}", file=sys.stderr)
            return 1
        print("🥁 stage 3... compiled by stage 2.")

        stage4_path = build_dir / "stage4.funnyc"
        ok, err_text = _bootstrap_run_stage(stage3_path, funnyc_path, stage4_path, selfhost_dir)
        if not ok:
            print(f"stage 3 -> stage 4 failed:\n{err_text}", file=sys.stderr)
            return 1
        print("🥁 stage 4... compiled by stage 3.")
        print()

        stage3_bytes = stage3_path.read_bytes()
        stage4_bytes = stage4_path.read_bytes()
        if stage3_bytes == stage4_bytes:
            print(f"stage3 and stage4 are byte-identical ({len(stage3_bytes):,} bytes).")
            print("FunnyLang now compiles FunnyLang. we are so back. 🏆")
            return 0

        print(f"stage3 ({len(stage3_bytes)} bytes) and stage4 ({len(stage4_bytes)} bytes) differ.", file=sys.stderr)
        if show_diff:
            print(_bootstrap_diff(stage3_path, stage4_path), file=sys.stderr)
        return 1
    finally:
        if tmp_ctx is not None:
            tmp_ctx.cleanup()


# ---------------------------------------------------------------------------
# argument parsing / dispatch
# ---------------------------------------------------------------------------


def _add_global_flags(p: argparse.ArgumentParser) -> None:
    # default=SUPPRESS: these are defined on *both* the top-level parser and
    # every subcommand parser, so `funny --time run x` and `funny run x
    # --time` both work. With SUPPRESS, a subparser that didn't see the flag
    # leaves the namespace alone instead of stomping a True the top-level
    # parser already set back to its own default of False.
    p.add_argument("--serious", action="store_true", default=argparse.SUPPRESS, help="plain professional error text, no roasts")
    p.add_argument("--no-color", action="store_true", default=argparse.SUPPRESS, help="disable ANSI color in diagnostics")
    p.add_argument("--time", action="store_true", default=argparse.SUPPRESS, help="print how long compiling/running took")
    p.add_argument("--vibes", action="store_true", default=argparse.SUPPRESS, help="verbose: print a loading quip per phase")


def _build_arg_parser() -> argparse.ArgumentParser:
    common = argparse.ArgumentParser(add_help=False)
    _add_global_flags(common)

    parser = argparse.ArgumentParser(prog="funny", add_help=True)
    _add_global_flags(parser)
    parser.add_argument("--version", action="store_true", help="print the version and exit")
    sub = parser.add_subparsers(dest="command")

    p_run = sub.add_parser("run", parents=[common], help="run a .funny/.funnyc/.funnypak file")
    p_run.add_argument("file")

    p_build = sub.add_parser("build", parents=[common], help="compile (+ link) to .funnyc/.funnypak")
    p_build.add_argument("file")
    p_build.add_argument("-o", "--out", required=True)

    p_yeet = sub.add_parser("yeet", parents=[common], help="compile to a native executable (M10)")
    p_yeet.add_argument("file")
    p_yeet.add_argument("-o", "--out")
    p_yeet.add_argument("--icon")
    p_yeet.add_argument("--console", action="store_true")
    p_yeet.add_argument("--no-console", action="store_true")
    p_yeet.add_argument("--keep-stub", action="store_true")
    p_yeet.add_argument("--rebuild-stub", action="store_true")

    sub.add_parser("vibe", parents=[common], help="the REPL")

    p_xray = sub.add_parser("xray", parents=[common], help="disassemble/inspect a file")
    p_xray.add_argument("file")
    p_xray.add_argument("--tokens", action="store_true")
    p_xray.add_argument("--ast", action="store_true")
    p_xray.add_argument("--pak", action="store_true")

    p_fmt = sub.add_parser("fmt", parents=[common], help="canonical formatter")
    p_fmt.add_argument("file")
    p_fmt.add_argument("--check", action="store_true")

    p_test = sub.add_parser("test", parents=[common], help="run *.funny/*.expected pairs in a directory")
    p_test.add_argument("dir")

    p_bootstrap = sub.add_parser("bootstrap", parents=[common], help="self-host verification (M12)")
    p_bootstrap.add_argument("--verify", action="store_true")
    p_bootstrap.add_argument("--keep", action="store_true")
    p_bootstrap.add_argument("--diff", action="store_true")

    return parser


def main(argv: list[str] | None = None) -> int:
    """The public entry point — never lets a Python traceback reach the
    user (PLAN.md §M11 rule 1). `SystemExit` (argparse's --help/usage-error
    exits) and `KeyboardInterrupt` pass straight through unmolested; anything
    else genuinely unexpected gets the "compiler skill issue" treatment with
    a real traceback attached, since at that point it's on us, not the user."""
    try:
        return _main_inner(argv)
    except (SystemExit, KeyboardInterrupt):
        raise
    except RecursionError:
        # Deep source nesting (or a truly pathological program) can exhaust
        # Python's own recursion limit during parsing/resolving/compiling —
        # a real limit, not a compiler bug, and the traceback for a
        # RecursionError is thousands of frames long, useless to print in
        # full. Give it the funny-taxonomy treatment instead of the raw dump.
        print("💀💀💀 FUNNYLANG MOMENT 💀💀💀", file=sys.stderr)
        print(file=sys.stderr)
        print("  TooDeepBro", file=sys.stderr)
        print(file=sys.stderr)
        print("  this nests so deep even the compiler touched grass.", file=sys.stderr)
        print("  break it up into smaller pieces.", file=sys.stderr)
        return 1
    except BaseException:
        import traceback

        print("☠️  COMPILER SKILL ISSUE  ☠️", file=sys.stderr)
        print("the compiler itself broke. that's on us, not you.", file=sys.stderr)
        print("please open an issue with this file and the goofy details below.", file=sys.stderr)
        print(file=sys.stderr)
        traceback.print_exc()
        return 70


def _main_inner(argv: list[str] | None = None) -> int:
    _ensure_utf8_stdio()
    if argv is None:
        argv = sys.argv[1:]

    extra_args: list[str] = []
    if "--" in argv:
        idx = argv.index("--")
        extra_args = argv[idx + 1:]
        argv = argv[:idx]

    parser = _build_arg_parser()
    args = parser.parse_args(argv)

    if args.version:
        print(f"funny {__version__} (bytecode v{BYTECODE_VERSION})")
        return 0
    if not args.command:
        print(banner())
        return 0

    if getattr(args, "serious", False):
        os.environ["FUNNY_SERIOUS"] = "1"
    # None means "auto-detect from isatty()" (render_diagnostic's default);
    # only --no-color should force it off. Passing True here unconditionally
    # would print raw ANSI escapes into every redirected/piped output.
    color = False if getattr(args, "no_color", False) else None
    vibes = getattr(args, "vibes", False)
    show_time = getattr(args, "time", False)

    if args.command == "run":
        return cmd_run(args.file, extra_args, color=color, vibes=vibes, show_time=show_time)
    if args.command == "build":
        return cmd_build(args.file, args.out, vibes=vibes)
    if args.command == "yeet":
        return cmd_yeet(args)
    if args.command == "vibe":
        return cmd_vibe(color=color)
    if args.command == "xray":
        return cmd_xray(args.file, tokens=args.tokens, ast=args.ast, pak=args.pak)
    if args.command == "fmt":
        return cmd_fmt(args.file, check=args.check)
    if args.command == "test":
        return cmd_test(args.dir)
    if args.command == "bootstrap":
        return cmd_bootstrap(args)

    parser.print_help()  # pragma: no cover - argparse already validates `command`
    return 1


if __name__ == "__main__":
    sys.exit(main())
