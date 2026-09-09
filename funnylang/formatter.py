"""Canonical formatter (PLAN.md §M8): an AST pretty-printer. 4-space indent,
`{` on the same line, spaces around binary operators. Since the AST doesn't
retain comments, formatting drops them — a known, accepted limitation of an
AST-based formatter (not a source-text-preserving one)."""
from __future__ import annotations

from . import ast_nodes as A

INDENT = "    "


class Formatter:
    def __init__(self):
        self.lines: list[str] = []

    def format_program(self, program: A.Program) -> str:
        self._stmts(program.statements, 0)
        text = "\n".join(self.lines)
        return text + "\n" if text else ""

    def _emit(self, depth: int, text: str) -> None:
        if not text:
            self.lines.append("")
            return
        # A nested block-bodied lambda formats its own body starting at a
        # relative depth of 0; every physical line it produced (including
        # its own closing brace) still needs this call's depth added on top
        # so it nests correctly inside whatever statement embeds it.
        for line in text.split("\n"):
            self.lines.append(f"{INDENT * depth}{line}" if line else "")

    def _stmts(self, statements, depth: int) -> None:
        for i, stmt in enumerate(statements):
            self._stmt(stmt, depth)

    def _block(self, block: A.Block, depth: int, prefix: str = "") -> None:
        self._emit(depth, f"{prefix}{{" if prefix else "{")
        self._stmts(block.statements, depth + 1)
        self._emit(depth, "}")

    # -- statements ---------------------------------------------------------

    def _stmt(self, node, depth: int) -> None:
        method = getattr(self, f"_s_{type(node).__name__}", None)
        if method is None:
            raise TypeError(f"formatter: no case for {type(node).__name__}")
        method(node, depth)

    def _s_VarDecl(self, node: A.VarDecl, depth: int) -> None:
        if node.initializer is None:
            self._emit(depth, f"yo {node.name}")
        else:
            self._emit(depth, f"yo {node.name} = {self._expr(node.initializer)}")

    def _s_ConstDecl(self, node: A.ConstDecl, depth: int) -> None:
        self._emit(depth, f"deadass {node.name} = {self._expr(node.initializer)}")

    def _params(self, params, variadic) -> str:
        parts = [p.name if p.default is None else f"{p.name} = {self._expr(p.default)}" for p in params]
        if variadic is not None:
            parts.append(f"...{variadic}")
        return ", ".join(parts)

    def _s_FuncDecl(self, node: A.FuncDecl, depth: int) -> None:
        header = f"bet {node.name}({self._params(node.params, node.variadic)}) "
        self._block(node.body, depth, prefix=header)

    def _s_Block(self, node: A.Block, depth: int) -> None:
        self._block(node, depth)

    def _s_If(self, node: A.If, depth: int) -> None:
        self._block(node.then_branch, depth, prefix=f"sus ({self._expr(node.cond)}) ")
        for cond, block in node.elif_branches:
            self.lines[-1] += f" kinda_sus ({self._expr(cond)}) {{"
            self._stmts(block.statements, depth + 1)
            self._emit(depth, "}")
        if node.else_branch is not None:
            self.lines[-1] += " nah {"
            self._stmts(node.else_branch.statements, depth + 1)
            self._emit(depth, "}")

    def _s_While(self, node: A.While, depth: int) -> None:
        self._block(node.body, depth, prefix=f"bruh ({self._expr(node.cond)}) ")

    def _s_ForRange(self, node: A.ForRange, depth: int) -> None:
        step = f" step {self._expr(node.step)}" if node.step is not None else ""
        prefix = f"grind {node.var} from {self._expr(node.start)} to {self._expr(node.stop)}{step} "
        self._block(node.body, depth, prefix=prefix)

    def _s_ForEach(self, node: A.ForEach, depth: int) -> None:
        prefix = f"grind {node.var} in {self._expr(node.iterable)} "
        self._block(node.body, depth, prefix=prefix)

    def _s_Return(self, node: A.Return, depth: int) -> None:
        if node.value is None:
            self._emit(depth, "bounce")
        else:
            self._emit(depth, f"bounce {self._expr(node.value)}")

    def _s_Break(self, node: A.Break, depth: int) -> None:
        self._emit(depth, "bail")

    def _s_Continue(self, node: A.Continue, depth: int) -> None:
        self._emit(depth, "nvm")

    def _s_Try(self, node: A.Try, depth: int) -> None:
        self._block(node.body, depth, prefix="sketchy ")
        if node.catch_body is not None:
            self.lines[-1] += f" my_bad ({node.catch_var}) {{"
            self._stmts(node.catch_body.statements, depth + 1)
            self._emit(depth, "}")
        if node.finally_body is not None:
            self.lines[-1] += " regardless {"
            self._stmts(node.finally_body.statements, depth + 1)
            self._emit(depth, "}")

    def _s_Chuck(self, node: A.Chuck, depth: int) -> None:
        self._emit(depth, f"chuck {self._expr(node.value)}")

    def _s_Import(self, node: A.Import, depth: int) -> None:
        if node.names is not None:
            names = ", ".join(node.names)
            self._emit(depth, f'gimme {{ {names} }} from "{node.source}"')
        elif node.is_stdlib:
            alias = f" as {node.alias}" if node.alias else ""
            self._emit(depth, f"gimme {node.source}{alias}")
        else:
            alias = f" as {node.alias}" if node.alias else ""
            self._emit(depth, f'gimme "{node.source}"{alias}')

    def _s_Export(self, node: A.Export, depth: int) -> None:
        before = len(self.lines)
        self._stmt(node.decl, depth)
        self.lines[before] = f"{INDENT * depth}flex {self.lines[before][len(INDENT) * depth:]}"

    def _s_Yap(self, node: A.Yap, depth: int) -> None:
        kw = "yap" if node.newline else "mumble"
        args = ", ".join(self._expr(a) for a in node.args)
        self._emit(depth, f"{kw} {args}" if args else kw)

    def _s_ExprStmt(self, node: A.ExprStmt, depth: int) -> None:
        self._emit(depth, self._expr(node.expr))

    def _s_VibeStmt(self, node: A.VibeStmt, depth: int) -> None:
        self._emit(depth, "vibe")

    def _s_SquadDecl(self, node: A.SquadDecl, depth: int) -> None:
        sup = f" inherits {node.superclass}" if node.superclass else ""
        self._emit(depth, f"squad {node.name}{sup} {{")
        if node.spawn is not None:
            self._emit(depth + 1, f"spawn({self._params(node.spawn.params, node.spawn.variadic)}) {{")
            self._stmts(node.spawn.body.statements, depth + 2)
            self._emit(depth + 1, "}")
        for m in node.methods:
            self._emit(depth + 1, f"bet {m.name}({self._params(m.params, m.variadic)}) {{")
            self._stmts(m.body.statements, depth + 2)
            self._emit(depth + 1, "}")
        self._emit(depth, "}")

    # -- expressions ------------------------------------------------------

    def _expr(self, node) -> str:
        method = getattr(self, f"_e_{type(node).__name__}", None)
        if method is None:
            raise TypeError(f"formatter: no case for {type(node).__name__}")
        return method(node)

    def _e_Literal(self, node: A.Literal) -> str:
        return A._lit(node.value)

    def _e_TemplateString(self, node: A.TemplateString) -> str:
        parts = []
        for kind, value in node.parts:
            parts.append(value if kind == "str" else "{" + self._expr(value) + "}")
        return "`" + "".join(parts) + "`"

    def _e_Identifier(self, node: A.Identifier) -> str:
        return node.name

    def _e_Unary(self, node: A.Unary) -> str:
        return f"{node.op}{self._expr(node.operand)}"

    def _e_Binary(self, node: A.Binary) -> str:
        return f"{self._expr(node.left)} {node.op} {self._expr(node.right)}"

    def _e_Logical(self, node: A.Logical) -> str:
        return f"{self._expr(node.left)} {node.op} {self._expr(node.right)}"

    def _e_Coalesce(self, node: A.Coalesce) -> str:
        return f"{self._expr(node.left)} ?? {self._expr(node.right)}"

    def _e_Pipe(self, node: A.Pipe) -> str:
        return f"{self._expr(node.left)} |> {self._expr(node.right)}"

    def _e_Ternary(self, node: A.Ternary) -> str:
        return f"{self._expr(node.cond)} ? {self._expr(node.then_expr)} : {self._expr(node.else_expr)}"

    def _e_Assign(self, node: A.Assign) -> str:
        return f"{node.target.name} {node.op} {self._expr(node.value)}"

    def _e_Set(self, node: A.Set) -> str:
        return f"{self._expr(node.obj)}.{node.name} {node.op} {self._expr(node.value)}"

    def _e_SetIndex(self, node: A.SetIndex) -> str:
        return f"{self._expr(node.obj)}[{self._expr(node.index)}] {node.op} {self._expr(node.value)}"

    def _e_Call(self, node: A.Call) -> str:
        args = ", ".join(self._expr(a) for a in node.args)
        return f"{self._expr(node.callee)}({args})"

    def _e_Index(self, node: A.Index) -> str:
        return f"{self._expr(node.obj)}[{self._expr(node.index)}]"

    def _e_Slice(self, node: A.Slice) -> str:
        start = self._expr(node.start) if node.start is not None else ""
        stop = self._expr(node.stop) if node.stop is not None else ""
        if node.step is not None:
            return f"{self._expr(node.obj)}[{start}:{stop}:{self._expr(node.step)}]"
        return f"{self._expr(node.obj)}[{start}:{stop}]"

    def _e_Get(self, node: A.Get) -> str:
        return f"{self._expr(node.obj)}.{node.name}"

    def _e_SafeGet(self, node: A.SafeGet) -> str:
        return f"{self._expr(node.obj)}?.{node.name}"

    def _e_StashLit(self, node: A.StashLit) -> str:
        return "[" + ", ".join(self._expr(e) for e in node.elements) + "]"

    def _e_GroupChatLit(self, node: A.GroupChatLit) -> str:
        pairs = ", ".join(f"{self._expr(k)}: {self._expr(v)}" for k, v in node.pairs)
        return "{" + pairs + "}"

    def _e_Lambda(self, node: A.Lambda) -> str:
        params = self._params(node.params, node.variadic)
        if node.is_expr_body:
            return f"lowkey ({params}) => {self._expr(node.body)}"
        inner = Formatter()
        inner._stmts(node.body.statements, 1)
        body = "\n".join(inner.lines)
        return f"lowkey ({params}) {{\n{body}\n}}"

    def _e_Me(self, node: A.Me) -> str:
        return "me"

    def _e_Og(self, node: A.Og) -> str:
        return "og"


def format_program(program: A.Program) -> str:
    return Formatter().format_program(program)
