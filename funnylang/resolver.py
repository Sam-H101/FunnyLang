"""Static scope resolution (PLAN.md §M3).

Produces, for every identifier reference, whether it's a LOCAL (stack slot),
an UPVALUE (captured from an enclosing function), or a GLOBAL (looked up by
name at runtime) — plus, per function, its local-slot high-water-mark and its
upvalue-capture list, in exactly the shape the M4 compiler needs to emit
`CLOSURE`'s operand list and decide `CLOSE_UPVAL` vs `POP` at scope exit.

This mirrors the classic Lua/Crafting-Interpreters compiler's local/upvalue
bookkeeping (PLAN.md §3.6 cites that model explicitly), just as a separate
pass instead of interleaved with codegen, since PLAN.md keeps resolver.py and
compiler.py as separate milestones/files.

Results are keyed by AST node identity (`id(node)`), not attached to the
(frozen) nodes themselves — the same technique Crafting Interpreters' Java
`Resolver` uses. Callers must keep the resolved `Program` tree alive and
reuse the very same node objects when compiling; don't re-parse in between.
"""
from __future__ import annotations

from dataclasses import dataclass, field

from . import ast_nodes as A
from .errors import ImmutableVibes, ParserHadAStroke, WhoDis, suggest_name

# Kept in sync by hand with stdlib/builtins.py (M6) and stdlib/__init__.py (M6).
# `gimme <name>` only ever resolves bare identifiers to these (PLAN.md §3.8).
BUILTIN_GLOBAL_NAMES: frozenset[str] = frozenset(
    {
        "how_thicc", "what_is_it", "to_yap", "to_numba", "to_int", "sheesh",
        "no_cap", "ask", "dip", "the_args", "combo", "identity",
        "range_stash", "zip_em", "enumerate_em", "deep_clone",
    }
)

STDLIB_MODULE_NAMES: frozenset[str] = frozenset(
    {"mafs", "yapper", "stash", "groupchat", "rizz", "filez", "clock", "sus", "computer", "internet"}
)

MAX_LOCALS = 256  # local/upvalue slots are u8 in the bytecode (PLAN.md §5)


@dataclass(frozen=True)
class Resolution:
    kind: str  # "local" | "upvalue" | "global"
    index: int = -1
    name: str = ""
    is_const: bool = False


@dataclass
class UpvalueInfo:
    is_local: bool
    index: int
    is_const: bool = False


@dataclass
class FuncInfo:
    local_count: int
    upvalues: list = field(default_factory=list)  # list[UpvalueInfo]
    captured_slots: set = field(default_factory=set)  # set[int]


@dataclass
class ResolverResult:
    identifier_resolutions: dict  # id(node) -> Resolution, for Identifier/Assign/Set.obj etc. reads
    decl_slots: dict  # (id(decl node), name) -> local slot (absent => global)
    func_info: dict  # id(Program|FuncDecl|Lambda) -> FuncInfo


class _Local:
    __slots__ = ("name", "depth", "is_const", "span", "captured")

    def __init__(self, name: str, depth: int, is_const: bool, span):
        self.name = name
        self.depth = depth
        self.is_const = is_const
        self.span = span
        self.captured = False


class _FunctionScope:
    def __init__(self, enclosing: "_FunctionScope | None", is_script: bool, is_method: bool = False):
        self.enclosing = enclosing
        self.is_script = is_script
        self.is_method = is_method
        self.locals: list[_Local] = []
        self.max_locals = 0
        self.scope_depth = 0 if is_script else 1
        self.loop_depth = 0
        self.upvalues: list[UpvalueInfo] = []
        self.captured_slots: set[int] = set()
        self.declared_here: set[str] = set()  # names declared at true global depth (dup check)


