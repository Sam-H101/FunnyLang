"""AST node dataclasses for the whole grammar (PLAN.md §3), plus dump_ast()."""
from __future__ import annotations

import json
from dataclasses import dataclass, field

from .source import Span


class Node:
    span: Span


class Expr(Node):
    pass


class Stmt(Node):
    pass


@dataclass(frozen=True)
class Param:
    name: str
    default: Expr | None
    span: Span


# ---------------------------------------------------------------------------
# Expressions
# ---------------------------------------------------------------------------


@dataclass(frozen=True)
class Literal(Expr):
    value: object  # int | float | str | bool | None(ghost)
    span: Span


@dataclass(frozen=True)
class TemplateString(Expr):
    # each part is ("str", str) or ("expr", Expr)
    parts: tuple
    span: Span


@dataclass(frozen=True)
class Identifier(Expr):
    name: str
    span: Span


@dataclass(frozen=True)
class Unary(Expr):
    op: str
    operand: Expr
    span: Span


@dataclass(frozen=True)
class Binary(Expr):
    op: str
    left: Expr
    right: Expr
    span: Span


@dataclass(frozen=True)
class Logical(Expr):
    op: str  # "&&" | "||"
    left: Expr
    right: Expr
    span: Span


@dataclass(frozen=True)
class Coalesce(Expr):
    left: Expr
    right: Expr
    span: Span


@dataclass(frozen=True)
class Pipe(Expr):
    left: Expr
    right: Expr
    span: Span


@dataclass(frozen=True)
class Ternary(Expr):
    cond: Expr
    then_expr: Expr
    else_expr: Expr
    span: Span


@dataclass(frozen=True)
class Assign(Expr):
    target: Identifier
    op: str  # "=" | "+=" | ... | "||="
    value: Expr
    span: Span


@dataclass(frozen=True)
class Set(Expr):
    obj: Expr
    name: str
    op: str
    value: Expr
    span: Span


@dataclass(frozen=True)
class SetIndex(Expr):
    obj: Expr
    index: Expr
    op: str
    value: Expr
    span: Span


@dataclass(frozen=True)
class Call(Expr):
    callee: Expr
    args: tuple
    span: Span


@dataclass(frozen=True)
class Index(Expr):
    obj: Expr
    index: Expr
    span: Span


@dataclass(frozen=True)
class Slice(Expr):
    obj: Expr
    start: Expr | None
    stop: Expr | None
    step: Expr | None
    span: Span


@dataclass(frozen=True)
class Get(Expr):
    obj: Expr
    name: str
    span: Span


@dataclass(frozen=True)
class SafeGet(Expr):
    obj: Expr
    name: str
    span: Span


@dataclass(frozen=True)
class StashLit(Expr):
    elements: tuple
    span: Span


@dataclass(frozen=True)
class GroupChatLit(Expr):
    pairs: tuple  # tuple[tuple[Expr, Expr], ...]
    span: Span


@dataclass(frozen=True)
class Lambda(Expr):
    params: tuple
    variadic: str | None
    body: object  # Expr (arrow form) or Block (brace form)
    is_expr_body: bool
    span: Span


@dataclass(frozen=True)
class Me(Expr):
    span: Span


@dataclass(frozen=True)
class Og(Expr):
    span: Span


# ---------------------------------------------------------------------------
# Statements
# ---------------------------------------------------------------------------


@dataclass(frozen=True)
class Block(Stmt):
    statements: tuple
    span: Span


@dataclass(frozen=True)
class VarDecl(Stmt):
    name: str
    initializer: Expr | None
    span: Span


@dataclass(frozen=True)
class ConstDecl(Stmt):
    name: str
    initializer: Expr
    span: Span


@dataclass(frozen=True)
class FuncDecl(Stmt):
    name: str
    params: tuple
    variadic: str | None
    body: Block
    span: Span


@dataclass(frozen=True)
class SquadDecl(Stmt):
    name: str
    superclass: str | None
    spawn: FuncDecl | None
    methods: tuple
    span: Span


@dataclass(frozen=True)
class If(Stmt):
    cond: Expr
    then_branch: Block
    elif_branches: tuple  # tuple[tuple[Expr, Block], ...]
    else_branch: Block | None
    span: Span


