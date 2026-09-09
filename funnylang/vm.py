"""The stack VM (PLAN.md §M5 — "the heart")."""
from __future__ import annotations

import sys

from .chunk import TAG_FLOAT, TAG_INT, TAG_PROTO_REF, TAG_STRING
from .errors import (
    FunnyError, GhostError, KeyGhosted, MathAintMathin, NotACallableRizz,
    OutOfPocket, SkillIssue, TooDeepBro, TypeVibeMismatch, WhoDis,
    WrongNumberOfHomies,
)
from .opcodes import Op
from .values import (
    GHOST, BoundMethod, Closure, GroupChat, Instance, Iterator, Module,
    NativeFn, Squad, Stash, Upvalue, funny_eq, is_truthy, to_display,
    type_name,
)

# Instance-method tables for primitive types (PLAN.md §3.9) — plain funcs
# living in the stdlib modules so the free-function and method-call forms
# share one implementation. No cycle: these modules only import errors/values.
from .stdlib.groupchat import METHODS as _GROUPCHAT_METHODS
from .stdlib.mafs import NUMBA_METHODS as _NUMBA_METHODS
from .stdlib.stash import METHODS as _STASH_METHODS
from .stdlib.yapper import YAPSTRING_METHODS as _YAPSTRING_METHODS

MAX_FRAMES = 10_000
ABSENT = 0xFFFF

sys.setrecursionlimit(max(20_000, sys.getrecursionlimit()))


def _is_num(v) -> bool:
    return isinstance(v, (int, float)) and not isinstance(v, bool)


def _is_int_like(v) -> bool:
    return isinstance(v, int)  # bool is a subclass; intentional (see PLAN.md §16)


class _Handler:
    __slots__ = ("handler_ip", "finally_ip", "stack_depth")

    def __init__(self, handler_ip: int, finally_ip: int, stack_depth: int):
        self.handler_ip = handler_ip
        self.finally_ip = finally_ip
        self.stack_depth = stack_depth


class Frame:
    __slots__ = ("closure", "ip", "slot_base", "handlers")

    def __init__(self, closure: Closure, slot_base: int):
        self.closure = closure
        self.ip = 0
        self.slot_base = slot_base
        self.handlers: list[_Handler] = []


