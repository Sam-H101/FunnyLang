from __future__ import annotations

from pathlib import Path

import pytest

from conftest import expect_error, run_funny
from funnylang.errors import (
    GhostError, ImmutableVibes, KeyGhosted, MathAintMathin, NotACallableRizz,
    OutOfPocket, TooDeepBro, TypeVibeMismatch, WhoDis, WrongNumberOfHomies,
)

LANG_DIR = Path(__file__).parent / "lang"


def _discover_lang_tests():
    return sorted(LANG_DIR.glob("*.funny"))


@pytest.mark.parametrize("path", _discover_lang_tests(), ids=lambda p: p.stem)
def test_lang_golden(path: Path):
    src = path.read_text(encoding="utf-8")
    expected = path.with_suffix(".expected").read_text(encoding="utf-8")
    if expected.startswith("!ERROR"):
        flavor = expected.splitlines()[0].split(maxsplit=1)[1].strip()
        err = expect_error(src, str(path))
        assert err.flavor == flavor
    else:
        actual = run_funny(src, str(path))
        assert actual == expected


def test_at_least_30_lang_programs_exist():
    assert len(_discover_lang_tests()) >= 30


# -- direct VM unit tests --------------------------------------------------


def test_arithmetic_precedence():
    assert run_funny("yap 1 + 2 * 3\n") == "7\n"


def test_int_float_promotion():
    assert run_funny("yap 1 + 1.5\n") == "2.5\n"


def test_division_always_float():
    assert run_funny("yap 4 / 2\n") == "2.0\n"


def test_floor_division():
    assert run_funny("yap 7 \\ 2\n") == "3\n"


def test_modulo():
    assert run_funny("yap 7 % 3\n") == "1\n"


def test_power_right_assoc_runtime():
    assert run_funny("yap 2 ** 3 ** 2\n") == "512\n"


def test_string_concat():
    assert run_funny('yap "a" + "b"\n') == "ab\n"


def test_string_repeat():
    assert run_funny('yap "ha" * 3\n') == "hahaha\n"


def test_stash_concat():
    assert run_funny("yap [1,2] + [3,4]\n") == "[1, 2, 3, 4]\n"


def test_stash_repeat():
    assert run_funny("yap [1,2] * 2\n") == "[1, 2, 1, 2]\n"


def test_truthiness_falsy_values():
    src = 'grind v in [0, 0.0, "", cap, ghost] {\n sus (v) { yap "truthy" } nah { yap "falsy" }\n}\n'
    assert run_funny(src) == "falsy\nfalsy\nfalsy\nfalsy\nfalsy\n"


def test_truthiness_stash_and_groupchat():
    src = 'sus ([]) { yap "t" } nah { yap "f" }\nsus ([1]) { yap "t" } nah { yap "f" }\n'
    assert run_funny(src) == "f\nt\n"


def test_equality_numeric_cross_type():
    assert run_funny("yap 1 same_energy 1.0\n") == "fax\n"


def test_equality_bool_vs_int_are_distinct():
    assert run_funny("yap fax same_energy 1\n") == "cap\n"


def test_equality_stash_structural():
    assert run_funny("yap [1, 2] same_energy [1, 2]\n") == "fax\n"


def test_logical_short_circuit_and():
    src = "bet boom() { chuck \"should not run\" }\nyap cap && boom()\n"
    assert run_funny(src) == "cap\n"


def test_logical_short_circuit_or():
    src = "bet boom() { chuck \"should not run\" }\nyap fax || boom()\n"
    assert run_funny(src) == "fax\n"


def test_bitwise_ops():
    assert run_funny("yap 6 & 3\n") == "2\n"
    assert run_funny("yap 6 | 1\n") == "7\n"
    assert run_funny("yap 5 ^ 1\n") == "4\n"
    assert run_funny("yap 1 << 4\n") == "16\n"
    assert run_funny("yap 16 >> 2\n") == "4\n"


def test_ternary():
    assert run_funny('yap fax ? "yes" : "no"\n') == "yes\n"


def test_coalesce():
    assert run_funny("yap ghost ?? 5\n") == "5\n"
    assert run_funny("yap 1 ?? 5\n") == "1\n"


def test_safe_nav_on_ghost():
    assert run_funny("yo x = ghost\nyap x?.name\n") == "ghost\n"


def test_pipe_prepends_arg():
    assert run_funny("bet add(a, b) { bounce a + b }\nyap 1 |> add(2)\n") == "3\n"


def test_pipe_chain():
    src = "bet inc(x) { bounce x + 1 }\nbet double(x) { bounce x * 2 }\nyap 3 |> inc |> double\n"
    assert run_funny(src) == "8\n"


def test_slicing():
    assert run_funny('yap "hello"[1:3]\n') == "el\n"
    assert run_funny("yap [1,2,3,4,5][::-1]\n") == "[5, 4, 3, 2, 1]\n"


def test_negative_index():
    assert run_funny("yap [1,2,3][-1]\n") == "3\n"


def test_template_string():
    assert run_funny('yo n = "sam"\nyap `hi {n}, {1+1}`\n') == "hi sam, 2\n"