@dataclass(frozen=True)
class While(Stmt):
    cond: Expr
    body: Block
    span: Span


@dataclass(frozen=True)
class ForRange(Stmt):
    var: str
    start: Expr
    stop: Expr
    step: Expr | None
    body: Block
    span: Span


@dataclass(frozen=True)
class ForEach(Stmt):
    var: str
    iterable: Expr
    body: Block
    span: Span


@dataclass(frozen=True)
class Return(Stmt):
    value: Expr | None
    span: Span


@dataclass(frozen=True)
class Break(Stmt):
    span: Span


@dataclass(frozen=True)
class Continue(Stmt):
    span: Span


@dataclass(frozen=True)
class Try(Stmt):
    body: Block
    catch_var: str | None
    catch_body: Block | None
    finally_body: Block | None
    span: Span


@dataclass(frozen=True)
class Chuck(Stmt):
    value: Expr
    span: Span


@dataclass(frozen=True)
class Import(Stmt):
    source: str  # quoted path text, or bare stdlib module name
    is_stdlib: bool
    alias: str | None
    names: tuple | None  # named-import form: gimme { a, b } from "..."
    span: Span


@dataclass(frozen=True)
class Export(Stmt):
    decl: Stmt
    span: Span


@dataclass(frozen=True)
class Yap(Stmt):
    args: tuple
    newline: bool
    span: Span


@dataclass(frozen=True)
class ExprStmt(Stmt):
    expr: Expr
    span: Span


@dataclass(frozen=True)
class VibeStmt(Stmt):
    span: Span


@dataclass(frozen=True)
class Program(Node):
    statements: tuple
    span: Span


# ---------------------------------------------------------------------------
# dump_ast: a compact S-expression printer, used by golden parser tests.
# ---------------------------------------------------------------------------


def _lit(value: object) -> str:
    if value is None:
        return "ghost"
    if isinstance(value, bool):
        return "fax" if value else "cap"
    if isinstance(value, str):
        return json.dumps(value)
    return repr(value) if isinstance(value, float) else str(value)


def _opt(node) -> str:
    return dump_ast(node) if node is not None else "ghost"


