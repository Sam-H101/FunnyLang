"""AST + ResolverResult -> bytecode (PLAN.md §M4).

One Compiler walks the whole program once. It keeps a stack of `_FuncCtx`
(one per function/lambda/script being compiled) mirroring the resolver's own
scope stack, so slot numbers the resolver assigned line up with the stack
positions the compiler actually pushes things to. Constant folding runs on
attempt at each Binary/Unary node before falling back to real codegen.
"""
from __future__ import annotations

from dataclasses import dataclass, field

from . import ast_nodes as A
from .chunk import Chunk, CompiledUnit, ConstPool, FunctionProto
from .opcodes import Op

_BINOP_OPCODE = {
    "+": Op.ADD, "-": Op.SUB, "*": Op.MUL, "/": Op.DIV, "\\": Op.IDIV,
    "%": Op.MOD, "**": Op.POW, "|": Op.BOR, "^": Op.BXOR, "&": Op.BAND,
    "==": Op.EQ, "!=": Op.NEQ, "<": Op.LT, "<=": Op.LE, ">": Op.GT,
    ">=": Op.GE, "in": Op.IN, "<<": Op.SHL, ">>": Op.SHR,
}

_UNOP_OPCODE = {"-": Op.NEG, "!": Op.NOT, "~": Op.BNOT}

# Constant folding (M4 task 3): literal-arithmetic only, ops that can't throw.
_FOLDABLE_NUMERIC = {"+", "-", "*", "|", "^", "&", "<<", ">>", "==", "!=", "<", "<=", ">", ">="}


@dataclass
class _LoopCtx:
    loop_start: int
    break_target: int  # stack depth to unwind to before jumping past the loop
    continue_target: int  # stack depth to unwind to before jumping to the next iteration
    break_sites: list = field(default_factory=list)
    continue_sites: list = field(default_factory=list)


class _FuncCtx:
    def __init__(self, chunk: Chunk, func_info):
        self.chunk = chunk
        self.func_info = func_info
        self.loops: list[_LoopCtx] = []


