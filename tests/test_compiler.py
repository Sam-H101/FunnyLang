from __future__ import annotations

import pytest

from conftest import compile_prog, entry_proto, op_sequence
from funnylang.chunk import TAG_BOOL, TAG_FLOAT, TAG_GHOST, TAG_INT, TAG_PROTO_REF, TAG_STRING
from funnylang.opcodes import Op


def test_hello_world_opcodes():
    unit = compile_prog('yo greeting = "yo sup world"\nyap greeting\n')
    assert op_sequence(unit) == ["CONST", "DEF_GLOBAL", "GET_GLOBAL", "YAP", "GHOST", "RETURN"]


def test_literal_ghost_fax_cap_use_dedicated_opcodes():
    unit = compile_prog("yap ghost\nyap fax\nyap cap\n")
    ops = op_sequence(unit)
    assert "GHOST" in ops and "FAX" in ops and "CAP" in ops
    # none of these should have gone through the constant pool
    assert all(tag not in (TAG_GHOST, TAG_BOOL) for tag, _ in unit.const_pool.entries)


def test_int_and_float_are_distinct_constants():
    unit = compile_prog("yap 1\nyap 1.0\n", fold_constants=False)
    tags_values = unit.const_pool.entries
    assert (TAG_INT, 1) in tags_values
    assert (TAG_FLOAT, 1.0) in tags_values


def test_constant_pool_dedupes_same_typed_value():
    unit = compile_prog('yap "hi"\nyap "hi"\n')
    count = sum(1 for tag, v in unit.const_pool.entries if tag == TAG_STRING and v == "hi")
    assert count == 1


def test_constant_folding_folds_literal_arithmetic():
    unit = compile_prog("yap 2 + 3 * 4\n")
    ops = op_sequence(unit)
    assert ops.count("CONST") == 1  # folded down to a single 14
    assert (TAG_INT, 14) in unit.const_pool.entries


def test_constant_folding_disabled():
    unit = compile_prog("yap 2 + 3\n", fold_constants=False)
    ops = op_sequence(unit)
    assert ops.count("CONST") == 2
    assert "ADD" in ops


def test_folding_skips_division_by_zero():
    # must not raise at compile time; division by zero is a *runtime* error
    unit = compile_prog("yap 1 / 0\n")
    assert "DIV" in op_sequence(unit)


def test_binary_op_maps_to_opcode():
    cases = {
        "+": Op.ADD, "-": Op.SUB, "*": Op.MUL, "/": Op.DIV, "%": Op.MOD,
        "**": Op.POW, "==": Op.EQ, "!=": Op.NEQ, "<": Op.LT, "<=": Op.LE,
        ">": Op.GT, ">=": Op.GE, "&": Op.BAND, "|": Op.BOR, "^": Op.BXOR,
        "<<": Op.SHL, ">>": Op.SHR,
    }
    for src_op, opcode in cases.items():
        unit = compile_prog(f"yo a = 1\nyo b = 2\nyap a {src_op} b\n")
        assert opcode.name in op_sequence(unit), f"{src_op} -> {opcode.name}"


def test_floor_division_backslash_maps_to_idiv():
    unit = compile_prog("yo a = 1\nyo b = 2\nyap a \\ b\n")
    assert "IDIV" in op_sequence(unit)


def test_unary_ops():
    unit = compile_prog("yo x = 1\nyap -x\nyap !x\nyap ~x\n")
    ops = op_sequence(unit)
    assert "NEG" in ops and "NOT" in ops and "BNOT" in ops


def test_logical_and_uses_jump_if_false_keep():
    unit = compile_prog("yo a = fax\nyo b = cap\nyap a && b\n")
    ops = op_sequence(unit)
    assert "JUMP_IF_FALSE_KEEP" in ops


def test_logical_or_uses_jump_if_true_keep():
    unit = compile_prog("yo a = fax\nyo b = cap\nyap a || b\n")
    ops = op_sequence(unit)
    assert "JUMP_IF_TRUE_KEEP" in ops