def dump_ast(node) -> str:  # noqa: C901 - one big dispatch, that's the point
    if node is None:
        return "ghost"
    if isinstance(node, Program):
        return "(program " + " ".join(dump_ast(s) for s in node.statements) + ")"
    if isinstance(node, Literal):
        return _lit(node.value)
    if isinstance(node, TemplateString):
        parts = " ".join(
            json.dumps(p[1]) if p[0] == "str" else dump_ast(p[1]) for p in node.parts
        )
        return f"(template {parts})"
    if isinstance(node, Identifier):
        return node.name
    if isinstance(node, Unary):
        return f"({node.op} {dump_ast(node.operand)})"
    if isinstance(node, (Binary, Logical)):
        return f"({node.op} {dump_ast(node.left)} {dump_ast(node.right)})"
    if isinstance(node, Coalesce):
        return f"(?? {dump_ast(node.left)} {dump_ast(node.right)})"
    if isinstance(node, Pipe):
        return f"(|> {dump_ast(node.left)} {dump_ast(node.right)})"
    if isinstance(node, Ternary):
        return f"(?: {dump_ast(node.cond)} {dump_ast(node.then_expr)} {dump_ast(node.else_expr)})"
    if isinstance(node, Assign):
        return f"({node.op} {node.target.name} {dump_ast(node.value)})"
    if isinstance(node, Set):
        return f"({node.op} (get {dump_ast(node.obj)} {node.name}) {dump_ast(node.value)})"
    if isinstance(node, SetIndex):
        return f"({node.op} (index {dump_ast(node.obj)} {dump_ast(node.index)}) {dump_ast(node.value)})"
    if isinstance(node, Call):
        args = " ".join(dump_ast(a) for a in node.args)
        return f"(call {dump_ast(node.callee)}{' ' + args if args else ''})"
    if isinstance(node, Index):
        return f"(index {dump_ast(node.obj)} {dump_ast(node.index)})"
    if isinstance(node, Slice):
        return f"(slice {dump_ast(node.obj)} {_opt(node.start)} {_opt(node.stop)} {_opt(node.step)})"
    if isinstance(node, Get):
        return f"(get {dump_ast(node.obj)} {node.name})"
    if isinstance(node, SafeGet):
        return f"(safe-get {dump_ast(node.obj)} {node.name})"
    if isinstance(node, StashLit):
        return "(stash " + " ".join(dump_ast(e) for e in node.elements) + ")"
    if isinstance(node, GroupChatLit):
        pairs = " ".join(f"({dump_ast(k)} {dump_ast(v)})" for k, v in node.pairs)
        return f"(groupchat {pairs})"
    if isinstance(node, Lambda):
        params = " ".join(p.name for p in node.params)
        variadic = f" ...{node.variadic}" if node.variadic else ""
        return f"(lambda ({params}{variadic}) {dump_ast(node.body)})"
    if isinstance(node, Me):
        return "me"
    if isinstance(node, Og):
        return "og"
    if isinstance(node, Block):
        return "(block " + " ".join(dump_ast(s) for s in node.statements) + ")"
    if isinstance(node, VarDecl):
        return f"(yo {node.name} {_opt(node.initializer)})"
    if isinstance(node, ConstDecl):
        return f"(deadass {node.name} {dump_ast(node.initializer)})"
    if isinstance(node, FuncDecl):
        params = " ".join(p.name for p in node.params)
        variadic = f" ...{node.variadic}" if node.variadic else ""
        return f"(bet {node.name} ({params}{variadic}) {dump_ast(node.body)})"
    if isinstance(node, SquadDecl):
        sup = f" inherits {node.superclass}" if node.superclass else ""
        spawn = f" {dump_ast(node.spawn)}" if node.spawn else ""
        methods = " ".join(dump_ast(m) for m in node.methods)
        return f"(squad {node.name}{sup}{spawn} {methods})"
    if isinstance(node, If):
        elifs = " ".join(f"(kinda_sus {dump_ast(c)} {dump_ast(b)})" for c, b in node.elif_branches)
        els = f" (nah {dump_ast(node.else_branch)})" if node.else_branch else ""
        return f"(sus {dump_ast(node.cond)} {dump_ast(node.then_branch)}{(' ' + elifs) if elifs else ''}{els})"
    if isinstance(node, While):
        return f"(bruh {dump_ast(node.cond)} {dump_ast(node.body)})"
    if isinstance(node, ForRange):
        step = f" step {dump_ast(node.step)}" if node.step is not None else ""
        return f"(grind {node.var} from {dump_ast(node.start)} to {dump_ast(node.stop)}{step} {dump_ast(node.body)})"
    if isinstance(node, ForEach):
        return f"(grind {node.var} in {dump_ast(node.iterable)} {dump_ast(node.body)})"
    if isinstance(node, Return):
        return f"(bounce {_opt(node.value)})"
    if isinstance(node, Break):
        return "(bail)"
    if isinstance(node, Continue):
        return "(nvm)"
    if isinstance(node, Try):
        catch = f" (my_bad {node.catch_var} {dump_ast(node.catch_body)})" if node.catch_body else ""
        fin = f" (regardless {dump_ast(node.finally_body)})" if node.finally_body else ""
        return f"(sketchy {dump_ast(node.body)}{catch}{fin})"
    if isinstance(node, Chuck):
        return f"(chuck {dump_ast(node.value)})"
    if isinstance(node, Import):
        kind = "stdlib" if node.is_stdlib else "path"
        alias = f" as {node.alias}" if node.alias else ""
        names = f" names({','.join(node.names)})" if node.names else ""
        return f"(gimme {kind} {json.dumps(node.source)}{alias}{names})"
    if isinstance(node, Export):
        return f"(flex {dump_ast(node.decl)})"
    if isinstance(node, Yap):
        kw = "yap" if node.newline else "mumble"
        args = " ".join(dump_ast(a) for a in node.args)
        return f"({kw}{' ' + args if args else ''})"
    if isinstance(node, ExprStmt):
        return dump_ast(node.expr)
    if isinstance(node, VibeStmt):
        return "(vibe)"
    raise TypeError(f"dump_ast: no case for {type(node).__name__}")