class Resolver:
    def __init__(self, source=None):
        self.source = source
        self.result = ResolverResult({}, {}, {})
        self.known_globals: set[str] = set(BUILTIN_GLOBAL_NAMES) | set(STDLIB_MODULE_NAMES)
        self.const_globals: set[str] = set()
        self.root: _FunctionScope | None = None
        self.current: _FunctionScope | None = None

    # -- public API -----------------------------------------------------

    def resolve(self, program: A.Program) -> ResolverResult:
        self.root = _FunctionScope(enclosing=None, is_script=True)
        self.current = self.root
        self._prepass_globals(program.statements)
        for stmt in program.statements:
            self._resolve_stmt(stmt)
        self.result.func_info[id(program)] = FuncInfo(
            local_count=self.root.max_locals,
            upvalues=[],
            captured_slots=self.root.captured_slots,
        )
        return self.result

    # -- helpers ----------------------------------------------------------

    def _err(self, span, message: str, roast: str | None = None) -> ParserHadAStroke:
        return ParserHadAStroke(message, span=span, source=self.source, roast=roast)

    def _module_binding_name(self, path: str) -> str:
        from pathlib import PurePosixPath

        return PurePosixPath(path.replace("\\", "/")).stem

    def _import_binding_names(self, imp: A.Import) -> list[str]:
        if imp.names is not None:
            return list(imp.names)
        if imp.alias is not None:
            return [imp.alias]
        if imp.is_stdlib:
            return [imp.source]
        return [self._module_binding_name(imp.source)]

    def _prepass_globals(self, statements) -> None:
        """Forward-declares every name a top-level statement will introduce,
        so mutually-recursive top-level functions (and calls to functions
        defined later in the file) resolve without complaint."""
        for stmt in statements:
            target = stmt.decl if isinstance(stmt, A.Export) else stmt
            if isinstance(target, (A.VarDecl, A.ConstDecl, A.FuncDecl)):
                self.known_globals.add(target.name)
            elif isinstance(target, A.Import):
                self.known_globals.update(self._import_binding_names(target))

    # -- scope management -------------------------------------------------

    def _begin_scope(self) -> None:
        self.current.scope_depth += 1

    def _end_scope(self) -> None:
        scope = self.current
        depth = scope.scope_depth
        while scope.locals and scope.locals[-1].depth == depth:
            scope.locals.pop()
        scope.scope_depth -= 1

    def _declare_local(self, name: str, is_const: bool, span) -> int:
        scope = self.current
        depth = scope.scope_depth
        for local in reversed(scope.locals):
            if local.depth < depth:
                break
            if local.name == name:
                raise self._err(
                    span,
                    f"'{name}' is already declared in this scope.",
                    f"you already declared `{name}` right there. scroll up.",
                )
        if len(scope.locals) >= MAX_LOCALS:
            raise self._err(span, f"this function has more than {MAX_LOCALS} locals. break it up.")
        slot = len(scope.locals)
        scope.locals.append(_Local(name, depth, is_const, span))
        scope.max_locals = max(scope.max_locals, len(scope.locals))
        return slot

    def _declare_binding(self, name: str, is_const: bool, span, decl_node) -> None:
        """Declares `name` as a local (if we're inside any real scope) or a
        global (only at the outermost script's un-nested top level)."""
        if self.current.scope_depth == 0:
            if name in self.current.declared_here:
                raise self._err(
                    span,
                    f"'{name}' is already declared.",
                    f"you already declared `{name}` right there. scroll up.",
                )
            self.current.declared_here.add(name)
            self.known_globals.add(name)
            if is_const:
                self.const_globals.add(name)
            else:
                self.const_globals.discard(name)
            # globals carry no slot: decl_slots simply has no entry for them.
            return
        slot = self._declare_local(name, is_const, span)
        self.result.decl_slots[(id(decl_node), name)] = slot

    def _resolve_local(self, scope: _FunctionScope, name: str) -> int | None:
        for i in range(len(scope.locals) - 1, -1, -1):
            if scope.locals[i].name == name:
                return i
        return None

    def _add_upvalue(self, scope: _FunctionScope, index: int, is_local: bool, is_const: bool) -> int:
        for i, uv in enumerate(scope.upvalues):
            if uv.index == index and uv.is_local == is_local:
                return i
        scope.upvalues.append(UpvalueInfo(is_local, index, is_const))
        return len(scope.upvalues) - 1

    def _resolve_upvalue(self, scope: _FunctionScope, name: str) -> tuple[int, bool] | None:
        if scope.enclosing is None:
            return None
        local_idx = self._resolve_local(scope.enclosing, name)
        if local_idx is not None:
            local = scope.enclosing.locals[local_idx]
            local.captured = True
            scope.enclosing.captured_slots.add(local_idx)
            idx = self._add_upvalue(scope, local_idx, True, local.is_const)
            return idx, local.is_const
        outer = self._resolve_upvalue(scope.enclosing, name)
        if outer is not None:
            outer_idx, outer_const = outer
            idx = self._add_upvalue(scope, outer_idx, False, outer_const)
            return idx, outer_const
        return None

    def _all_visible_names(self) -> list[str]:
        names = []
        scope = self.current
        while scope is not None:
            names.extend(local.name for local in scope.locals)
            scope = scope.enclosing
        names.extend(self.known_globals)
        return names

    def _resolve_name(self, name: str, span) -> Resolution:
        local_idx = self._resolve_local(self.current, name)
        if local_idx is not None:
            local = self.current.locals[local_idx]
            return Resolution("local", local_idx, is_const=local.is_const)
        upvalue = self._resolve_upvalue(self.current, name)
        if upvalue is not None:
            idx, is_const = upvalue
            return Resolution("upvalue", idx, is_const=is_const)
        if name in self.known_globals:
            return Resolution("global", name=name, is_const=name in self.const_globals)
        hint = suggest_name(name, self._all_visible_names())
        message = f"'{name}' isn't defined anywhere i can see."
        roast = f"`{name}` who? never heard of them."
        if hint:
            roast += f" did you mean `{hint}`?"
        raise WhoDis(message, span=span, source=self.source, roast=roast, hint=(f"did you mean `{hint}`?" if hint else None))

    # -- statements -----------------------------------------------------

    def _resolve_stmt(self, stmt) -> None:
        method = getattr(self, f"_stmt_{type(stmt).__name__}", None)
        if method is None:
            raise TypeError(f"resolver: no handler for statement {type(stmt).__name__}")
        method(stmt)

    def _resolve_nested_block(self, block: A.Block) -> None:
        self._begin_scope()
        for stmt in block.statements:
            self._resolve_stmt(stmt)
        self._end_scope()

    def _stmt_Block(self, node: A.Block) -> None:
        self._resolve_nested_block(node)

    def _stmt_VibeStmt(self, node: A.VibeStmt) -> None:
        pass

    def _stmt_VarDecl(self, node: A.VarDecl) -> None:
        if node.initializer is not None:
            self._resolve_expr(node.initializer)
        self._declare_binding(node.name, False, node.span, node)

    def _stmt_ConstDecl(self, node: A.ConstDecl) -> None:
        self._resolve_expr(node.initializer)
        self._declare_binding(node.name, True, node.span, node)

    def _stmt_FuncDecl(self, node: A.FuncDecl, is_method: bool = False) -> None:
        # Declare the name in the *enclosing* scope first so the body can
        # call itself recursively (and, at top level, so sibling functions
        # can call each other regardless of declaration order).
        if not is_method:
            self._declare_binding(node.name, False, node.span, node)
        self._resolve_function(node.params, node.variadic, node.body.statements, node, is_method)

    def _resolve_function(self, params, variadic, body_statements, node, is_method: bool) -> None:
        scope = _FunctionScope(enclosing=self.current, is_script=False, is_method=is_method)
        self.current = scope
        for p in params:
            if p.default is not None:
                self._resolve_expr(p.default)
            slot = self._declare_local(p.name, False, p.span)
            self.result.decl_slots[(id(p), p.name)] = slot
        if variadic is not None:
            self._declare_local(variadic, False, node.span)
        for stmt in body_statements:
            self._resolve_stmt(stmt)
        self.result.func_info[id(node)] = FuncInfo(
            local_count=scope.max_locals,
            upvalues=scope.upvalues,
            captured_slots=scope.captured_slots,
        )
        self.current = scope.enclosing

    def _stmt_If(self, node: A.If) -> None:
        self._resolve_expr(node.cond)
        self._resolve_nested_block(node.then_branch)
        for cond, block in node.elif_branches:
            self._resolve_expr(cond)
            self._resolve_nested_block(block)
        if node.else_branch is not None:
            self._resolve_nested_block(node.else_branch)

    def _stmt_While(self, node: A.While) -> None:
        self._resolve_expr(node.cond)
        self.current.loop_depth += 1
        self._resolve_nested_block(node.body)
        self.current.loop_depth -= 1

    def _stmt_ForRange(self, node: A.ForRange) -> None:
        self._resolve_expr(node.start)
        self._resolve_expr(node.stop)
        if node.step is not None:
            self._resolve_expr(node.step)
        self._begin_scope()
        self._declare_local(node.var, False, node.span)
        self.current.loop_depth += 1
        for stmt in node.body.statements:
            self._resolve_stmt(stmt)
        self.current.loop_depth -= 1
        self._end_scope()

    def _stmt_ForEach(self, node: A.ForEach) -> None:
        self._resolve_expr(node.iterable)
        self._begin_scope()
        self._declare_local(node.var, False, node.span)
        self.current.loop_depth += 1
        for stmt in node.body.statements:
            self._resolve_stmt(stmt)
        self.current.loop_depth -= 1
        self._end_scope()

    def _stmt_Return(self, node: A.Return) -> None:
        if self.current.is_script:
            raise self._err(
                node.span,
                "'bounce' isn't valid at the top level.",
                "bounce to where? this is the top.",
            )
        if node.value is not None:
            self._resolve_expr(node.value)

    def _stmt_Break(self, node: A.Break) -> None:
        if self.current.loop_depth == 0:
            raise self._err(node.span, "'bail' isn't valid outside a loop.", "bail from what, exactly?")

    def _stmt_Continue(self, node: A.Continue) -> None:
        if self.current.loop_depth == 0:
            raise self._err(node.span, "'nvm' isn't valid outside a loop.", "nvm what, exactly? there's no loop.")

    def _stmt_Try(self, node: A.Try) -> None:
        self._resolve_nested_block(node.body)
        if node.catch_body is not None:
            self._begin_scope()
            self._declare_local(node.catch_var, False, node.span)
            for stmt in node.catch_body.statements:
                self._resolve_stmt(stmt)
            self._end_scope()
        if node.finally_body is not None:
            self._resolve_nested_block(node.finally_body)

    def _stmt_Chuck(self, node: A.Chuck) -> None:
        self._resolve_expr(node.value)

    def _stmt_Import(self, node: A.Import) -> None:
        for name in self._import_binding_names(node):
            self._declare_binding(name, False, node.span, node)

    def _stmt_Export(self, node: A.Export) -> None:
        self._resolve_stmt(node.decl)

    def _stmt_Yap(self, node: A.Yap) -> None:
        for arg in node.args:
            self._resolve_expr(arg)

    def _stmt_ExprStmt(self, node: A.ExprStmt) -> None:
        self._resolve_expr(node.expr)

    # -- expressions ------------------------------------------------------

    def _resolve_expr(self, expr) -> None:
        method = getattr(self, f"_expr_{type(expr).__name__}", None)
        if method is None:
            raise TypeError(f"resolver: no handler for expression {type(expr).__name__}")
        method(expr)

    def _expr_Literal(self, node: A.Literal) -> None:
        pass

    def _expr_TemplateString(self, node: A.TemplateString) -> None:
        for kind, value in node.parts:
            if kind == "expr":
                self._resolve_expr(value)

    def _expr_Identifier(self, node: A.Identifier) -> None:
        self.result.identifier_resolutions[id(node)] = self._resolve_name(node.name, node.span)

    def _expr_Unary(self, node: A.Unary) -> None:
        self._resolve_expr(node.operand)

    def _expr_Binary(self, node: A.Binary) -> None:
        self._resolve_expr(node.left)
        self._resolve_expr(node.right)

    def _expr_Logical(self, node: A.Logical) -> None:
        self._resolve_expr(node.left)
        self._resolve_expr(node.right)

    def _expr_Coalesce(self, node: A.Coalesce) -> None:
        self._resolve_expr(node.left)
        self._resolve_expr(node.right)

    def _expr_Pipe(self, node: A.Pipe) -> None:
        self._resolve_expr(node.left)
        self._resolve_expr(node.right)

    def _expr_Ternary(self, node: A.Ternary) -> None:
        self._resolve_expr(node.cond)
        self._resolve_expr(node.then_expr)
        self._resolve_expr(node.else_expr)

    def _expr_Assign(self, node: A.Assign) -> None:
        self._resolve_expr(node.value)
        resolution = self._resolve_name(node.target.name, node.target.span)
        self.result.identifier_resolutions[id(node.target)] = resolution
        if resolution.is_const:
            raise ImmutableVibes(
                f"'{node.target.name}' is a deadass constant.",
                span=node.span,
                source=self.source,
                roast=f"`{node.target.name}` is deadass. it doesn't change. like your ex's opinion of you.",
            )

    def _expr_Set(self, node: A.Set) -> None:
        self._resolve_expr(node.obj)
        self._resolve_expr(node.value)

    def _expr_SetIndex(self, node: A.SetIndex) -> None:
        self._resolve_expr(node.obj)
        self._resolve_expr(node.index)
        self._resolve_expr(node.value)

    def _expr_Call(self, node: A.Call) -> None:
        self._resolve_expr(node.callee)
        for arg in node.args:
            self._resolve_expr(arg)

    def _expr_Index(self, node: A.Index) -> None:
        self._resolve_expr(node.obj)
        self._resolve_expr(node.index)

    def _expr_Slice(self, node: A.Slice) -> None:
        self._resolve_expr(node.obj)
        for part in (node.start, node.stop, node.step):
            if part is not None:
                self._resolve_expr(part)

    def _expr_Get(self, node: A.Get) -> None:
        self._resolve_expr(node.obj)

    def _expr_SafeGet(self, node: A.SafeGet) -> None:
        self._resolve_expr(node.obj)

    def _expr_StashLit(self, node: A.StashLit) -> None:
        for el in node.elements:
            self._resolve_expr(el)

    def _expr_GroupChatLit(self, node: A.GroupChatLit) -> None:
        for k, v in node.pairs:
            self._resolve_expr(k)
            self._resolve_expr(v)

    def _expr_Lambda(self, node: A.Lambda) -> None:
        if node.is_expr_body:
            body_statements = [A.Return(node.body, node.body.span)]
        else:
            body_statements = list(node.body.statements)
        self._resolve_function(node.params, node.variadic, body_statements, node, is_method=False)

    def _expr_Me(self, node: A.Me) -> None:
        if not self._in_method_context():
            raise self._err(node.span, "'me' isn't valid outside a squad.", "who's 'me'? you're not in a squad.")

    def _expr_Og(self, node: A.Og) -> None:
        if not self._in_method_context():
            raise self._err(node.span, "'og' isn't valid outside a squad.", "'og' who? you're not in a squad.")

    def _in_method_context(self) -> bool:
        scope = self.current
        while scope is not None:
            if scope.is_method:
                return True
            scope = scope.enclosing
        return False


def resolve_program(program: A.Program, source=None) -> ResolverResult:
    return Resolver(source).resolve(program)