def test_coalesce_uses_jump_if_ghost_keep():
    unit = compile_prog("yo a = ghost\nyap a ?? 5\n")
    assert "JUMP_IF_GHOST_KEEP" in op_sequence(unit)


def test_safe_get_uses_jump_if_ghost_keep():
    unit = compile_prog("yo a = ghost\nyap a?.x\n")
    assert "JUMP_IF_GHOST_KEEP" in op_sequence(unit)


def test_ternary_compiles_both_branches():
    unit = compile_prog("yap fax ? 1 : 2\n")
    ops = op_sequence(unit)
    assert "JUMP_IF_FALSE" in ops and "JUMP" in ops


def test_if_elif_else_compiles():
    src = 'yo x = 5\nsus (x > 10) {\n yap "big"\n} kinda_sus (x > 3) {\n yap "mid"\n} nah {\n yap "small"\n}\n'
    unit = compile_prog(src)
    ops = op_sequence(unit)
    assert ops.count("JUMP_IF_FALSE") == 2
    assert ops.count("JUMP") >= 2


def test_while_loop_uses_loop_opcode():
    unit = compile_prog("yo x = 0\nbruh (x < 10) {\n x += 1\n}\n")
    assert "LOOP" in op_sequence(unit)


def test_for_range_uses_loop_and_get_local():
    unit = compile_prog("grind i from 0 to 10 {\n yap i\n}\n")
    ops = op_sequence(unit)
    assert "LOOP" in ops and "GET_LOCAL" in ops


def test_for_each_uses_iter_opcodes():
    unit = compile_prog('grind item in ["a", "b"] {\n yap item\n}\n')
    ops = op_sequence(unit)
    assert "ITER_NEW" in ops and "ITER_NEXT" in ops


def test_break_and_continue_compile_to_jumps():
    src = "bruh (fax) {\n sus (fax) { bail }\n sus (cap) { nvm }\n}\n"
    unit = compile_prog(src)
    ops = op_sequence(unit)
    assert ops.count("JUMP") >= 2


def test_function_decl_produces_closure_and_separate_proto():
    unit = compile_prog("bet add(a, b) {\n bounce a + b\n}\nyap add(1, 2)\n")
    assert len(unit.protos) == 2
    add_proto = next(p for p in unit.protos if p.name == "add")
    assert add_proto.arity == 2


def test_call_via_get_uses_invoke():
    unit = compile_prog("gimme mafs\nyap mafs.sqrt(16)\n")
    assert "INVOKE" in op_sequence(unit)


def test_plain_call_uses_call_opcode():
    unit = compile_prog("bet f() { bounce 1 }\nyap f()\n")
    ops = op_sequence(unit)
    assert "CALL" in ops
    assert "INVOKE" not in ops


def test_closure_captures_upvalue_via_get_upval():
    src = "bet counter() {\n yo n = 0\n bounce lowkey () => { n += 1\n bounce n\n }\n}\n"
    unit = compile_prog(src)
    lam_proto = next(p for p in unit.protos if p.name == "<lowkey>")
    assert lam_proto.upvalue_count == 1
    assert "GET_UPVAL" in op_sequence(unit, lam_proto)
    assert "SET_UPVAL" in op_sequence(unit, lam_proto)


def test_default_param_prologue_checks_ghost():
    unit = compile_prog('bet greet(name, greeting = "yo") {\n bounce greeting\n}\n')
    proto = next(p for p in unit.protos if p.name == "greet")
    ops = op_sequence(unit, proto)
    assert ops[:4] == ["GET_LOCAL", "GHOST", "EQ", "JUMP_IF_FALSE"]
    assert proto.default_count == 1


def test_variadic_function_metadata():
    unit = compile_prog("bet f(...rest) {\n bounce rest\n}\n")
    proto = next(p for p in unit.protos if p.name == "f")
    assert proto.is_variadic is True
    assert proto.arity == 0


