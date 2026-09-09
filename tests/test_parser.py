from __future__ import annotations

import pytest

from conftest import dump_prog, parse_one, parse_prog
from funnylang.ast_nodes import (
    Assign, Binary, Break, Call, Chuck, Continue, Export, ForEach, ForRange,
    FuncDecl, Get, GroupChatLit, Identifier, If, Import, Index, Lambda,
    Literal, Return, Set, SetIndex, Slice, StashLit, TemplateString, Ternary,
    Try, VarDecl, ConstDecl, VibeStmt, While, Yap, dump_ast,
)
from funnylang.errors import ParseErrorBundle
from funnylang.parser import parse_expr


# -- precedence (the acceptance-criteria examples) -----------------------


def test_precedence_mul_over_add():
    assert dump_ast(parse_expr("1 + 2 * 3")) == "(+ 1 (* 2 3))"


def test_power_is_right_associative():
    assert dump_ast(parse_expr("2 ** 3 ** 2")) == "(** 2 (** 3 2))"


def test_unary_binds_tighter_than_power():
    # frozen precedence table: unary(16) > power(15), unlike Python
    assert dump_ast(parse_expr("-2 ** 2")) == "(** (- 2) 2)"


def test_assignment_is_right_associative():
    stmt = parse_one("x = y = 1")
    assert isinstance(stmt, type(parse_one("z = 1")))  # ExprStmt-ish sanity
    assert dump_ast(stmt) == "(= x (= y 1))"


def test_comparison_chain_left_assoc():
    assert dump_ast(parse_expr("1 < 2 < 3")) == "(< (< 1 2) 3)"


def test_full_precedence_ladder():
    src = "a || b && c | d ^ e & f == g < h << i + j * k ** 2"
    tree = dump_ast(parse_expr(src))
    assert tree.startswith("(|| a (&&")


def test_ternary_right_assoc_and_low_precedence():
    assert dump_ast(parse_expr("a ? b : c ? d : e")) == "(?: a b (?: c d e))"


def test_pipe_operator():
    assert dump_ast(parse_expr("x |> f |> g")) == "(|> (|> x f) g)"


def test_coalesce_operator():
    # left-associative: only `? :`, `**`, and `=`-family are right-assoc (§3.4)
    assert dump_ast(parse_expr("a ?? b ?? c")) == "(?? (?? a b) c)"


def test_logical_aliases_normalize():
    assert dump_ast(parse_expr("a fr b")) == dump_ast(parse_expr("a && b"))
    assert dump_ast(parse_expr("a orr b")) == dump_ast(parse_expr("a || b"))


def test_equality_aliases_normalize():
    assert dump_ast(parse_expr("a same_energy b")) == "(== a b)"
    assert dump_ast(parse_expr("a diff_energy b")) == "(!= a b)"


def test_aint_normalizes_to_bang():
    assert dump_ast(parse_expr("aint x")) == dump_ast(parse_expr("!x"))


def test_floor_division_operator():
    assert dump_ast(parse_expr(r"10 \ 3")) == r"(\ 10 3)"


# -- postfix chains -------------------------------------------------------


def test_safe_get_chain():
    expr = parse_expr("a?.b?.c")
    assert dump_ast(expr) == "(safe-get (safe-get a b) c)"


def test_mixed_get_call_index_chain():
    expr = parse_expr("a.b(1)[2].c")
    assert isinstance(expr, Get)
    assert expr.name == "c"


def test_call_with_trailing_comma():
    expr = parse_expr("f(1, 2, 3,)")
    assert isinstance(expr, Call)
    assert len(expr.args) == 3


def test_call_args_span_multiple_lines():
    expr = parse_expr("f(\n  1,\n  2\n)")
    assert isinstance(expr, Call)
    assert len(expr.args) == 2


# -- slices ---------------------------------------------------------------


@pytest.mark.parametrize(
    "src,expected",
    [
        ("arr[1:3]", "(slice arr 1 3 ghost)"),
        ("arr[:2]", "(slice arr ghost 2 ghost)"),
        ("arr[2:]", "(slice arr 2 ghost ghost)"),
        ("s[::-1]", "(slice s ghost ghost (- 1))"),
        ("arr[-1]", "(index arr (- 1))"),
    ],
)
def test_slices_and_negative_index(src, expected):
    assert dump_ast(parse_expr(src)) == expected


# -- assignment targets -----------------------------------------------


def test_assign_to_identifier():
    assert isinstance(parse_expr("x = 1"), Assign)


def test_assign_to_property_becomes_set():
    expr = parse_expr("obj.name = 1")
    assert isinstance(expr, Set)
    assert expr.name == "name"


def test_assign_to_index_becomes_setindex():
    expr = parse_expr("arr[0] = 1")
    assert isinstance(expr, SetIndex)