class VM:
    def __init__(self, stdout=None):
        self.stack: list = []
        self.frames: list[Frame] = []
        self.open_upvalues: list[Upvalue] = []
        # Shared, read-only-from-user-code globals (builtins + nothing
        # else) — every module's own globals (Closure.module_globals) are
        # isolated from each other, per §3.8's "non-flexed names are
        # private". install_stdlib() populates this.
        self.builtins: dict[str, object] = {}
        self.stdout = stdout if stdout is not None else sys.stdout
        self.source = None
        self.module_loader = None  # wired up in M7
        self.module_resolver = None  # wired up in M7 (install_stdlib)

    # -- entry points -------------------------------------------------------

    def interpret(self, unit, source=None):
        self.source = source
        entry_path = getattr(source, "path", None)
        if self.module_resolver is not None:
            self.module_resolver.enter(entry_path)
        try:
            closure = self._make_entry_closure(unit)
            return self.call_value(closure, [])
        finally:
            if self.module_resolver is not None:
                self.module_resolver.exit(entry_path)

    def _make_entry_closure(self, unit, module_globals=None, module_exports=None) -> Closure:
        entry = unit.protos[unit.entry_proto]
        return Closure(
            entry, [], const_pool=unit.const_pool, protos=unit.protos,
            module_globals=module_globals, module_exports=module_exports,
        )

    def run_repl_unit(self, unit, source, module_globals: dict, module_exports: dict):
        """Like interpret(), but reuses the given (persistent) globals dicts
        instead of starting fresh each time — funny vibe's "persistent global
        scope across inputs"."""
        self.source = source
        closure = self._make_entry_closure(unit, module_globals, module_exports)
        return self.call_value(closure, [])

    def run_module(self, unit, source, module_name: str):
        """Runs a freshly-compiled file's top-level code once, in its own
        isolated global namespace, and returns a Module of its `flex`ed
        exports (PLAN.md §M7 task 3)."""
        prior_source = self.source
        closure = self._make_entry_closure(unit)
        try:
            self.source = source
            self.call_value(closure, [])
        finally:
            self.source = prior_source
        return Module(module_name, dict(closure.module_exports))

    def call_value(self, callee, args: list):
        if isinstance(callee, NativeFn):
            self._check_native_arity(callee, args)
            return callee.fn(self, list(args))
        if isinstance(callee, Closure):
            self._push_closure_frame(callee, list(args))
            return self._run(len(self.frames) - 1)
        if isinstance(callee, BoundMethod):
            return self.call_value(callee.method, [callee.receiver, *args])
        if isinstance(callee, Squad):
            return self._construct(callee, list(args))
        raise NotACallableRizz(
            f"'{type_name(callee)}' is not callable.",
            roast="that thing has no call rizz whatsoever.",
        )

    def _check_native_arity(self, fn: NativeFn, args: list) -> None:
        n = len(args)
        if not (fn.arity_min <= n <= fn.arity_max):
            want = fn.arity_min if fn.arity_min == fn.arity_max else f"{fn.arity_min}-{fn.arity_max}"
            raise WrongNumberOfHomies(
                f"'{fn.name}' wants {want} args, got {n}.",
                roast=f"`{fn.name}` wanted {want} args. you brought {n}. awkward.",
            )

    def _push_closure_frame(self, closure: Closure, args: list) -> None:
        if len(self.frames) >= MAX_FRAMES:
            raise TooDeepBro(
                f"recursed {len(self.frames)} deep.",
                roast=f"you recursed {len(self.frames)} deep. touch grass.",
            )
        proto = closure.proto
        argc = len(args)
        arity = proto.arity
        min_required = arity - proto.default_count
        if proto.is_variadic:
            if argc < min_required:
                raise self._arity_error(proto, argc)
            fixed = args[:arity]
            while len(fixed) < arity:
                fixed.append(GHOST)
            rest = Stash(args[arity:] if argc > arity else [])
            frame_args = fixed + [rest]
        else:
            if argc > arity or argc < min_required:
                raise self._arity_error(proto, argc)
            frame_args = list(args)
            while len(frame_args) < arity:
                frame_args.append(GHOST)
        slot_base = len(self.stack)
        self.stack.extend(frame_args)
        self.frames.append(Frame(closure, slot_base))

    def _arity_error(self, proto, argc: int) -> WrongNumberOfHomies:
        want = proto.arity - proto.default_count
        if proto.default_count or proto.is_variadic:
            want_str = f"at least {want}"
        else:
            want_str = str(want)
        return WrongNumberOfHomies(
            f"'{proto.name}' wants {want_str} args, got {argc}.",
            roast=f"`{proto.name}` wanted {want_str} args. you brought {argc}. awkward.",
        )

    def _construct(self, squad: Squad, args: list):
        instance = Instance(squad)
        # A subclass with no spawn of its own inherits the nearest
        # ancestor's — find_method walks the superclass chain, unlike the
        # dedicated `.spawn` field (which only ever holds *this* squad's own).
        spawn = squad.find_method("spawn")
        if spawn is not None:
            self.call_value(BoundMethod(instance, spawn), args)
        return instance

    # -- constants ----------------------------------------------------------

    def _const(self, frame: Frame, idx: int):
        tag, value = frame.closure.const_pool.entries[idx]
        return value

    def _const_str(self, frame: Frame, idx: int) -> str:
        return frame.closure.const_pool.entries[idx][1]

    # -- upvalues -------------------------------------------------------

    def _capture_upvalue(self, slot: int) -> Upvalue:
        for uv in self.open_upvalues:
            if not uv.closed and uv.slot == slot:
                return uv
        uv = Upvalue(self.stack, slot)
        self.open_upvalues.append(uv)
        return uv

    def _close_upvalues_from(self, min_slot: int) -> None:
        remaining = []
        for uv in self.open_upvalues:
            if not uv.closed and uv.slot >= min_slot:
                uv.close()
            else:
                remaining.append(uv)
        self.open_upvalues = remaining

    # -- the dispatch loop ------------------------------------------------

    def _run(self, base_frame_count: int):
        stack = self.stack
        while True:
            frame = self.frames[-1]
            code = frame.closure.proto.code
            instr_start = frame.ip
            op = code[frame.ip]
            ip = frame.ip + 1
            try:
                if op == Op.CONST:
                    idx = (code[ip] << 8) | code[ip + 1]
                    stack.append(self._const(frame, idx))
                    frame.ip = ip + 2
                elif op == Op.GET_LOCAL:
                    stack.append(stack[frame.slot_base + code[ip]])
                    frame.ip = ip + 1
                elif op == Op.SET_LOCAL:
                    stack[frame.slot_base + code[ip]] = stack[-1]
                    frame.ip = ip + 1
                elif op == Op.ADD:
                    b = stack.pop(); a = stack[-1]; stack[-1] = self._add(a, b)
                    frame.ip = ip
                elif op == Op.SUB:
                    b = stack.pop(); a = stack[-1]
                    self._check_num2(a, b, "-")
                    stack[-1] = a - b
                    frame.ip = ip
                elif op == Op.MUL:
                    b = stack.pop(); a = stack[-1]; stack[-1] = self._mul(a, b)
                    frame.ip = ip
                elif op == Op.DIV:
                    b = stack.pop(); a = stack[-1]
                    self._check_num2(a, b, "/")
                    if b == 0:
                        raise MathAintMathin("division by zero.", roast="you divided by zero. the universe said no.")
                    stack[-1] = a / b
                    frame.ip = ip
                elif op == Op.IDIV:
                    b = stack.pop(); a = stack[-1]
                    self._check_num2(a, b, "\\")
                    if b == 0:
                        raise MathAintMathin("division by zero.", roast="you divided by zero. the universe said no.")
                    stack[-1] = a // b
                    frame.ip = ip
                elif op == Op.MOD:
                    b = stack.pop(); a = stack[-1]
                    self._check_num2(a, b, "%")
                    if b == 0:
                        raise MathAintMathin("mod by zero.", roast="you divided by zero. the universe said no.")
                    stack[-1] = a % b
                    frame.ip = ip
                elif op == Op.POW:
                    b = stack.pop(); a = stack[-1]
                    self._check_num2(a, b, "**")
                    try:
                        stack[-1] = a ** b
                    except ZeroDivisionError:
                        raise MathAintMathin("0 to a negative power.", roast="you divided by zero. the universe said no.")
                    frame.ip = ip
                elif op == Op.NEG:
                    a = stack[-1]
                    if not _is_num(a):
                        raise TypeVibeMismatch(f"can't negate a {type_name(a)}.", roast=f"you can't negate a {type_name(a)}. that's not a numba.")
                    stack[-1] = -a
                    frame.ip = ip
                elif op == Op.NOT:
                    stack[-1] = not is_truthy(stack[-1])
                    frame.ip = ip
                elif op == Op.BNOT:
                    a = stack[-1]
                    if not _is_int_like(a):
                        raise TypeVibeMismatch(f"can't bitwise-not a {type_name(a)}.")
                    stack[-1] = ~a
                    frame.ip = ip
                elif op in (Op.BAND, Op.BOR, Op.BXOR, Op.SHL, Op.SHR):
                    b = stack.pop(); a = stack[-1]
                    stack[-1] = self._bitwise(op, a, b)
                    frame.ip = ip
                elif op == Op.EQ:
                    b = stack.pop(); a = stack[-1]; stack[-1] = funny_eq(a, b, self)
                    frame.ip = ip
                elif op == Op.NEQ:
                    b = stack.pop(); a = stack[-1]; stack[-1] = not funny_eq(a, b, self)
                    frame.ip = ip
                elif op in (Op.LT, Op.LE, Op.GT, Op.GE):
                    b = stack.pop(); a = stack[-1]
                    stack[-1] = self._compare(op, a, b)
                    frame.ip = ip
                elif op == Op.IN:
                    b = stack.pop(); a = stack[-1]; stack[-1] = self._contains(a, b)
                    frame.ip = ip
                elif op == Op.JUMP:
                    off = (code[ip] << 8) | code[ip + 1]
                    frame.ip = ip + 2 + off
                elif op == Op.LOOP:
                    off = (code[ip] << 8) | code[ip + 1]
                    frame.ip = ip + 2 - off
                elif op == Op.JUMP_IF_FALSE:
                    off = (code[ip] << 8) | code[ip + 1]
                    cond = stack.pop()
                    frame.ip = (ip + 2 + off) if not is_truthy(cond) else ip + 2
                elif op == Op.JUMP_IF_TRUE:
                    off = (code[ip] << 8) | code[ip + 1]
                    cond = stack.pop()
                    frame.ip = (ip + 2 + off) if is_truthy(cond) else ip + 2
                elif op == Op.JUMP_IF_FALSE_KEEP:
                    off = (code[ip] << 8) | code[ip + 1]
                    frame.ip = (ip + 2 + off) if not is_truthy(stack[-1]) else ip + 2
                elif op == Op.JUMP_IF_TRUE_KEEP:
                    off = (code[ip] << 8) | code[ip + 1]
                    frame.ip = (ip + 2 + off) if is_truthy(stack[-1]) else ip + 2
                elif op == Op.JUMP_IF_GHOST_KEEP:
                    off = (code[ip] << 8) | code[ip + 1]
                    frame.ip = (ip + 2 + off) if stack[-1] is GHOST else ip + 2
                elif op == Op.JUMP_LONG:
                    off = int.from_bytes(code[ip:ip + 4], "big")
                    frame.ip = ip + 4 + off
                elif op == Op.LOOP_LONG:
                    off = int.from_bytes(code[ip:ip + 4], "big")
                    frame.ip = ip + 4 - off
                elif op == Op.POP:
                    stack.pop()
                    frame.ip = ip
                elif op == Op.DUP:
                    stack.append(stack[-1])
                    frame.ip = ip
                elif op == Op.SWAP:
                    stack[-1], stack[-2] = stack[-2], stack[-1]
                    frame.ip = ip
                elif op == Op.GHOST:
                    stack.append(GHOST)
                    frame.ip = ip
                elif op == Op.FAX:
                    stack.append(True)
                    frame.ip = ip
                elif op == Op.CAP:
                    stack.append(False)
                    frame.ip = ip
                elif op == Op.GET_GLOBAL:
                    idx = (code[ip] << 8) | code[ip + 1]
                    name = self._const_str(frame, idx)
                    mg = frame.closure.module_globals
                    if name in mg:
                        stack.append(mg[name])
                    elif name in self.builtins:
                        stack.append(self.builtins[name])
                    else:
                        from .errors import suggest_name
                        hint = suggest_name(name, list(mg.keys()) + list(self.builtins.keys()))
                        roast = f"`{name}` who? never heard of them."
                        if hint:
                            roast += f" did you mean `{hint}`?"
                        raise WhoDis(
                            f"'{name}' isn't defined.",
                            roast=roast,
                            hint=(f"did you mean `{hint}`?" if hint else None),
                            span=_FakeSpan(*frame.closure.proto.line_for_offset(frame.ip)),
                            source=self.source,
                        )
                    frame.ip = ip + 2
                elif op == Op.SET_GLOBAL:
                    idx = (code[ip] << 8) | code[ip + 1]
                    frame.closure.module_globals[self._const_str(frame, idx)] = stack[-1]
                    frame.ip = ip + 2
                elif op == Op.DEF_GLOBAL:
                    idx = (code[ip] << 8) | code[ip + 1]
                    frame.closure.module_globals[self._const_str(frame, idx)] = stack.pop()
                    frame.ip = ip + 2
                elif op == Op.GET_UPVAL:
                    stack.append(frame.closure.upvalues[code[ip]].get())
                    frame.ip = ip + 1
                elif op == Op.SET_UPVAL:
                    frame.closure.upvalues[code[ip]].set(stack[-1])
                    frame.ip = ip + 1
                elif op == Op.CLOSE_UPVAL:
                    self._close_upvalues_from(len(stack) - 1)
                    stack.pop()
                    frame.ip = ip
                elif op == Op.GET_PROP:
                    idx = (code[ip] << 8) | code[ip + 1]
                    name = self._const_str(frame, idx)
                    obj = stack.pop()
                    stack.append(self._get_prop(obj, name))
                    frame.ip = ip + 2
                elif op == Op.GET_PROP_SAFE:
                    idx = (code[ip] << 8) | code[ip + 1]
                    name = self._const_str(frame, idx)
                    obj = stack.pop()
                    stack.append(GHOST if obj is GHOST else self._get_prop(obj, name))
                    frame.ip = ip + 2
                elif op == Op.SET_PROP:
                    idx = (code[ip] << 8) | code[ip + 1]
                    name = self._const_str(frame, idx)
                    value = stack.pop()
                    obj = stack.pop()
                    self._set_prop(obj, name, value)
                    stack.append(value)
                    frame.ip = ip + 2
                elif op == Op.GET_INDEX:
                    key = stack.pop(); obj = stack.pop()
                    stack.append(self._get_index(obj, key))
                    frame.ip = ip
                elif op == Op.SET_INDEX:
                    value = stack.pop(); key = stack.pop(); obj = stack.pop()
                    self._set_index(obj, key, value)
                    stack.append(value)
                    frame.ip = ip
                elif op == Op.GET_SLICE:
                    step = stack.pop(); stop = stack.pop(); start = stack.pop(); obj = stack.pop()
                    stack.append(self._get_slice(obj, start, stop, step))
                    frame.ip = ip
                elif op == Op.CALL:
                    argc = code[ip]
                    frame.ip = ip + 1
                    self._do_call(argc)
                elif op == Op.INVOKE:
                    idx = (code[ip] << 8) | code[ip + 1]
                    argc = code[ip + 2]
                    name = self._const_str(frame, idx)
                    frame.ip = ip + 3
                    self._do_invoke(name, argc)
                elif op == Op.INVOKE_OG:
                    idx = (code[ip] << 8) | code[ip + 1]
                    argc = code[ip + 2]
                    name = self._const_str(frame, idx)
                    frame.ip = ip + 3
                    self._do_invoke_og(frame, name, argc)
                elif op == Op.CLOSURE:
                    const_idx = (code[ip] << 8) | code[ip + 1]
                    pos = ip + 2
                    tag, proto_idx = frame.closure.const_pool.entries[const_idx]
                    proto = frame.closure.protos[proto_idx]
                    upvalues = []
                    for _ in range(proto.upvalue_count):
                        is_local = code[pos]; uv_idx = code[pos + 1]
                        pos += 2
                        if is_local:
                            upvalues.append(self._capture_upvalue(frame.slot_base + uv_idx))
                        else:
                            upvalues.append(frame.closure.upvalues[uv_idx])
                    stack.append(Closure(
                        proto, upvalues,
                        const_pool=frame.closure.const_pool, protos=frame.closure.protos,
                        module_globals=frame.closure.module_globals, module_exports=frame.closure.module_exports,
                    ))
                    frame.ip = pos
                elif op == Op.RETURN:
                    value = stack.pop()
                    self._close_upvalues_from(frame.slot_base)
                    del stack[frame.slot_base:]
                    self.frames.pop()
                    if len(self.frames) == base_frame_count:
                        return value
                    stack.append(value)
                elif op == Op.BUILD_STASH:
                    n = (code[ip] << 8) | code[ip + 1]
                    if n:
                        items = stack[-n:]
                        del stack[-n:]
                    else:
                        items = []
                    stack.append(Stash(items))
                    frame.ip = ip + 2
                elif op == Op.BUILD_GROUPCHAT:
                    n = (code[ip] << 8) | code[ip + 1]
                    items = {}
                    if n:
                        flat = stack[-2 * n:]
                        del stack[-2 * n:]
                        for i in range(n):
                            items[flat[2 * i]] = flat[2 * i + 1]
                    stack.append(GroupChat(items))
                    frame.ip = ip + 2
                elif op == Op.BUILD_STRING:
                    n = (code[ip] << 8) | code[ip + 1]
                    parts = stack[-n:] if n else []
                    if n:
                        del stack[-n:]
                    stack.append("".join(to_display(p, self) for p in parts))
                    frame.ip = ip + 2
                elif op == Op.YAP:
                    argc = code[ip]
                    newline = code[ip + 1]
                    args = stack[-argc:] if argc else []
                    if argc:
                        del stack[-argc:]
                    text = " ".join(to_display(a, self) for a in args)
                    self.stdout.write(text + ("\n" if newline else ""))
                    frame.ip = ip + 2
                elif op == Op.CHUCK:
                    payload = stack.pop()
                    raise self._make_thrown(payload, frame, ip - 1)
                elif op == Op.TRY_PUSH:
                    handler_off = (code[ip] << 8) | code[ip + 1]
                    finally_off = (code[ip + 2] << 8) | code[ip + 3]
                    base = ip + 4
                    handler_ip = ABSENT if handler_off == ABSENT else base + handler_off
                    finally_ip = ABSENT if finally_off == ABSENT else base + finally_off
                    frame.handlers.append(_Handler(handler_ip, finally_ip, len(stack)))
                    frame.ip = base
                elif op == Op.TRY_POP:
                    frame.handlers.pop()
                    frame.ip = ip
                elif op == Op.ITER_NEW:
                    stack[-1] = Iterator(self._make_iter_gen(stack[-1]))
                    frame.ip = ip
                elif op == Op.ITER_NEXT:
                    off = (code[ip] << 8) | code[ip + 1]
                    it = stack[-1]
                    try:
                        stack.append(next(it.gen))
                        frame.ip = ip + 2
                    except StopIteration:
                        frame.ip = ip + 2 + off
                elif op == Op.IMPORT:
                    idx = (code[ip] << 8) | code[ip + 1]
                    mode = code[ip + 2]
                    path = self._const_str(frame, idx)
                    stack.append(self._do_import(path, mode))
                    frame.ip = ip + 3
                elif op == Op.EXPORT:
                    idx = (code[ip] << 8) | code[ip + 1]
                    frame.closure.module_exports[self._const_str(frame, idx)] = stack[-1]
                    frame.ip = ip + 2
                elif op == Op.SQUAD:
                    idx = (code[ip] << 8) | code[ip + 1]
                    stack.append(Squad(self._const_str(frame, idx)))
                    frame.ip = ip + 2
                elif op == Op.METHOD:
                    idx = (code[ip] << 8) | code[ip + 1]
                    name = self._const_str(frame, idx)
                    method = stack.pop()
                    squad = stack[-1]
                    method.home_squad = squad
                    # Always in `methods` too (not just the dedicated `.spawn`
                    # field `_construct` uses) so `find_method("spawn")` finds
                    # it — needed for `og.spawn(...)` and direct calls alike.
                    squad.methods[name] = method
                    if name == "spawn":
                        squad.spawn = method
                    frame.ip = ip + 2
                elif op == Op.INHERIT:
                    sup = stack[-2]; sub = stack[-1]
                    sub.superclass = sup
                    frame.ip = ip
                elif op == Op.NOP:
                    frame.ip = ip
                elif op == Op.HALT:
                    return GHOST
                else:  # pragma: no cover
                    raise AssertionError(f"unimplemented opcode {op!r}")
            except FunnyError as err:
                if err.span is None:
                    line, col = frame.closure.proto.line_for_offset(instr_start)
                    err.span = _FakeSpan(line, col)
                if err.source is None:
                    err.source = self.source
                if not err.frames:
                    err.frames = self._build_trace(frame, instr_start)
                if self._unwind(err, base_frame_count):
                    continue
                raise

    # -- error unwinding ----------------------------------------------------

    def _build_trace(self, top_frame: Frame, top_ip: int) -> list[str]:
        path = getattr(self.source, "path", None) or "<unknown>"
        lines = []
        for f in reversed(self.frames):
            lookup_ip = top_ip if f is top_frame else max(f.ip - 1, 0)
            line, _col = f.closure.proto.line_for_offset(lookup_ip)
            name = f.closure.proto.name
            label = "<the big one>" if name == "<script>" else f"{name}()"
            lines.append(f"at {label}  {path}:{line}")
        return lines

    def _make_thrown(self, payload, frame, chuck_ip) -> FunnyError:
        if isinstance(payload, FunnyError):
            return payload
        line, col = frame.closure.proto.line_for_offset(chuck_ip)
        return SkillIssue(
            to_display(payload, self),
            roast="skill issue.",
            payload=payload,
            span=_FakeSpan(line, col),
            source=self.source,
        )

    def _unwind(self, err: FunnyError, base_frame_count: int) -> bool:
        while len(self.frames) > base_frame_count:
            frame = self.frames[-1]
            if frame.handlers:
                handler = frame.handlers.pop()
                self._close_upvalues_from(handler.stack_depth)
                del self.stack[handler.stack_depth:]
                if handler.handler_ip != ABSENT:
                    self.stack.append(err)
                    frame.ip = handler.handler_ip
                    return True
                if handler.finally_ip != ABSENT:
                    self.stack.append(err)
                    frame.ip = handler.finally_ip
                    return True
                continue
            self._close_upvalues_from(frame.slot_base)
            del self.stack[frame.slot_base:]
            self.frames.pop()
        return False

    # -- property / index / slice --------------------------------------

    def _get_prop(self, obj, name: str):
        if isinstance(obj, Module):
            if name not in obj.members:
                raise WhoDis(
                    f"'{name}' isn't exported by module '{obj.name}'.",
                    roast=f"`{name}` isn't flexed. it's shy.",
                )
            return obj.members[name]
        if isinstance(obj, Instance):
            if name in obj.fields:
                return obj.fields[name]
            method = obj.squad.find_method(name)
            if method is not None:
                return BoundMethod(obj, method)
            return GHOST
        if isinstance(obj, Squad):
            method = obj.methods.get(name)
            if method is not None:
                return method
            raise WhoDis(f"'{obj.name}' doesn't do '{name}'.", roast=f"`{obj.name}` doesn't do `{name}`. that's not its thing.")
        if isinstance(obj, FunnyError):
            return self._error_field(obj, name)
        if isinstance(obj, Stash):
            return self._bind_native_method(obj, name, _STASH_METHODS, "stash")
        if isinstance(obj, GroupChat):
            return self._bind_native_method(obj, name, _GROUPCHAT_METHODS, "groupchat")
        if isinstance(obj, str):
            return self._bind_native_method(obj, name, _YAPSTRING_METHODS, "yapstring")
        if isinstance(obj, (int, float)) and not isinstance(obj, bool):
            return self._bind_native_method(obj, name, _NUMBA_METHODS, "numba")
        if obj is GHOST:
            raise GhostError(f"can't read '{name}' off ghost.", roast="you're talking to a ghost, king.")
        raise WhoDis(
            f"a {type_name(obj)} doesn't have '{name}' (yet).",
            roast=f"`{name}` who? never heard of them.",
        )

    def _bind_native_method(self, obj, name: str, table: dict, type_label: str) -> NativeFn:
        fn = table.get(name)
        if fn is None:
            raise WhoDis(
                f"a {type_label} doesn't have '{name}'.",
                roast=f"`{name}` who? never heard of them.",
            )
        return NativeFn(name, lambda vm, args, _fn=fn, _obj=obj: _fn(vm, [_obj, *args]), 0, 255)

    def _set_prop(self, obj, name: str, value) -> None:
        if isinstance(obj, Instance):
            obj.fields[name] = value
            return
        if obj is GHOST:
            raise GhostError(f"can't set '{name}' on ghost.", roast="you're talking to a ghost, king.")
        raise TypeVibeMismatch(f"can't set properties on a {type_name(obj)}.")

    def _error_field(self, err: FunnyError, name: str):
        if name == "flavor":
            return err.flavor
        if name == "message":
            return err.message
        if name == "line":
            return err.span.line if err.span else 0
        if name == "col":
            return err.span.col if err.span else 0
        if name == "file":
            return getattr(err.source, "path", "") if err.source else ""
        if name == "trace":
            return Stash(list(err.frames))
        if name == "payload":
            return err.payload if err.payload is not None else GHOST
        raise WhoDis(f"error objects don't have '{name}'.", roast=f"`{name}` who? never heard of them.")

    def _get_index(self, obj, key):
        if isinstance(obj, Stash):
            if not _is_int_like(key):
                raise TypeVibeMismatch(f"can't index a stash with a {type_name(key)}.")
            n = len(obj.items)
            idx = key + n if key < 0 else key
            if idx < 0 or idx >= n:
                raise OutOfPocket(
                    f"index {key} on a stash of {n}.",
                    roast=f"index {key} on a stash of {n}. that's straight up out of pocket.",
                )
            return obj.items[idx]
        if isinstance(obj, str):
            if not _is_int_like(key):
                raise TypeVibeMismatch(f"can't index a yapstring with a {type_name(key)}.")
            n = len(obj)
            idx = key + n if key < 0 else key
            if idx < 0 or idx >= n:
                raise OutOfPocket(
                    f"index {key} on a yapstring of length {n}.",
                    roast=f"index {key} on a stash of {n}. that's straight up out of pocket.",
                )
            return obj[idx]
        if isinstance(obj, GroupChat):
            if key not in obj.items:
                raise KeyGhosted(f"key '{key}' not found.", roast=f"key `{key}` left the group chat.")
            return obj.items[key]
        if isinstance(obj, Instance):
            method = obj.squad.find_method("get_it")
            if method is not None:
                return self.call_value(method, [obj, key])
            raise WhoDis(f"'{obj.squad.name}' doesn't do 'get_it'.", roast=f"`{obj.squad.name}` doesn't do `get_it`. that's not its thing.")
        if obj is GHOST:
            raise GhostError("can't index ghost.", roast="you're talking to a ghost, king.")
        raise TypeVibeMismatch(f"can't index a {type_name(obj)}.")

    def _set_index(self, obj, key, value) -> None:
        if isinstance(obj, Stash):
            if not _is_int_like(key):
                raise TypeVibeMismatch(f"can't index a stash with a {type_name(key)}.")
            n = len(obj.items)
            idx = key + n if key < 0 else key
            if idx < 0 or idx >= n:
                raise OutOfPocket(
                    f"index {key} on a stash of {n}.",
                    roast=f"index {key} on a stash of {n}. that's straight up out of pocket.",
                )
            obj.items[idx] = value
            return
        if isinstance(obj, GroupChat):
            obj.items[key] = value
            return
        if isinstance(obj, str):
            raise TypeVibeMismatch("yapstring is immutable. make a new one.")
        if isinstance(obj, Instance):
            method = obj.squad.find_method("set_it")
            if method is not None:
                self.call_value(method, [obj, key, value])
                return
            raise WhoDis(f"'{obj.squad.name}' doesn't do 'set_it'.", roast=f"`{obj.squad.name}` doesn't do `set_it`. that's not its thing.")
        if obj is GHOST:
            raise GhostError("can't index-assign ghost.", roast="you're talking to a ghost, king.")
        raise TypeVibeMismatch(f"can't index-assign a {type_name(obj)}.")

    def _get_slice(self, obj, start, stop, step):
        conv = lambda v: None if v is GHOST else v
        try:
            sl = slice(conv(start), conv(stop), conv(step))
        except Exception:
            raise TypeVibeMismatch("slice bounds have to be numbas (or left out).")
        if isinstance(obj, Stash):
            return Stash(obj.items[sl])
        if isinstance(obj, str):
            return obj[sl]
        if obj is GHOST:
            raise GhostError("can't slice ghost.", roast="you're talking to a ghost, king.")
        raise TypeVibeMismatch(f"can't slice a {type_name(obj)}.")

    # -- arithmetic / comparisons -------------------------------------

    def _check_num2(self, a, b, opname: str) -> None:
        if not (_is_num(a) and _is_num(b)):
            if a is GHOST or b is GHOST:
                raise GhostError(f"can't do '{opname}' with ghost.", roast="you're talking to a ghost, king.")
            raise TypeVibeMismatch(
                f"a {type_name(a)} and a {type_name(b)} do NOT have the same energy.",
                roast="a numba and a yapstring do NOT have the same energy." if {type_name(a), type_name(b)} == {"numba", "yapstring"} else f"a {type_name(a)} and a {type_name(b)} do NOT have the same energy.",
            )

    def _add(self, a, b):
        if _is_num(a) and _is_num(b):
            return a + b
        if isinstance(a, str) and isinstance(b, str):
            return a + b
        if isinstance(a, Stash) and isinstance(b, Stash):
            return Stash(a.items + b.items)
        if a is GHOST or b is GHOST:
            raise GhostError("can't add with ghost.", roast="you're talking to a ghost, king.")
        numba_and_string = {type_name(a), type_name(b)} == {"numba", "yapstring"}
        # PLAN.md §4.2's own top-10-hints example: type mismatch on `+`.
        hint = "wrap the numba in to_yap(...) first, so both sides are yapstrings" if numba_and_string else None
        raise TypeVibeMismatch(
            f"a {type_name(a)} and a {type_name(b)} do NOT have the same energy.",
            roast="a numba and a yapstring do NOT have the same energy." if numba_and_string else f"a {type_name(a)} and a {type_name(b)} do NOT have the same energy.",
            hint=hint,
        )

    def _mul(self, a, b):
        if _is_num(a) and _is_num(b):
            return a * b
        if isinstance(a, str) and _is_int_like(b):
            return a * b
        if _is_int_like(a) and isinstance(b, str):
            return b * a
        if isinstance(a, Stash) and _is_int_like(b):
            return Stash(a.items * b)
        if _is_int_like(a) and isinstance(b, Stash):
            return Stash(b.items * a)
        if a is GHOST or b is GHOST:
            raise GhostError("can't multiply with ghost.", roast="you're talking to a ghost, king.")
        raise TypeVibeMismatch(f"a {type_name(a)} and a {type_name(b)} do NOT have the same energy.")

    def _bitwise(self, op, a, b):
        if not (_is_int_like(a) and _is_int_like(b)):
            raise TypeVibeMismatch(f"bitwise ops need whole numbas, not a {type_name(a)}/{type_name(b)}.")
        if op == Op.BAND:
            return a & b
        if op == Op.BOR:
            return a | b
        if op == Op.BXOR:
            return a ^ b
        if op == Op.SHL:
            return a << b
        return a >> b  # SHR

    def _compare(self, op, a, b):
        if _is_num(a) and _is_num(b):
            pass
        elif isinstance(a, str) and isinstance(b, str):
            pass
        else:
            if a is GHOST or b is GHOST:
                raise GhostError("can't compare with ghost.", roast="you're talking to a ghost, king.")
            raise TypeVibeMismatch(f"can't compare a {type_name(a)} and a {type_name(b)}.")
        if op == Op.LT:
            return a < b
        if op == Op.LE:
            return a <= b
        if op == Op.GT:
            return a > b
        return a >= b  # GE

    def _contains(self, a, b) -> bool:
        if isinstance(b, Stash):
            return any(funny_eq(a, x, self) for x in b.items)
        if isinstance(b, GroupChat):
            return any(funny_eq(a, k, self) for k in b.items)
        if isinstance(b, str):
            if not isinstance(a, str):
                raise TypeVibeMismatch("can only check if a yapstring is 'in' another yapstring.")
            return a in b
        raise TypeVibeMismatch(f"can't check 'in' on a {type_name(b)}.")

    # -- calls ------------------------------------------------------------

    def _do_call(self, argc: int) -> None:
        stack = self.stack
        arg_start = len(stack) - argc
        callee = stack[arg_start - 1]
        args = stack[arg_start:]
        del stack[arg_start - 1:]
        if isinstance(callee, Closure):
            self._push_closure_frame(callee, args)
        elif isinstance(callee, NativeFn):
            self._check_native_arity(callee, args)
            stack.append(callee.fn(self, args))
        elif isinstance(callee, BoundMethod):
            self._push_closure_frame(callee.method, [callee.receiver, *args])
        elif isinstance(callee, Squad):
            stack.append(self._construct(callee, args))
        else:
            raise NotACallableRizz(
                f"'{type_name(callee)}' is not callable.",
                roast="that thing has no call rizz whatsoever.",
            )

    def _do_invoke(self, name: str, argc: int) -> None:
        stack = self.stack
        arg_start = len(stack) - argc
        obj = stack[arg_start - 1]
        if isinstance(obj, Instance):
            method = obj.fields.get(name)
            if method is None:
                method = obj.squad.find_method(name)
                if method is None:
                    raise WhoDis(f"'{obj.squad.name}' doesn't do '{name}'.", roast=f"`{obj.squad.name}` doesn't do `{name}`. that's not its thing.")
                args = stack[arg_start:]
                del stack[arg_start - 1:]
                self._push_closure_frame(method, [obj, *args])
                return
        callee = self._get_prop(obj, name)
        args = stack[arg_start:]
        del stack[arg_start - 1:]
        if isinstance(callee, Closure):
            self._push_closure_frame(callee, args)
        elif isinstance(callee, NativeFn):
            self._check_native_arity(callee, args)
            stack.append(callee.fn(self, args))
        elif isinstance(callee, BoundMethod):
            self._push_closure_frame(callee.method, [callee.receiver, *args])
        else:
            raise NotACallableRizz(f"'{type_name(callee)}' is not callable.", roast="that thing has no call rizz whatsoever.")

    def _do_invoke_og(self, frame: Frame, name: str, argc: int) -> None:
        stack = self.stack
        arg_start = len(stack) - argc
        me = stack[arg_start - 1]
        # `og` resolves relative to the *defining* class of the currently
        # executing method (frame.closure.home_squad), never the instance's
        # own runtime class — otherwise a super-call from a middle class in
        # a 3+ level hierarchy would re-invoke its own method forever
        # instead of reaching the next class up.
        home_squad = frame.closure.home_squad
        if not isinstance(me, Instance) or home_squad is None or home_squad.superclass is None:
            raise NotACallableRizz("'og' has no superclass here.", roast="'og' who? you're not in a squad.")
        superclass = home_squad.superclass
        method = superclass.find_method(name)
        if method is None:
            raise WhoDis(f"'{superclass.name}' doesn't do '{name}'.", roast=f"`{superclass.name}` doesn't do `{name}`. that's not its thing.")
        args = stack[arg_start:]
        del stack[arg_start - 1:]
        self._push_closure_frame(method, [me, *args])

    # -- iteration ----------------------------------------------------------

    def _make_iter_gen(self, iterable):
        if isinstance(iterable, Stash):
            return iter(list(iterable.items))
        if isinstance(iterable, str):
            return iter(iterable)
        if isinstance(iterable, GroupChat):
            return iter(list(iterable.items.keys()))
        if iterable is GHOST:
            raise GhostError("can't iterate over ghost.", roast="you're talking to a ghost, king.")
        raise TypeVibeMismatch(f"can't iterate over a {type_name(iterable)}.")

    # -- modules (real semantics land in M7) -------------------------------

    def _do_import(self, path: str, mode: int):
        if self.module_loader is not None:
            return self.module_loader(self, path, mode)
        raise WhoDis(f"can't find '{path}'. modules aren't wired up yet.", roast=f"can't find `{path}`. did you make it up?")


class _FakeSpan:
    """A minimal stand-in for source.Span when an error occurs deep inside
    the VM with only a bare (line, col) available, not a full source range."""

    __slots__ = ("start", "end", "line", "col")

    def __init__(self, line: int, col: int):
        self.start = 0
        self.end = 0
        self.line = line
        self.col = col