class Compiler:
    def __init__(self, resolver_result, source=None, fold_constants: bool = True):
        self.result = resolver_result
        self.source = source
        self.fold_constants = fold_constants
        self.const_pool = ConstPool()
        self.protos: list[FunctionProto] = []
        self.stack: list[_FuncCtx] = []

    # -- top level ----------------------------------------------------------

    def compile_program(self, program: A.Program, source_name: str = "<script>", repl_capture_last: bool = False) -> CompiledUnit:
        """`repl_capture_last`: if the program's last statement is a bare
        expression statement, compile it to RETURN its value instead of
        POPping it — funny vibe's "last expression value auto-printed"."""
        func_info = self.result.func_info[id(program)]
        chunk = Chunk("<script>", 0, 0, False, self.const_pool)
        chunk.local_count = func_info.local_count
        self.stack.append(_FuncCtx(chunk, func_info))
        statements = program.statements
        capture = repl_capture_last and statements and isinstance(statements[-1], A.ExprStmt)
        for stmt in (statements[:-1] if capture else statements):
            self._compile_stmt(stmt)
        if capture:
            self._compile_expr(statements[-1].expr)
        else:
            self._emit(Op.GHOST)
            self._push()
        self._emit(Op.RETURN)
        self._pop()
        proto = chunk.finish()
        self.stack.pop()
        entry_idx = self._add_proto(proto)
        return CompiledUnit(source_name, self.const_pool, self.protos, entry_idx)

    def _add_proto(self, proto: FunctionProto) -> int:
        self.protos.append(proto)
        return len(self.protos) - 1

    # -- small helpers delegating to the current chunk --------------------

    @property
    def ctx(self) -> _FuncCtx:
        return self.stack[-1]

    @property
    def chunk(self) -> Chunk:
        return self.stack[-1].chunk

    def _emit(self, op: Op, *operands, span=None):
        return self.chunk.emit(op, *operands, span=span)

    def _push(self, n: int = 1):
        self.chunk.push(n)

    def _pop(self, n: int = 1):
        self.chunk.pop(n)

    def _const_str(self, s: str) -> int:
        return self.const_pool.add_string(s)

    # -- statements ---------------------------------------------------------

    def _compile_stmt(self, stmt) -> None:
        method = getattr(self, f"_stmt_{type(stmt).__name__}", None)
        if method is None:
            raise TypeError(f"compiler: no handler for statement {type(stmt).__name__}")
        method(stmt)

    def _stmt_VibeStmt(self, node: A.VibeStmt) -> None:
        pass

    def _binding_slot(self, decl_node, name: str) -> int | None:
        return self.result.decl_slots.get((id(decl_node), name))

    def _finish_declaration(self, decl_node, name: str, span) -> None:
        """Value is already on top of the stack; either it becomes a local
        (nothing further to do — its stack position *is* its slot) or it
        must be popped into a named global."""
        slot = self._binding_slot(decl_node, name)
        if slot is None:
            self._emit(Op.DEF_GLOBAL, self._const_str(name), span=span)
            self._pop()
        # else: local — leave it exactly where it landed on the stack.

    def _stmt_VarDecl(self, node: A.VarDecl) -> None:
        if node.initializer is not None:
            self._compile_expr(node.initializer)
        else:
            self._emit(Op.GHOST, span=node.span)
            self._push()
        self._finish_declaration(node, node.name, node.span)

    def _stmt_ConstDecl(self, node: A.ConstDecl) -> None:
        self._compile_expr(node.initializer)
        self._finish_declaration(node, node.name, node.span)

    def _stmt_FuncDecl(self, node: A.FuncDecl) -> None:
        self._compile_closure(node, node.params, node.variadic, node.body.statements, node.name, node.span)
        self._finish_declaration(node, node.name, node.span)

    def _stmt_SquadDecl(self, node: A.SquadDecl) -> None:
        if node.superclass is not None:
            # INHERIT's stack effect is "super class -> super class" (both
            # kept, unchanged) — it wants the superclass pushed *first*.
            res = self.result.identifier_resolutions[id(node)]
            self._emit_get_resolution(res, node.span)
            self._push()
        self._emit(Op.SQUAD, self._const_str(node.name), span=node.span)
        self._push()
        if node.superclass is not None:
            self._emit(Op.INHERIT, span=node.span)
            self._emit(Op.SWAP, span=node.span)
            self._emit(Op.POP, span=node.span)  # drop the now-unneeded superclass ref
            self._pop()
        if node.spawn is not None:
            self._compile_closure(
                node.spawn, node.spawn.params, node.spawn.variadic,
                node.spawn.body.statements, "spawn", node.spawn.span, is_method=True,
            )
            self._emit(Op.METHOD, self._const_str("spawn"), span=node.span)
            self._pop()
        for m in node.methods:
            self._compile_closure(m, m.params, m.variadic, m.body.statements, m.name, m.span, is_method=True)
            self._emit(Op.METHOD, self._const_str(m.name), span=node.span)
            self._pop()
        self._finish_declaration(node, node.name, node.span)

    def _stmt_Block(self, node: A.Block) -> None:
        self._compile_scoped_block(node.statements)

    def _compile_scoped_block(self, statements) -> None:
        # Every local declared directly in `statements` lives at consecutive
        # stack slots starting at the current depth; pop them (CLOSE_UPVAL if
        # captured) once the block ends, exactly mirroring the resolver's
        # begin_scope()/end_scope().
        base_depth = self.chunk.stack_depth
        for stmt in statements:
            self._compile_stmt(stmt)
        self._close_locals_above(base_depth)

    def _close_locals_above(self, base_depth: int) -> None:
        while self.chunk.stack_depth > base_depth:
            self._close_or_pop_top()

    def _close_or_pop_top(self, span=None) -> None:
        """POP the top-of-stack slot, or CLOSE_UPVAL if some closure captured
        it — used for every kind of scope exit (blocks, loop iterations,
        unwinding), so a captured local *anywhere* reliably gets closed."""
        slot = self.chunk.stack_depth - 1
        if slot in self.ctx.func_info.captured_slots:
            self._emit(Op.CLOSE_UPVAL, span=span)
        else:
            self._emit(Op.POP, span=span)
        self._pop()

    def _stmt_If(self, node: A.If) -> None:
        end_jumps = []
        self._compile_expr(node.cond)
        else_jump = self.chunk.emit_jump(Op.JUMP_IF_FALSE, span=node.span)
        self._pop()
        self._compile_scoped_block(node.then_branch.statements)
        end_jumps.append(self.chunk.emit_jump(Op.JUMP, span=node.span))
        self.chunk.patch_jump(else_jump)
        for cond, block in node.elif_branches:
            self._compile_expr(cond)
            next_jump = self.chunk.emit_jump(Op.JUMP_IF_FALSE, span=cond.span)
            self._pop()
            self._compile_scoped_block(block.statements)
            end_jumps.append(self.chunk.emit_jump(Op.JUMP, span=cond.span))
            self.chunk.patch_jump(next_jump)
        if node.else_branch is not None:
            self._compile_scoped_block(node.else_branch.statements)
        for j in end_jumps:
            self.chunk.patch_jump(j)

    def _stmt_While(self, node: A.While) -> None:
        loop_start = self.chunk.next_offset
        self._compile_expr(node.cond)
        exit_jump = self.chunk.emit_jump(Op.JUMP_IF_FALSE, span=node.span)
        self._pop()
        base_depth = self.chunk.stack_depth
        self.ctx.loops.append(_LoopCtx(loop_start, base_depth, base_depth))
        self._compile_scoped_block(node.body.statements)
        loop_ctx = self.ctx.loops.pop()
        for site in loop_ctx.continue_sites:
            self.chunk.patch_jump(site)
        self.chunk.emit_loop(loop_start, span=node.span)
        self.chunk.patch_jump(exit_jump)
        for site in loop_ctx.break_sites:
            self.chunk.patch_jump(site)

    def _stmt_ForRange(self, node: A.ForRange) -> None:
        self._compile_expr(node.start)  # counter local
        counter_slot = self.chunk.stack_depth - 1
        if node.step is not None:
            self._compile_expr(node.step)
        else:
            self._emit(Op.CONST, self._const_int(1), span=node.span)
            self._push()
        step_slot = self.chunk.stack_depth - 1
        self._compile_expr(node.stop)
        stop_slot = self.chunk.stack_depth - 1

        loop_start = self.chunk.next_offset
        # cond = (step >= 0 && counter < stop) || (step < 0 && counter > stop)
        # Fully linear (no branches) — BAND/BOR on boolski operands is a
        # plain Python `&`/`|`, which is logical and/or for actual bools, so
        # this needs no short-circuiting to be correct.
        self._emit(Op.GET_LOCAL, step_slot, span=node.span)
        self._push()
        self._emit(Op.CONST, self._const_int(0), span=node.span)
        self._push()
        self._emit(Op.GE, span=node.span)
        self._pop()
        self._emit(Op.GET_LOCAL, counter_slot, span=node.span)
        self._push()
        self._emit(Op.GET_LOCAL, stop_slot, span=node.span)
        self._push()
        self._emit(Op.LT, span=node.span)
        self._pop()
        self._emit(Op.BAND, span=node.span)
        self._pop()
        self._emit(Op.GET_LOCAL, step_slot, span=node.span)
        self._push()
        self._emit(Op.CONST, self._const_int(0), span=node.span)
        self._push()
        self._emit(Op.LT, span=node.span)
        self._pop()
        self._emit(Op.GET_LOCAL, counter_slot, span=node.span)
        self._push()
        self._emit(Op.GET_LOCAL, stop_slot, span=node.span)
        self._push()
        self._emit(Op.GT, span=node.span)
        self._pop()
        self._emit(Op.BAND, span=node.span)
        self._pop()
        self._emit(Op.BOR, span=node.span)
        self._pop()
        exit_jump = self.chunk.emit_jump(Op.JUMP_IF_FALSE, span=node.span)
        self._pop()
        break_target = self.chunk.stack_depth  # depth exit_jump's landing point expects

        # loop variable: a fresh copy the body can freely use/shadow
        self._emit(Op.GET_LOCAL, counter_slot, span=node.span)
        self._push()
        loop_var_slot = self.chunk.stack_depth - 1

        base_depth = self.chunk.stack_depth
        self.ctx.loops.append(_LoopCtx(loop_start, break_target, base_depth))
        for s in node.body.statements:
            self._compile_stmt(s)
        self._close_locals_above(base_depth)
        loop_ctx = self.ctx.loops.pop()
        for site in loop_ctx.continue_sites:
            self.chunk.patch_jump(site)
        # pop the per-iteration loop-variable copy (CLOSE_UPVAL if captured)
        self._close_or_pop_top(node.span)
        # counter += step
        self._emit(Op.GET_LOCAL, counter_slot, span=node.span)
        self._push()
        self._emit(Op.GET_LOCAL, step_slot, span=node.span)
        self._push()
        self._emit(Op.ADD, span=node.span)
        self._pop(2)
        self._push()
        self._emit(Op.SET_LOCAL, counter_slot, span=node.span)
        self._emit(Op.POP, span=node.span)
        self._pop()
        self.chunk.emit_loop(loop_start, span=node.span)
        self.chunk.patch_jump(exit_jump)
        for site in loop_ctx.break_sites:
            self.chunk.patch_jump(site)
        # pop counter, step, stop
        self._emit(Op.POP, span=node.span)
        self._emit(Op.POP, span=node.span)
        self._emit(Op.POP, span=node.span)
        self._pop(3)
        assert self.chunk.stack_depth == counter_slot

    def _const_int(self, value: int) -> int:
        return self.const_pool.add_int(value)

    def _stmt_ForEach(self, node: A.ForEach) -> None:
        # ITER_NEXT operates in place on the iterator already on top of the
        # stack: not-done leaves [iterator, value]; done leaves [iterator]
        # untouched (doesn't consume it) and jumps to doneOff. So the
        # iterator needs exactly one persistent stack slot, never re-fetched.
        self._compile_expr(node.iterable)
        self._emit(Op.ITER_NEW, span=node.span)
        iter_slot = self.chunk.stack_depth - 1

        loop_start = self.chunk.next_offset
        break_target = self.chunk.stack_depth  # done path: just [iterator], unchanged
        done_jump = self.chunk.emit_jump(Op.ITER_NEXT, span=node.span)
        self._push()  # not-done path additionally pushes the item
        loop_var_slot = self.chunk.stack_depth - 1

        base_depth = self.chunk.stack_depth
        self.ctx.loops.append(_LoopCtx(loop_start, break_target, base_depth))
        for s in node.body.statements:
            self._compile_stmt(s)
        self._close_locals_above(base_depth)
        loop_ctx = self.ctx.loops.pop()
        for site in loop_ctx.continue_sites:
            self.chunk.patch_jump(site)
        self._close_or_pop_top(node.span)  # pop loop var (CLOSE_UPVAL if captured)
        self.chunk.emit_loop(loop_start, span=node.span)
        self.chunk.patch_jump(done_jump)
        for site in loop_ctx.break_sites:
            self.chunk.patch_jump(site)
        self._emit(Op.POP, span=node.span)  # drop the iterator itself
        self._pop()
        assert self.chunk.stack_depth == iter_slot

    def _stmt_Return(self, node: A.Return) -> None:
        if node.value is not None:
            self._compile_expr(node.value)
        else:
            self._emit(Op.GHOST, span=node.span)
            self._push()
        self._emit(Op.RETURN, span=node.span)
        self._pop()

    def _emit_unwind_to(self, target_depth: int, span) -> None:
        """Real POP/CLOSE_UPVAL for every slot above `target_depth`, without
        touching the compiler's own tracked stack_depth — `bail`/`nvm` jump
        out of the block that's still linearly being compiled, so whatever
        (unreachable) code textually follows must keep seeing the normal
        fallthrough depth, not this side-exit's."""
        captured = self.ctx.func_info.captured_slots
        for slot in range(self.chunk.stack_depth - 1, target_depth - 1, -1):
            self._emit(Op.CLOSE_UPVAL if slot in captured else Op.POP, span=span)

    def _stmt_Break(self, node: A.Break) -> None:
        loop = self.ctx.loops[-1]
        self._emit_unwind_to(loop.break_target, node.span)
        site = self.chunk.emit_jump(Op.JUMP, span=node.span)
        loop.break_sites.append(site)

    def _stmt_Continue(self, node: A.Continue) -> None:
        loop = self.ctx.loops[-1]
        self._emit_unwind_to(loop.continue_target, node.span)
        site = self.chunk.emit_jump(Op.JUMP, span=node.span)
        loop.continue_sites.append(site)

    # Sentinel meaning "not present" for TRY_PUSH's handlerOff/finallyOff —
    # a real relative offset of 0 is reachable (an empty catch body sitting
    # immediately after the try body), so 0 can't double as "absent".
    # Sentinel meaning "not present" for TRY_PUSH's handlerOff/finallyOff —
    # a real relative offset of 0 is reachable (an empty catch body sitting
    # immediately after the try body), so 0 can't double as "absent".
    _TRY_ABSENT = 0xFFFF

    def _patch_try_push(self, site: int, base_ip: int, handler_abs: int | None, finally_abs: int | None) -> None:
        handler_rel = self._TRY_ABSENT if handler_abs is None else handler_abs - base_ip
        finally_rel = self._TRY_ABSENT if finally_abs is None else finally_abs - base_ip
        code = self.chunk.code
        code[site + 1: site + 3] = handler_rel.to_bytes(2, "big")
        code[site + 3: site + 5] = finally_rel.to_bytes(2, "big")

    def _stmt_Try(self, node: A.Try) -> None:
        # PLAN.md §M5: "regardless" must run on both the normal path and the
        # exceptional (unwinding) path, so its bytecode is compiled twice.
        # Both TRY_PUSH offsets are relative to the address right after its
        # own operands (i.e. where the protected body begins) — the same
        # convention as JUMP's "ip after the instruction".
        try_base_depth = self.chunk.stack_depth
        outer_site = self.chunk.emit(Op.TRY_PUSH, 0, 0, span=node.span)
        outer_base_ip = outer_site + 5
        self._compile_scoped_block(node.body.statements)
        self._emit(Op.TRY_POP, span=node.span)
        after_try_jump = self.chunk.emit_jump(Op.JUMP, span=node.span)

        inner_site = None
        handler_abs = None
        if node.catch_body is not None:
            handler_abs = self.chunk.next_offset
            # Wrap the catch body in its own (handler-less) TRY_PUSH so that
            # if the catch body itself throws, this try's `regardless` still
            # runs before the new exception keeps propagating outward.
            if node.finally_body is not None:
                inner_site = self.chunk.emit(Op.TRY_PUSH, 0, 0, span=node.span)
            self._push()  # the caught error value, bound to catch_var's slot
            base_depth = self.chunk.stack_depth
            for s in node.catch_body.statements:
                self._compile_stmt(s)
            self._close_locals_above(base_depth)
            self._emit(Op.POP, span=node.span)  # pop the caught error binding
            self._pop()
            if inner_site is not None:
                self._emit(Op.TRY_POP, span=node.span)
        self.chunk.patch_jump(after_try_jump)

        finally_abs = None
        if node.finally_body is not None:
            # Copy 1: inline, normal-path finally — reached by falling
            # through, never by a TRY_PUSH jump, so it needs no address noted.
            self._compile_scoped_block(node.finally_body.statements)
            skip_exceptional_copy = self.chunk.emit_jump(Op.JUMP, span=node.span)
            # Copy 2: exceptional-path finally. This is what `finallyOff`
            # actually points to — TRY_PUSH's finally field is only ever
            # consulted during exception unwinding. Reached after the VM
            # truncates the stack to whichever TRY_PUSH's own recorded depth
            # (outer or inner — they differ, e.g. when the *catch* body is
            # what threw, since the outer's caught-error binding is still
            # live underneath) and pushes the pending error there, so it's
            # always exactly the top of stack when this copy starts. Nothing
            # here references an absolute slot for it — the finally body's
            # own statements are self-balancing (_compile_scoped_block), so
            # the error is still the sole top-of-stack value at the end,
            # ready for a plain CHUCK to re-throw it, unwinding continues.
            finally_abs = self.chunk.next_offset
            if inner_site is not None:
                self._patch_try_push(inner_site, inner_site + 5, None, finally_abs)
            saved_depth = self.chunk.stack_depth
            self.chunk.stack_depth = try_base_depth + 1
            self._compile_scoped_block(node.finally_body.statements)
            self._emit(Op.CHUCK, span=node.span)
            self._pop()
            self.chunk.stack_depth = saved_depth
            self.chunk.patch_jump(skip_exceptional_copy)

        self._patch_try_push(outer_site, outer_base_ip, handler_abs, finally_abs)

    def _stmt_Chuck(self, node: A.Chuck) -> None:
        self._compile_expr(node.value)
        self._emit(Op.CHUCK, span=node.span)
        self._pop()

    def _import_mode(self, node: A.Import) -> int:
        if node.names is not None:
            return 1
        if node.is_stdlib:
            return 2
        return 0

    def _stmt_Import(self, node: A.Import) -> None:
        path_idx = self._const_str(node.source)
        mode = self._import_mode(node)
        self._emit(Op.IMPORT, path_idx, mode, span=node.span)
        self._push()
        if node.names is not None:
            for name in node.names:
                self._emit(Op.DUP, span=node.span)
                self._push()
                self._emit(Op.GET_PROP, self._const_str(name), span=node.span)
                self._finish_declaration(node, name, node.span)
            self._emit(Op.POP, span=node.span)
            self._pop()
        else:
            binding = node.alias if node.alias else (node.source if node.is_stdlib else _module_stem(node.source))
            self._finish_declaration(node, binding, node.span)

    def _stmt_Export(self, node: A.Export) -> None:
        self._compile_stmt(node.decl)
        name = node.decl.name
        slot = self._binding_slot(node.decl, name)
        if slot is not None:
            self._emit(Op.GET_LOCAL, slot, span=node.span)
        else:
            self._emit(Op.GET_GLOBAL, self._const_str(name), span=node.span)
        self._push()
        self._emit(Op.EXPORT, self._const_str(name), span=node.span)
        self._emit(Op.POP, span=node.span)
        self._pop()

    def _stmt_Yap(self, node: A.Yap) -> None:
        for arg in node.args:
            self._compile_expr(arg)
        self._emit(Op.YAP, len(node.args), 1 if node.newline else 0, span=node.span)
        self._pop(len(node.args))

    def _stmt_ExprStmt(self, node: A.ExprStmt) -> None:
        self._compile_expr(node.expr)
        self._emit(Op.POP, span=node.span)
        self._pop()

    # -- expressions ------------------------------------------------------

    def _compile_expr(self, expr) -> None:
        if self.fold_constants:
            folded = _fold(expr)
            if folded is not expr:
                expr = folded
        method = getattr(self, f"_expr_{type(expr).__name__}", None)
        if method is None:
            raise TypeError(f"compiler: no handler for expression {type(expr).__name__}")
        method(expr)

    def _expr_Literal(self, node: A.Literal) -> None:
        v = node.value
        if v is None:
            self._emit(Op.GHOST, span=node.span)
        elif v is True:
            self._emit(Op.FAX, span=node.span)
        elif v is False:
            self._emit(Op.CAP, span=node.span)
        elif isinstance(v, int):
            self._emit(Op.CONST, self.const_pool.add_int(v), span=node.span)
        elif isinstance(v, float):
            self._emit(Op.CONST, self.const_pool.add_float(v), span=node.span)
        elif isinstance(v, str):
            self._emit(Op.CONST, self.const_pool.add_string(v), span=node.span)
        else:  # pragma: no cover
            raise TypeError(f"unsupported literal value {v!r}")
        self._push()

    def _expr_TemplateString(self, node: A.TemplateString) -> None:
        for kind, value in node.parts:
            if kind == "str":
                self._emit(Op.CONST, self.const_pool.add_string(value), span=node.span)
                self._push()
            else:
                self._compile_expr(value)
        self._emit(Op.BUILD_STRING, len(node.parts), span=node.span)
        self._pop(len(node.parts))
        self._push()

    def _expr_Identifier(self, node: A.Identifier) -> None:
        res = self.result.identifier_resolutions[id(node)]
        self._emit_get_resolution(res, node.span)
        self._push()

    def _emit_get_resolution(self, res, span) -> None:
        if res.kind == "local":
            self._emit(Op.GET_LOCAL, res.index, span=span)
        elif res.kind == "upvalue":
            self._emit(Op.GET_UPVAL, res.index, span=span)
        else:
            self._emit(Op.GET_GLOBAL, self._const_str(res.name), span=span)

    def _emit_set_resolution(self, res, span) -> None:
        if res.kind == "local":
            self._emit(Op.SET_LOCAL, res.index, span=span)
        elif res.kind == "upvalue":
            self._emit(Op.SET_UPVAL, res.index, span=span)
        else:
            self._emit(Op.SET_GLOBAL, self._const_str(res.name), span=span)

    def _expr_Unary(self, node: A.Unary) -> None:
        self._compile_expr(node.operand)
        self._emit(_UNOP_OPCODE[node.op], span=node.span)

    def _expr_Binary(self, node: A.Binary) -> None:
        self._compile_expr(node.left)
        self._compile_expr(node.right)
        self._emit(_BINOP_OPCODE[node.op], span=node.span)
        self._pop()

    def _expr_Logical(self, node: A.Logical) -> None:
        self._compile_expr(node.left)
        op = Op.JUMP_IF_FALSE_KEEP if node.op == "&&" else Op.JUMP_IF_TRUE_KEEP
        site = self.chunk.emit_jump(op, span=node.span)
        self._emit(Op.POP, span=node.span)
        self._pop()
        self._compile_expr(node.right)
        self.chunk.patch_jump(site)

    def _expr_Coalesce(self, node: A.Coalesce) -> None:
        self._compile_expr(node.left)
        ghost_site = self.chunk.emit_jump(Op.JUMP_IF_GHOST_KEEP, span=node.span)
        end_site = self.chunk.emit_jump(Op.JUMP, span=node.span)
        self.chunk.patch_jump(ghost_site)
        self._emit(Op.POP, span=node.span)
        self._pop()
        self._compile_expr(node.right)
        self.chunk.patch_jump(end_site)

    def _expr_Pipe(self, node: A.Pipe) -> None:
        if isinstance(node.right, A.Call):
            self._compile_expr(node.right.callee)
            self._compile_expr(node.left)
            for a in node.right.args:
                self._compile_expr(a)
            argc = len(node.right.args) + 1
            self._emit(Op.CALL, argc, span=node.span)
            self._pop(argc)
            self._push()
        else:
            self._compile_expr(node.right)
            self._compile_expr(node.left)
            self._emit(Op.CALL, 1, span=node.span)
            self._pop(1)
            self._push()

    def _expr_Ternary(self, node: A.Ternary) -> None:
        self._compile_expr(node.cond)
        else_jump = self.chunk.emit_jump(Op.JUMP_IF_FALSE, span=node.span)
        self._pop()
        baseline = self.chunk.stack_depth
        self._compile_expr(node.then_expr)
        end_jump = self.chunk.emit_jump(Op.JUMP, span=node.span)
        # then/else are alternatives, not sequential code: reset the tracked
        # depth to the shared baseline before compiling the other branch, or
        # max_stack (and anything computing slot numbers afterward) would be
        # thrown off by however deep the then-branch happened to get.
        self.chunk.stack_depth = baseline
        self.chunk.patch_jump(else_jump)
        self._compile_expr(node.else_expr)
        self.chunk.patch_jump(end_jump)

    def _expr_Assign(self, node: A.Assign) -> None:
        res = self.result.identifier_resolutions[id(node.target)]
        if node.op == "=":
            self._compile_expr(node.value)
        elif node.op == "||=":
            # `x ||= y` is exactly `x || y`'s value, then stored back into x
            # (storing x's own current value back into itself in the
            # short-circuit case is a harmless no-op write).
            self._emit_get_resolution(res, node.span)
            self._push()
            keep_site = self.chunk.emit_jump(Op.JUMP_IF_TRUE_KEEP, span=node.span)
            self._emit(Op.POP, span=node.span)
            self._pop()
            self._compile_expr(node.value)
            self.chunk.patch_jump(keep_site)
        else:
            self._emit_get_resolution(res, node.span)
            self._push()
            self._compile_expr(node.value)
            self._emit(_BINOP_OPCODE[node.op[:-1]], span=node.span)
            self._pop()
        self._emit_set_resolution(res, node.span)

    def _expr_Set(self, node: A.Set) -> None:
        self._compile_expr(node.obj)
        if node.op == "=":
            self._compile_expr(node.value)
            self._emit(Op.SET_PROP, self._const_str(node.name), span=node.span)
            self._pop()
        else:
            self._emit(Op.DUP, span=node.span)
            self._push()
            self._emit(Op.GET_PROP, self._const_str(node.name), span=node.span)
            self._compile_expr(node.value)
            self._emit(_BINOP_OPCODE[node.op[:-1]], span=node.span)
            self._pop()
            self._emit(Op.SET_PROP, self._const_str(node.name), span=node.span)
            self._pop()

    def _expr_SetIndex(self, node: A.SetIndex) -> None:
        self._compile_expr(node.obj)
        obj_slot = self.chunk.stack_depth - 1
        self._compile_expr(node.index)
        key_slot = self.chunk.stack_depth - 1
        if node.op == "=":
            self._compile_expr(node.value)
            self._emit(Op.SET_INDEX, span=node.span)
            self._pop(2)
        else:
            self._emit(Op.GET_LOCAL, obj_slot, span=node.span)
            self._push()
            self._emit(Op.GET_LOCAL, key_slot, span=node.span)
            self._push()
            self._emit(Op.GET_INDEX, span=node.span)
            self._pop()
            self._compile_expr(node.value)
            self._emit(_BINOP_OPCODE[node.op[:-1]], span=node.span)
            self._pop()
            self._emit(Op.SET_INDEX, span=node.span)
            self._pop(2)

    def _expr_Call(self, node: A.Call) -> None:
        if isinstance(node.callee, A.Get) and isinstance(node.callee.obj, A.Og):
            self._emit_me(node.span)
            for a in node.args:
                self._compile_expr(a)
            self._emit(Op.INVOKE_OG, self._const_str(node.callee.name), len(node.args), span=node.span)
            self._pop(len(node.args))
            return
        if isinstance(node.callee, A.Get):
            self._compile_expr(node.callee.obj)
            for a in node.args:
                self._compile_expr(a)
            self._emit(Op.INVOKE, self._const_str(node.callee.name), len(node.args), span=node.span)
            self._pop(len(node.args))
            return
        if isinstance(node.callee, A.SafeGet):
            self._compile_expr(node.callee.obj)
            ghost_site = self.chunk.emit_jump(Op.JUMP_IF_GHOST_KEEP, span=node.span)
            for a in node.args:
                self._compile_expr(a)
            self._emit(Op.INVOKE, self._const_str(node.callee.name), len(node.args), span=node.span)
            self._pop(len(node.args))
            self.chunk.patch_jump(ghost_site)
            return
        self._compile_expr(node.callee)
        for a in node.args:
            self._compile_expr(a)
        self._emit(Op.CALL, len(node.args), span=node.span)
        self._pop(len(node.args))

    def _emit_me(self, span) -> None:
        # `me` resolves like any captured name once squads exist (M9); until
        # then this path is unreachable (resolver blocks bare `og`/`me`).
        self._emit(Op.GET_LOCAL, 0, span=span)
        self._push()

    def _expr_Index(self, node: A.Index) -> None:
        self._compile_expr(node.obj)
        self._compile_expr(node.index)
        self._emit(Op.GET_INDEX, span=node.span)
        self._pop()

    def _expr_Slice(self, node: A.Slice) -> None:
        self._compile_expr(node.obj)
        for part in (node.start, node.stop, node.step):
            if part is None:
                self._emit(Op.GHOST, span=node.span)
                self._push()
            else:
                self._compile_expr(part)
        self._emit(Op.GET_SLICE, span=node.span)
        self._pop(3)

    def _expr_Get(self, node: A.Get) -> None:
        self._compile_expr(node.obj)
        self._emit(Op.GET_PROP, self._const_str(node.name), span=node.span)

    def _expr_SafeGet(self, node: A.SafeGet) -> None:
        # A plain `?.` read is exactly what GET_PROP_SAFE is for (op 18):
        # one opcode, no jump. The jump-based JUMP_IF_GHOST_KEEP dance is
        # still needed for `obj?.method(args)` (see _expr_Call), since that
        # also has to skip the call itself, not just the property read.
        self._compile_expr(node.obj)
        self._emit(Op.GET_PROP_SAFE, self._const_str(node.name), span=node.span)

    def _expr_StashLit(self, node: A.StashLit) -> None:
        for el in node.elements:
            self._compile_expr(el)
        self._emit(Op.BUILD_STASH, len(node.elements), span=node.span)
        self._pop(len(node.elements))
        self._push()

    def _expr_GroupChatLit(self, node: A.GroupChatLit) -> None:
        for k, v in node.pairs:
            self._compile_expr(k)
            self._compile_expr(v)
        self._emit(Op.BUILD_GROUPCHAT, len(node.pairs), span=node.span)
        self._pop(2 * len(node.pairs))
        self._push()

    def _expr_Lambda(self, node: A.Lambda) -> None:
        if node.is_expr_body:
            body_statements = (A.Return(node.body, node.body.span),)
        else:
            body_statements = node.body.statements
        self._compile_closure(node, node.params, node.variadic, body_statements, "<lowkey>", node.span)

    def _expr_Me(self, node: A.Me) -> None:
        self._emit_me(node.span)

    def _expr_Og(self, node: A.Og) -> None:
        self._emit_me(node.span)

    # -- closures -----------------------------------------------------------

    def _compile_closure(self, node, params, variadic, body_statements, name, span, is_method: bool = False) -> None:
        func_info = self.result.func_info[id(node)]
        default_count = sum(1 for p in params if p.default is not None)
        # A method's real arity (for the VM's argc check) counts the implicit
        # receiver too: every calling convention that reaches a method
        # (BoundMethod, INVOKE, INVOKE_OG) always prepends it, so the VM
        # should validate argc against "params + receiver", uniformly, with
        # no special-casing needed on the calling side.
        arity = len(params) + (1 if is_method else 0)
        chunk = Chunk(name, arity, default_count, variadic is not None, self.const_pool)
        chunk.local_count = func_info.local_count
        chunk.upvalue_count = len(func_info.upvalues)
        self.stack.append(_FuncCtx(chunk, func_info))
        slot_offset = 0
        if is_method:
            self._push()  # slot 0 reserved for `me` (matches resolver's "$me")
            slot_offset = 1
        for _ in params:
            self._push()  # each parameter occupies the next slot, in order
        if variadic is not None:
            self._push()
        self._emit_param_defaults(params, span, slot_offset)
        for stmt in body_statements:
            self._compile_stmt(stmt)
        self._emit(Op.GHOST, span=span)
        self._push()
        self._emit(Op.RETURN, span=span)
        self._pop()
        proto = chunk.finish()
        self.stack.pop()
        proto_idx = self._add_proto(proto)
        const_idx = self.const_pool.add_proto_ref(proto_idx)
        self.chunk.emit_closure(const_idx, func_info.upvalues, span=span)
        self._push()

    def _emit_param_defaults(self, params, span, slot_offset: int = 0) -> None:
        for i, p in enumerate(params):
            if p.default is None:
                continue
            slot = i + slot_offset
            self._emit(Op.GET_LOCAL, slot, span=p.span)
            self._push()
            self._emit(Op.GHOST, span=p.span)
            self._push()
            self._emit(Op.EQ, span=p.span)
            self._pop(2)
            self._push()
            skip_site = self.chunk.emit_jump(Op.JUMP_IF_FALSE, span=p.span)
            self._pop()
            self._compile_expr(p.default)
            self._emit(Op.SET_LOCAL, slot, span=p.span)
            self._emit(Op.POP, span=p.span)
            self._pop()
            self.chunk.patch_jump(skip_site)