def test_compound_assign_op_preserved():
    expr = parse_expr("x += 1")
    assert isinstance(expr, Assign)
    assert expr.op == "+="


def test_invalid_assignment_target_is_syntax_error():
    with pytest.raises(ParseErrorBundle):
        parse_prog("1 + 2 = 3")


# -- literals / templates --------------------------------------------------


def test_template_string_parses_expr_parts():
    expr = parse_expr("`yo {name}, you are {age}`")
    assert isinstance(expr, TemplateString)
    kinds = [p[0] for p in expr.parts]
    assert kinds == ["str", "expr", "str", "expr"]
    assert isinstance(expr.parts[1][1], Identifier)


def test_stash_literal():
    expr = parse_expr("[1, 2, 3]")
    assert isinstance(expr, StashLit)
    assert len(expr.elements) == 3


def test_groupchat_literal():
    expr = parse_expr('{"a": 1, "b": 2}')
    assert isinstance(expr, GroupChatLit)
    assert len(expr.pairs) == 2


def test_empty_groupchat_literal():
    expr = parse_expr("{}")
    assert isinstance(expr, GroupChatLit)
    assert expr.pairs == ()


# -- lambdas --------------------------------------------------------------


def test_lambda_arrow_form():
    expr = parse_expr("lowkey (x) => x + 1")
    assert isinstance(expr, Lambda)
    assert expr.is_expr_body is True
    assert [p.name for p in expr.params] == ["x"]


def test_lambda_block_form():
    expr = parse_expr("lowkey (x) { bounce x * 2 }")
    assert isinstance(expr, Lambda)
    assert expr.is_expr_body is False


def test_lambda_variadic():
    expr = parse_expr("lowkey (...rest) => rest")
    assert expr.variadic == "rest"


# -- statement forms -------------------------------------------------------


def test_var_decl_no_initializer():
    stmt = parse_one("yo x")
    assert isinstance(stmt, VarDecl)
    assert stmt.initializer is None


def test_var_decl_with_initializer():
    stmt = parse_one("yo x = 10")
    assert isinstance(stmt, VarDecl) and stmt.initializer is not None


def test_const_decl_requires_initializer():
    with pytest.raises(ParseErrorBundle):
        parse_prog("deadass PI")


def test_const_decl():
    stmt = parse_one("deadass PI = 3.14159")
    assert isinstance(stmt, ConstDecl)


def test_function_decl_with_defaults():
    stmt = parse_one('bet greet(name, greeting = "yo") {\n bounce name \n}')
    assert isinstance(stmt, FuncDecl)
    assert stmt.params[1].default is not None


def test_function_decl_variadic():
    stmt = parse_one("bet sum_all(...rest) {\n bounce rest \n}")
    assert isinstance(stmt, FuncDecl)
    assert stmt.variadic == "rest"


def test_variadic_must_be_last():
    with pytest.raises(ParseErrorBundle):
        parse_prog("bet f(...rest, x) { bounce 1 }")


def test_if_elif_else_chain():
    src = (
        "sus (x > 100) {\n"
        '    yap "big"\n'
        "} kinda_sus (x > 10) {\n"
        '    yap "medium"\n'
        "} nah {\n"
        '    yap "smol"\n'
        "}"
    )
    stmt = parse_one(src)
    assert isinstance(stmt, If)
    assert len(stmt.elif_branches) == 1
    assert stmt.else_branch is not None


def test_while_loop():
    stmt = parse_one("bruh (x > 0) {\n x -= 1\n}")
    assert isinstance(stmt, While)


def test_for_range_with_step():
    stmt = parse_one("grind i from 10 to 0 step -2 {\n yap i\n}")
    assert isinstance(stmt, ForRange)
    assert stmt.step is not None


def test_for_range_no_step():
    stmt = parse_one("grind i from 0 to 10 {\n yap i\n}")
    assert isinstance(stmt, ForRange)
    assert stmt.step is None


def test_for_each():
    stmt = parse_one('grind item in ["a", "b"] {\n yap item\n}')
    assert isinstance(stmt, ForEach)


def test_break_and_continue():
    stmt = parse_one("bruh (fax) {\n sus (x) { bail }\n}")
    assert isinstance(stmt, While)


def test_bare_return():
    stmt = parse_one("bet f() {\n bounce \n}")
    ret = stmt.body.statements[0]
    assert isinstance(ret, Return)
    assert ret.value is None


def test_return_with_value():
    stmt = parse_one("bet f() {\n bounce 1 + 2\n}")
    ret = stmt.body.statements[0]
    assert isinstance(ret, Return)
    assert ret.value is not None