def test_closures_share_upvalue():
    src = "bet counter() {\n yo n = 0\n bounce lowkey () => { n += 1\n bounce n }\n}\nyo c = counter()\nyap c()\nyap c()\nyap c()\n"
    assert run_funny(src) == "1\n2\n3\n"


def test_closures_are_independent_per_call():
    src = "bet counter() {\n yo n = 0\n bounce lowkey () => { n += 1\n bounce n }\n}\nyo a = counter()\nyo b = counter()\nyap a()\nyap a()\nyap b()\n"
    assert run_funny(src) == "1\n2\n1\n"


def test_recursion_fib():
    src = "bet fib(n) {\n sus (n < 2) { bounce n }\n bounce fib(n-1) + fib(n-2)\n}\nyap fib(10)\n"
    assert run_funny(src) == "55\n"


def test_break_continue_nested_loops():
    src = (
        "grind i from 0 to 3 {\n"
        "    grind j from 0 to 3 {\n"
        "        sus (j == 1) { nvm }\n"
        "        sus (j == 2) { bail }\n"
        "        yap i, j\n"
        "    }\n"
        "}\n"
    )
    assert run_funny(src) == "0 0\n1 0\n2 0\n"


def test_shadowing_in_nested_block():
    src = "yo x = 1\nsus (fax) {\n yo x = 2\n yap x\n}\nyap x\n"
    assert run_funny(src) == "2\n1\n"


def test_variadic_collects_extra_args():
    src = "bet sum_all(...rest) {\n yo total = 0\n grind n in rest { total += n }\n bounce total\n}\nyap sum_all(1,2,3,4)\n"
    assert run_funny(src) == "10\n"


def test_default_param_used_when_omitted():
    assert run_funny('bet greet(name, greeting = "yo") { bounce greeting }\nyap greet("sam")\n') == "yo\n"


def test_default_param_overridden():
    assert run_funny('bet greet(name, greeting = "yo") { bounce greeting }\nyap greet("sam", "hi")\n') == "hi\n"


def test_try_catch_runs_finally_after_catch():
    src = 'sketchy {\n chuck "boom"\n} my_bad (e) {\n yap "caught"\n} regardless {\n yap "cleanup"\n}\nyap "after"\n'
    assert run_funny(src) == "caught\ncleanup\nafter\n"


def test_try_no_exception_still_runs_finally():
    src = 'sketchy {\n yap "try"\n} regardless {\n yap "cleanup"\n}\n'
    assert run_funny(src) == "try\ncleanup\n"


def test_try_finally_runs_before_uncaught_propagates():
    src = 'sketchy {\n sketchy {\n chuck "inner"\n} regardless {\n yap "inner-cleanup"\n}\n} my_bad (e) {\n yap "outer caught:", e.message\n}\n'
    assert run_funny(src) == "inner-cleanup\nouter caught: inner\n"


def test_error_object_fields():
    src = 'sketchy {\n chuck "custom"\n} my_bad (e) {\n yap e.flavor\n yap e.message\n yap e.payload\n}\n'
    assert run_funny(src) == "SkillIssue\ncustom\ncustom\n"


def test_rechuck_preserves_flavor():
    src = (
        'sketchy {\n'
        '    sketchy {\n'
        '        chuck "x"\n'
        '    } my_bad (e) {\n'
        '        chuck e\n'
        '    }\n'
        '} my_bad (e2) {\n'
        '    yap e2.flavor, e2.message\n'
        '}\n'
    )
    assert run_funny(src) == "SkillIssue x\n"


# -- runtime errors ---------------------------------------------------------


def test_type_mismatch_add():
    err = expect_error('yap "x" + 1\n')
    assert isinstance(err, TypeVibeMismatch)


def test_division_by_zero():
    err = expect_error("yap 1 / 0\n")
    assert isinstance(err, MathAintMathin)


def test_index_out_of_range():
    err = expect_error("yap [1,2,3][10]\n")
    assert isinstance(err, OutOfPocket)


def test_key_missing():
    err = expect_error('yap {"a": 1}["b"]\n')
    assert isinstance(err, KeyGhosted)


def test_undefined_global_at_runtime():
    # resolver can't always prove a name is missing (imports could define it),
    # so some undefined-name cases only surface at runtime.
    err = expect_error("yap totally_not_a_thing\n")
    assert isinstance(err, WhoDis)


def test_wrong_arity_too_few():
    err = expect_error("bet f(a, b) { bounce a }\nyap f(1)\n")
    assert isinstance(err, WrongNumberOfHomies)


def test_wrong_arity_too_many():
    err = expect_error("bet f(a) { bounce a }\nyap f(1, 2, 3)\n")
    assert isinstance(err, WrongNumberOfHomies)


def test_not_callable():
    err = expect_error("yo x = 5\nyap x()\n")
    assert isinstance(err, NotACallableRizz)


def test_immutable_reassignment():
    err = expect_error("deadass PI = 3\nPI = 4\n")
    assert isinstance(err, ImmutableVibes)


def test_ghost_property_access():
    err = expect_error("yo x = ghost\nyap x.name\n")
    assert isinstance(err, GhostError)


def test_stack_overflow_recursion():
    err = expect_error("bet f() { bounce f() }\nf()\n")
    assert isinstance(err, TooDeepBro)