def _module_stem(path: str) -> str:
    from pathlib import PurePosixPath

    return PurePosixPath(path.replace("\\", "/")).stem


# -- constant folding (PLAN.md §M4 task 3) ---------------------------------

_NUMERIC = (int, float)


def _fold(expr):
    if isinstance(expr, A.Unary) and expr.op in ("-", "~"):
        inner = _fold(expr.operand)
        if isinstance(inner, A.Literal) and isinstance(inner.value, _NUMERIC) and not isinstance(inner.value, bool):
            value = -inner.value if expr.op == "-" else ~int(inner.value)
            return A.Literal(value, expr.span)
        return expr if inner is expr.operand else A.Unary(expr.op, inner, expr.span)
    if isinstance(expr, A.Binary):
        left = _fold(expr.left)
        right = _fold(expr.right)
        if (
            isinstance(left, A.Literal)
            and isinstance(right, A.Literal)
            and isinstance(left.value, _NUMERIC)
            and isinstance(right.value, _NUMERIC)
            and not isinstance(left.value, bool)
            and not isinstance(right.value, bool)
            and expr.op in _FOLDABLE_NUMERIC
        ):
            try:
                value = _apply_numeric(expr.op, left.value, right.value)
            except ZeroDivisionError:
                return _rebuild_binary(expr, left, right)
            return A.Literal(value, expr.span)
        return _rebuild_binary(expr, left, right)
    return expr


def _rebuild_binary(expr: A.Binary, left, right):
    if left is expr.left and right is expr.right:
        return expr
    return A.Binary(expr.op, left, right, expr.span)


def _apply_numeric(op: str, a, b):
    if op == "+":
        return a + b
    if op == "-":
        return a - b
    if op == "*":
        return a * b
    if op == "|":
        return int(a) | int(b)
    if op == "^":
        return int(a) ^ int(b)
    if op == "&":
        return int(a) & int(b)
    if op == "<<":
        return int(a) << int(b)
    if op == ">>":
        return int(a) >> int(b)
    if op == "==":
        return a == b
    if op == "!=":
        return a != b
    if op == "<":
        return a < b
    if op == "<=":
        return a <= b
    if op == ">":
        return a > b
    if op == ">=":
        return a >= b
    raise AssertionError(op)  # pragma: no cover


def compile_program(program: A.Program, resolver_result, source=None, source_name: str = "<script>") -> CompiledUnit:
    return Compiler(resolver_result, source).compile_program(program, source_name)