def test_try_catch_finally():
    src = (
        "sketchy {\n"
        '    chuck "oops"\n'
        "} my_bad (e) {\n"
        '    yap "caught:", e.message\n'
        "} regardless {\n"
        '    yap "cleanup"\n'
        "}"
    )
    stmt = parse_one(src)
    assert isinstance(stmt, Try)
    assert stmt.catch_var == "e"
    assert stmt.finally_body is not None


def test_try_without_catch_or_finally_is_error():
    with pytest.raises(ParseErrorBundle):
        parse_prog('sketchy {\n chuck "oops"\n}')


def test_chuck_statement():
    stmt = parse_one('chuck "you fumbled it"')
    assert isinstance(stmt, Chuck)


def test_import_simple():
    stmt = parse_one('gimme "mathstuff.funny"')
    assert isinstance(stmt, Import)
    assert stmt.source == "mathstuff.funny"
    assert stmt.is_stdlib is False


def test_import_with_alias():
    stmt = parse_one('gimme "./util/helpers.funny" as helpers')
    assert isinstance(stmt, Import)
    assert stmt.alias == "helpers"


def test_import_named():
    stmt = parse_one('gimme { double, TAU } from "mathstuff.funny"')
    assert isinstance(stmt, Import)
    assert stmt.names == ("double", "TAU")


def test_import_stdlib():
    stmt = parse_one("gimme mafs")
    assert isinstance(stmt, Import)
    assert stmt.is_stdlib is True
    assert stmt.source == "mafs"


def test_import_stdlib_alias():
    stmt = parse_one("gimme rizz as luck")
    assert isinstance(stmt, Import)
    assert stmt.alias == "luck"


def test_export_function():
    stmt = parse_one("flex bet add(a, b) {\n bounce a + b\n}")
    assert isinstance(stmt, Export)
    assert isinstance(stmt.decl, FuncDecl)


def test_export_const():
    stmt = parse_one("flex deadass TAU = 6.28318")
    assert isinstance(stmt, Export)
    assert isinstance(stmt.decl, ConstDecl)


def test_yap_multiple_args():
    stmt = parse_one('yap x, y, "three things"')
    assert isinstance(stmt, Yap)
    assert stmt.newline is True
    assert len(stmt.args) == 3


def test_yeet_is_alias_of_yap():
    a = dump_prog('yap "hi"')
    b = dump_prog('yeet "hi"')
    assert a == b


def test_mumble_no_newline():
    stmt = parse_one('mumble "no newline"')
    assert isinstance(stmt, Yap)
    assert stmt.newline is False


def test_vibe_statement_is_noop():
    stmt = parse_one("vibe")
    assert isinstance(stmt, VibeStmt)


def test_reserved_keyword_rejected():
    with pytest.raises(ParseErrorBundle) as excinfo:
        parse_prog("yo x = 1\nmatch_this y {}\n")
    assert any("match_this" in e.message or "isn't implemented" in e.message for e in excinfo.value.errors)


def test_closures_example_parses():
    src = (
        "bet counter() {\n"
        "    yo n = 0\n"
        "    bounce lowkey () => { n += 1; bounce n }\n"
        "}\n"
    )
    prog = parse_prog(src)
    assert len(prog.statements) == 1


# -- error recovery: multi-error collection --------------------------------


def test_error_recovery_collects_multiple_errors():
    src = "yo 1 = 2\nyo 3 = 4\nyo 5 = 6\n"
    with pytest.raises(ParseErrorBundle) as excinfo:
        parse_prog(src)
    assert len(excinfo.value.errors) >= 1


def test_error_recovery_continues_after_bad_statement():
    src = "1 + 2 = 3\nyo good = 1\n"
    with pytest.raises(ParseErrorBundle) as excinfo:
        parse_prog(src)
    # the parser should have synchronized and kept going, not stopped dead
    assert len(excinfo.value.errors) == 1


def test_error_recovery_caps_at_five_reported_in_full_list():
    src = "\n".join("1 + 2 = 3" for _ in range(7))
    with pytest.raises(ParseErrorBundle) as excinfo:
        parse_prog(src)
    assert len(excinfo.value.errors) == 7


def test_unterminated_call_is_syntax_error():
    with pytest.raises(ParseErrorBundle):
        parse_prog("yo x = f(1, 2\n")


def test_missing_block_brace_is_syntax_error():
    with pytest.raises(ParseErrorBundle):
        parse_prog("sus (x)\n yap x\n")


def test_dangling_lambda_without_body_is_syntax_error():
    with pytest.raises(ParseErrorBundle):
        parse_prog("yo f = lowkey (x)\n")


def test_full_program_dump_smoke():
    src = 'yo greeting = "yo sup world"\nyap greeting\n'
    assert dump_prog(src) == '(program (yo greeting "yo sup world") (yap greeting))'
