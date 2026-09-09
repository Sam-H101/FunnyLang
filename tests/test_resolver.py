from __future__ import annotations

import pytest

from conftest import resolve_prog
from funnylang.errors import ImmutableVibes, ParserHadAStroke, WhoDis


def test_local_variable_resolves_to_local():
    prog, result = resolve_prog("bet f() {\n yo x = 1\n bounce x\n}")
    func = prog.statements[0]
    ret = func.body.statements[1]
    res = result.identifier_resolutions[id(ret.value)]
    assert res.kind == "local"
    assert res.index == 0


def test_toplevel_variable_resolves_to_global():
    prog, result = resolve_prog("yo x = 1\nyap x")
    yap_stmt = prog.statements[1]
    res = result.identifier_resolutions[id(yap_stmt.args[0])]
    assert res.kind == "global"
    assert res.name == "x"


def test_parameter_resolves_to_local_slot():
    prog, result = resolve_prog("bet f(a, b) {\n bounce a + b\n}")
    func = prog.statements[0]
    binary = func.body.statements[0].value
    res_a = result.identifier_resolutions[id(binary.left)]
    res_b = result.identifier_resolutions[id(binary.right)]
    assert (res_a.kind, res_a.index) == ("local", 0)
    assert (res_b.kind, res_b.index) == ("local", 1)


def test_closure_captures_upvalue():
    src = "bet counter() {\n yo n = 0\n bounce lowkey () => { n += 1\n bounce n\n }\n}"
    prog, result = resolve_prog(src)
    func = prog.statements[0]
    lam = func.body.statements[1].value
    assign_expr = lam.body.statements[0].expr
    res = result.identifier_resolutions[id(assign_expr.target)]
    assert res.kind == "upvalue"
    func_info = result.func_info[id(func)]
    assert func_info.captured_slots == {0}
    lam_info = result.func_info[id(lam)]
    assert len(lam_info.upvalues) == 1
    assert lam_info.upvalues[0].is_local is True
    assert lam_info.upvalues[0].index == 0


def test_nested_closure_chains_upvalue():
    src = (
        "bet outer() {\n"
        "    yo x = 1\n"
        "    bet middle() {\n"
        "        bounce lowkey () => x\n"
        "    }\n"
        "    bounce middle\n"
        "}\n"
    )
    prog, result = resolve_prog(src)
    outer = prog.statements[0]
    middle = outer.body.statements[1]
    lam = middle.body.statements[0].value
    res = result.identifier_resolutions[id(lam.body)]
    assert res.kind == "upvalue"
    # middle itself must have captured x as an upvalue-of-an-upvalue
    middle_info = result.func_info[id(middle)]
    assert middle_info.upvalues[0].is_local is True
    lam_info = result.func_info[id(lam)]
    assert lam_info.upvalues[0].is_local is False


def test_undefined_variable_raises_whodis():
    with pytest.raises(WhoDis):
        resolve_prog("yap totally_undefined_name")


def test_whodis_suggests_close_match():
    with pytest.raises(WhoDis) as excinfo:
        resolve_prog("yo greeting = 1\nyap greetng\n")
    assert "greeting" in excinfo.value.roast


def test_assigning_deadass_local_is_immutable_vibes():
    with pytest.raises(ImmutableVibes):
        resolve_prog("bet f() {\n deadass X = 1\n X = 2\n}")


def test_assigning_deadass_global_is_immutable_vibes():
    with pytest.raises(ImmutableVibes):
        resolve_prog("deadass PI = 3.14\nPI = 4\n")


def test_reassigning_plain_yo_is_fine():
    resolve_prog("yo x = 1\nx = 2\n")  # should not raise


def test_shadowing_in_same_scope_is_error():
    with pytest.raises(ParserHadAStroke):
        resolve_prog("bet f() {\n yo x = 1\n yo x = 2\n}")


def test_shadowing_parameter_with_local_is_error():
    with pytest.raises(ParserHadAStroke):
        resolve_prog("bet f(x) {\n yo x = 2\n}")


def test_shadowing_in_nested_block_is_allowed():
    # a new block is a new scope, so this must NOT raise
    resolve_prog("bet f() {\n yo x = 1\n sus (fax) {\n yo x = 2\n}\n}")


def test_bail_outside_loop_is_error():
    with pytest.raises(ParserHadAStroke):
        resolve_prog("bet f() {\n bail\n}")


