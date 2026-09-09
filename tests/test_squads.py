from __future__ import annotations

import pytest

from conftest import compile_prog, expect_error, op_sequence, run_funny
from funnylang.errors import GhostError, TypeVibeMismatch, WhoDis


def test_spawn_sets_fields():
    src = "squad P { spawn(x, y) { me.x = x\n me.y = y } }\nyo p = P(1, 2)\nyap p.x\nyap p.y\n"
    assert run_funny(src) == "1\n2\n"


def test_method_call():
    src = "squad P { spawn(x) { me.x = x }\n bet get() { bounce me.x } }\nyap P(42).get()\n"
    assert run_funny(src) == "42\n"


def test_inheritance_field_and_method_visible():
    src = (
        "squad A { spawn(x) { me.x = x } }\n"
        "squad B inherits A { }\n"
        "yo b = B(5)\n"
        "yap b.x\n"
    )
    assert run_funny(src) == "5\n"


def test_method_override_wins_over_parent():
    src = (
        "squad A { bet f() { bounce 1 } }\n"
        "squad B inherits A { bet f() { bounce 2 } }\n"
        "yap B().f()\n"
    )
    assert run_funny(src) == "2\n"


def test_super_call_reaches_parent_method():
    src = (
        "squad A { bet f() { bounce 1 } }\n"
        "squad B inherits A { bet f() { bounce og.f() + 10 } }\n"
        "yap B().f()\n"
    )
    assert run_funny(src) == "11\n"


def test_three_level_super_chain_no_infinite_recursion():
    src = (
        "squad A { bet f() { bounce 1 } }\n"
        "squad B inherits A { bet f() { bounce og.f() + 1 } }\n"
        "squad C inherits B { bet f() { bounce og.f() + 1 } }\n"
        "yap C().f()\n"
    )
    assert run_funny(src) == "3\n"


def test_unset_field_returns_ghost():
    src = "squad A { spawn() { } }\nyo a = A()\nyap a.nope\n"
    assert run_funny(src) == "ghost\n"


def test_undefined_method_raises_whodis_with_exact_roast():
    err = expect_error("squad A { spawn() { } }\nyo a = A()\na.nope()\n")
    assert isinstance(err, WhoDis)
    assert "doesn't do `nope`. that's not its thing." in err.roast


def test_setting_field_on_non_instance_raises_type_mismatch():
    err = expect_error("yo x = 5\nx.field = 1\n")
    assert isinstance(err, TypeVibeMismatch)


def test_me_outside_squad_still_rejected():
    from funnylang.errors import ParserHadAStroke
    err = expect_error("yap me\n")
    assert isinstance(err, ParserHadAStroke)


def test_og_outside_squad_still_rejected():
    from funnylang.errors import ParserHadAStroke
    err = expect_error("og.speak()\n")
    assert isinstance(err, ParserHadAStroke)


def test_to_yap_magic_method_used_by_yap():
    src = 'squad V { spawn(n) { me.n = n }\n bet to_yap() { bounce `V({me.n})` } }\nyap V(7)\n'
    assert run_funny(src) == "V(7)\n"


def test_default_display_when_no_to_yap():
    src = "squad Plain { spawn() { } }\nyap Plain()\n"
    out = run_funny(src)
    assert out == "<Plain instance>\n"


def test_how_thicc_magic_method():
    src = "squad Bag { spawn() { me.items = [1,2,3,4,5] }\n bet how_thicc() { bounce me.items.how_thicc() } }\nyap how_thicc(Bag())\n"
    assert run_funny(src) == "5\n"


def test_same_energy_magic_method_used_by_equality_operator():
    src = (
        "squad P { spawn(v) { me.v = v }\n bet same_energy(other) { bounce me.v same_energy other.v } }\n"
        "yap P(1) same_energy P(1)\n"
        "yap P(1) same_energy P(2)\n"
    )
    assert run_funny(src) == "fax\ncap\n"


def test_default_identity_equality_without_same_energy():
    src = "squad P { spawn() { } }\nyo a = P()\nyo b = P()\nyap a same_energy a\nyap a same_energy b\n"
    assert run_funny(src) == "fax\ncap\n"


def test_get_it_set_it_magic_methods():
    src = (
        'squad R { spawn() { me.d = {} }\n bet get_it(k) { bounce me.d.get(k, "none") }\n bet set_it(k, v) { me.d.set(k, v) } }\n'
        "yo r = R()\n"
        'r["x"] = 99\n'
        'yap r["x"]\n'
        'yap r["y"]\n'
    )
    assert run_funny(src) == "99\nnone\n"


def test_methods_are_first_class_values():
    src = "squad C { spawn() { me.n = 0 }\n bet bump() { me.n += 1\n bounce me.n } }\nyo c = C()\nyo b = c.bump\nyap b()\nyap b()\n"
    assert run_funny(src) == "1\n2\n"


def test_spawn_with_variadic():
    src = "squad Bag { spawn(...items) { me.items = items } }\nyo b = Bag(1, 2, 3)\nyap b.items\n"
    assert run_funny(src) == "[1, 2, 3]\n"


def test_spawn_with_default_param():
    src = 'squad Greeter { spawn(name, greeting = "yo") { me.msg = `{greeting} {name}` } }\nyap Greeter("sam").msg\n'
    assert run_funny(src) == "yo sam\n"


def test_no_spawn_still_constructs():
    src = "squad Empty { }\nyo e = Empty()\nyap what_is_it(e)\n"
    assert run_funny(src) == "Empty\n"


def test_squad_compiles_to_squad_method_inherit_opcodes():
    unit = compile_prog("squad A { spawn() { } }\nsquad B inherits A { bet f() { bounce 1 } }\n")
    ops = op_sequence(unit)
    assert "SQUAD" in ops and "METHOD" in ops and "INHERIT" in ops


def test_call_via_get_on_instance_uses_invoke():
    unit = compile_prog("squad A { bet f() { bounce 1 } }\nyap A().f()\n")
    assert "INVOKE" in op_sequence(unit)


def test_super_call_uses_invoke_og():
    unit = compile_prog("squad A { bet f() { bounce 1 } }\nsquad B inherits A { bet f() { bounce og.f() } }\n")
    proto = next(p for p in unit.protos if p.name == "f" and p.upvalue_count == 0)
    all_ops = [op for p in unit.protos for op in op_sequence(unit, p)]
    assert "INVOKE_OG" in all_ops


def test_exported_squad():
    src = 'flex squad Point { spawn(x) { me.x = x } }\nyap Point(5).x\n'
    assert run_funny(src) == "5\n"


def test_squad_in_conditional_expression_position():
    # regression check for the `sus`-as-keyword-vs-module ambiguity note in
    # PLAN.md §16 — squads/classes must be unaffected by that fix.
    src = "squad A { bet f() { bounce fax } }\nsus (A().f()) { yap \"yes\" } nah { yap \"no\" }\n"
    assert run_funny(src) == "yes\n"