def test_try_catch_finally_uses_try_push_pop():
    src = 'sketchy {\n chuck "x"\n} my_bad (e) {\n yap e\n} regardless {\n yap "done"\n}\n'
    unit = compile_prog(src)
    ops = op_sequence(unit)
    assert "TRY_PUSH" in ops and "TRY_POP" in ops and "CHUCK" in ops


def test_regardless_body_is_compiled_twice():
    # once inline (normal path) and once for the exceptional/unwind path,
    # which re-CHUCKs — PLAN.md §M5's explicit requirement.
    unit = compile_prog('sketchy {\n yap "try"\n} regardless {\n yap "cleanup-marker"\n}\n')
    marker_count = sum(
        1 for tag, v in unit.const_pool.entries if tag == TAG_STRING and v == "cleanup-marker"
    )
    assert marker_count == 1  # constant is deduped even though the code using it isn't
    ops = op_sequence(unit)
    assert ops.count("YAP") == 3  # try, cleanup (normal), cleanup (exceptional)
    assert "CHUCK" in ops


def test_try_with_catch_and_finally_wraps_catch_in_inner_protection():
    # if the catch body itself throws, `regardless` must still run before
    # the new exception propagates further.
    unit = compile_prog(
        'sketchy {\n chuck "a"\n} my_bad (e) {\n chuck "b"\n} regardless {\n yap "cleanup"\n}\n'
    )
    ops = op_sequence(unit)
    assert ops.count("TRY_PUSH") == 2
    assert ops.count("TRY_POP") == 2


def test_try_finally_no_catch_still_protects_and_rethrows():
    unit = compile_prog('sketchy {\n yap 1\n} regardless {\n yap "cleanup"\n}\n')
    ops = op_sequence(unit)
    assert ops.count("YAP") == 3
    assert "CHUCK" in ops


def test_stash_and_groupchat_literals():
    unit = compile_prog('yap [1, 2, 3]\nyap {"a": 1}\n')
    ops = op_sequence(unit)
    assert "BUILD_STASH" in ops and "BUILD_GROUPCHAT" in ops


def test_template_string_uses_build_string():
    unit = compile_prog('yo name = "sam"\nyap `hi {name}`\n')
    assert "BUILD_STRING" in op_sequence(unit)


def test_pipe_prepends_argument():
    unit = compile_prog("bet add(a, b) { bounce a + b }\nyap 1 |> add(2)\n")
    ops = op_sequence(unit)
    assert "CALL" in ops


def test_index_and_slice():
    unit = compile_prog("yo a = [1,2,3]\nyap a[0]\nyap a[1:2]\n")
    ops = op_sequence(unit)
    assert "GET_INDEX" in ops and "GET_SLICE" in ops


def test_compound_assign_index():
    unit = compile_prog("yo a = [1,2,3]\na[0] += 1\n")
    ops = op_sequence(unit)
    assert "GET_INDEX" in ops and "SET_INDEX" in ops and "ADD" in ops


def test_compound_assign_property():
    unit = compile_prog("gimme mafs\nyo x = mafs\n")  # placeholder-safe compile check
    # a real property compound-assign needs an instance (M9); sanity-check Set alone:
    unit2 = compile_prog("bet f(obj) {\n obj.n += 1\n}\n")
    ops = op_sequence(unit2, next(p for p in unit2.protos if p.name == "f"))
    assert "GET_PROP" in ops and "SET_PROP" in ops and "DUP" in ops


def test_export_emits_export_opcode():
    unit = compile_prog("flex deadass PI = 3\n")
    assert "EXPORT" in op_sequence(unit)


def test_import_stdlib_emits_import_opcode():
    unit = compile_prog("gimme mafs\n")
    ops = op_sequence(unit)
    assert "IMPORT" in ops and "DEF_GLOBAL" in ops


def test_named_import_binds_each_name():
    unit = compile_prog('gimme { double, TAU } from "mathstuff.funny"\n')
    ops = op_sequence(unit)
    assert ops.count("DEF_GLOBAL") == 2
    assert ops.count("GET_PROP") == 2