def test_nvm_outside_loop_is_error():
    with pytest.raises(ParserHadAStroke):
        resolve_prog("bet f() {\n nvm\n}")


def test_bail_inside_loop_is_fine():
    resolve_prog("bet f() {\n bruh (fax) {\n bail\n}\n}")


def test_bail_inside_nested_if_in_loop_is_fine():
    resolve_prog("bet f() {\n bruh (fax) {\n sus (fax) { bail }\n}\n}")


def test_bounce_at_top_level_is_error():
    with pytest.raises(ParserHadAStroke):
        resolve_prog("bounce 1")


def test_me_outside_squad_is_error():
    with pytest.raises(ParserHadAStroke):
        resolve_prog("yap me")


def test_og_outside_squad_is_error():
    with pytest.raises(ParserHadAStroke):
        resolve_prog("og.speak()")


def test_toplevel_mutual_recursion_resolves():
    src = (
        'bet is_even(n) {\n'
        '    sus (n same_energy 0) { bounce fax }\n'
        '    bounce is_odd(n - 1)\n'
        '}\n'
        'bet is_odd(n) {\n'
        '    sus (n same_energy 0) { bounce cap }\n'
        '    bounce is_even(n - 1)\n'
        '}\n'
    )
    resolve_prog(src)  # should not raise


def test_forrange_loop_variable_is_local_and_scoped():
    prog, result = resolve_prog("bet f() {\n grind i from 0 to 10 {\n yap i\n}\n}")
    func = prog.statements[0]
    loop = func.body.statements[0]
    yap_stmt = loop.body.statements[0]
    res = result.identifier_resolutions[id(yap_stmt.args[0])]
    assert res.kind == "local"


def test_foreach_loop_variable_is_local():
    prog, result = resolve_prog('bet f() {\n grind item in ["a"] {\n yap item\n}\n}')
    func = prog.statements[0]
    loop = func.body.statements[0]
    yap_stmt = loop.body.statements[0]
    res = result.identifier_resolutions[id(yap_stmt.args[0])]
    assert res.kind == "local"


def test_loop_variable_not_visible_after_loop():
    with pytest.raises(WhoDis):
        resolve_prog("bet f() {\n grind i from 0 to 10 { }\n yap i\n}")


def test_try_catch_variable_is_local_and_scoped():
    prog, result = resolve_prog(
        'bet f() {\n sketchy {\n chuck "x"\n} my_bad (e) {\n yap e\n}\n}'
    )
    func = prog.statements[0]
    try_stmt = func.body.statements[0]
    yap_stmt = try_stmt.catch_body.statements[0]
    res = result.identifier_resolutions[id(yap_stmt.args[0])]
    assert res.kind == "local"


def test_catch_variable_not_visible_after_try():
    with pytest.raises(WhoDis):
        resolve_prog('bet f() {\n sketchy {\n chuck "x"\n} my_bad (e) { }\n yap e\n}')


def test_known_builtin_resolves_as_global():
    prog, result = resolve_prog("yap how_thicc([1,2,3])")
    yap_stmt = prog.statements[0]
    call = yap_stmt.args[0]
    res = result.identifier_resolutions[id(call.callee)]
    assert res.kind == "global"


def test_stdlib_module_import_resolves():
    prog, result = resolve_prog("gimme mafs\nyap mafs.sqrt(16)")
    yap_stmt = prog.statements[1]
    get_expr = yap_stmt.args[0].callee  # Get(mafs, "sqrt")
    res = result.identifier_resolutions[id(get_expr.obj)]
    assert res.kind == "global"
    assert res.name == "mafs"


def test_named_import_binds_names_directly():
    prog, result = resolve_prog('gimme { double, TAU } from "mathstuff.funny"\nyap double(TAU)')
    resolve_prog('gimme { double, TAU } from "mathstuff.funny"\nyap double(TAU)')  # no raise


def test_local_function_recursion_via_upvalue():
    src = (
        "bet outer() {\n"
        "    bet fact(n) {\n"
        "        sus (n < 2) { bounce 1 }\n"
        "        bounce n * fact(n - 1)\n"
        "    }\n"
        "    bounce fact(5)\n"
        "}\n"
    )
    prog, result = resolve_prog(src)
    outer = prog.statements[0]
    fact = outer.body.statements[0]
    mul = fact.body.statements[1].value  # n * fact(n - 1)
    call = mul.right
    res = result.identifier_resolutions[id(call.callee)]
    assert res.kind == "upvalue"
