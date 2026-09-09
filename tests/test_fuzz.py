"""PLAN.md §M11 task 3: throw garbage and near-garbage at every stage and
assert nothing but a FunnyError-family exception ever escapes."""
from __future__ import annotations

import random

import pytest

from funnylang.compiler import Compiler
from funnylang.errors import FunnyError, ParseErrorBundle
from funnylang.lexer import Lexer
from funnylang.parser import parse_source
from funnylang.resolver import resolve_program
from funnylang.source import SourceFile

pytestmark = pytest.mark.slow

# A representative pool of lexemes covering every token category, used for
# unstructured "token soup" fuzzing (mostly exercises the lexer + parser's
# error recovery, since most combinations are syntactically nonsense).
_SOUP_TOKENS = [
    "yo", "deadass", "bet", "lowkey", "bounce", "sus", "kinda_sus", "nah",
    "bruh", "grind", "from", "to", "step", "in", "bail", "nvm", "sketchy",
    "my_bad", "regardless", "chuck", "gimme", "as", "flex", "squad",
    "inherits", "me", "og", "spawn", "fax", "cap", "ghost", "fr", "orr",
    "aint", "same_energy", "diff_energy", "vibe", "yap", "yeet", "mumble",
    "(", ")", "[", "]", "{", "}", ",", ".", ";", ":", "=", "==", "!=", "+",
    "-", "*", "/", "\\", "%", "**", "<", "<=", ">", ">=", "&&", "||", "!",
    "?", "??", "?.", "|>", "=>", "...", "x", "y", "foo", "_bar", "42",
    "3.14", '"str"', "`t{x}`", "\n", "//comment\n",
]


def _random_soup(rng: random.Random, n_tokens: int) -> str:
    return " ".join(rng.choice(_SOUP_TOKENS) for _ in range(n_tokens))


def test_random_token_soups_never_crash_lexer_or_parser():
    rng = random.Random(12345)
    for i in range(2000):
        src = _random_soup(rng, rng.randint(1, 25))
        try:
            source = SourceFile(f"<fuzz{i}>", src)
            program = parse_source(source)
            resolved = resolve_program(program, source)
            Compiler(resolved, source).compile_program(program, f"<fuzz{i}>")
        except (ParseErrorBundle, FunnyError):
            pass  # expected: garbage input should fail cleanly
        except Exception as exc:  # pragma: no cover - this is the bug we're hunting
            pytest.fail(f"non-FunnyError escaped for input {src!r}: {type(exc).__name__}: {exc}")


# -- grammar-aware random programs (exercise resolver/compiler more deeply) --

_NAMES = ["a", "b", "c", "x", "y", "n", "total", "value"]
_LITERALS = ["0", "1", "-1", "3.14", '"hi"', "fax", "cap", "ghost", "[1, 2]", '{"k": 1}']
_BINOPS = ["+", "-", "*", "/", "%", "==", "<", ">", "&&", "||", "??"]


def _rexpr(rng: random.Random, depth: int) -> str:
    if depth <= 0 or rng.random() < 0.4:
        return rng.choice(_LITERALS + _NAMES)
    choice = rng.random()
    if choice < 0.5:
        return f"({_rexpr(rng, depth - 1)} {rng.choice(_BINOPS)} {_rexpr(rng, depth - 1)})"
    if choice < 0.7:
        return f"{rng.choice(_NAMES)}({_rexpr(rng, depth - 1)})"
    if choice < 0.85:
        return f"{_rexpr(rng, depth - 1)}[{_rexpr(rng, depth - 1)}]"
    return f"({_rexpr(rng, depth - 1)} ? {_rexpr(rng, depth - 1)} : {_rexpr(rng, depth - 1)})"


def _rstmt(rng: random.Random, depth: int) -> str:
    name = rng.choice(_NAMES)
    choice = rng.random()
    if choice < 0.3:
        return f"yo {name} = {_rexpr(rng, 3)}"
    if choice < 0.45:
        return f"{name} = {_rexpr(rng, 3)}"
    if choice < 0.6:
        return f"yap {_rexpr(rng, 2)}"
    if choice < 0.7 and depth > 0:
        return f"sus ({_rexpr(rng, 2)}) {{\n{_rblock(rng, depth - 1)}\n}}"
    if choice < 0.8 and depth > 0:
        return f"bruh ({_rexpr(rng, 2)}) {{\n{_rblock(rng, depth - 1)}\n}}"
    if choice < 0.9 and depth > 0:
        return f"grind {name} from 0 to 5 {{\n{_rblock(rng, depth - 1)}\n}}"
    return f"bounce {_rexpr(rng, 2)}" if rng.random() < 0.5 else f"chuck {_rexpr(rng, 2)}"


def _rblock(rng: random.Random, depth: int) -> str:
    return "\n".join(_rstmt(rng, depth) for _ in range(rng.randint(0, 4)))


def _random_program(rng: random.Random) -> str:
    parts = [_rstmt(rng, 2) for _ in range(rng.randint(1, 8))]
    if rng.random() < 0.5:
        params = ", ".join(rng.sample(_NAMES, k=rng.randint(0, 3)))
        parts.append(f"bet f({params}) {{\n{_rblock(rng, 2)}\n}}")
    return "\n".join(parts) + "\n"


def test_random_grammar_aware_programs_never_crash_pipeline():
    rng = random.Random(54321)
    for i in range(500):
        src = _random_program(rng)
        try:
            source = SourceFile(f"<fuzzprog{i}>", src)
            program = parse_source(source)
            resolved = resolve_program(program, source)
            Compiler(resolved, source).compile_program(program, f"<fuzzprog{i}>")
        except (ParseErrorBundle, FunnyError):
            pass
        except Exception as exc:  # pragma: no cover - this is the bug we're hunting
            pytest.fail(f"non-FunnyError escaped for program:\n{src}\n\n{type(exc).__name__}: {exc}")
