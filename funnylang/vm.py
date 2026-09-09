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
        self.globals: dict[str, object] = {}
        self.stdout = stdout if stdout is not None else sys.stdout
        self.const_pool = None
        self.protos = None
        self.source = None
        self.module_loader = None  # wired up in M7

    # -- entry points -------------------------------------------------------

    def interpret(self, unit, source=None):
        self.const_pool = unit.const_pool
        self.protos = unit.protos
        self.source = source
        entry = unit.protos[unit.entry_proto]
        closure = Closure(entry, [])
        return self.call_value(closure, [])

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
        if squad.spawn is not None:
            self.call_value(BoundMethod(instance, squad.spawn), args)
        return instance

    # -- constants ----------------------------------------------------------

    def _const(self, idx: int):
        tag, value = self.const_pool.entries[idx]
        return value

    def _const_str(self, idx: int) -> str:
        return self.const_pool.entries[idx][1]

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
            op = code[frame.ip]
            ip = frame.ip + 1
            try:
                if op == Op.CONST:
                    idx = (code[ip] << 8) | code[ip + 1]
                    stack.append(self._const(idx))
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
                    name = self._const_str(idx)
                    if name not in self.globals:
                        from .errors import suggest_name
                        hint = suggest_name(name, self.globals.keys())
                        roast = f"`{name}` who? never heard of them."
                        if hint:
                            roast += f" did you mean `{hint}`?"
                        raise WhoDis(f"'{name}' isn't defined.", roast=roast)
                    stack.append(self.globals[name])
                    frame.ip = ip + 2
                elif op == Op.SET_GLOBAL:
                    idx = (code[ip] << 8) | code[ip + 1]
                    self.globals[self._const_str(idx)] = stack[-1]
                    frame.ip = ip + 2
                elif op == Op.DEF_GLOBAL:
                    idx = (code[ip] << 8) | code[ip + 1]
                    self.globals[self._const_str(idx)] = stack.pop()
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
                    name = self._const_str(idx)
                    obj = stack.pop()
                    stack.append(self._get_prop(obj, name))
                    frame.ip = ip + 2
                elif op == Op.GET_PROP_SAFE:
                    idx = (code[ip] << 8) | code[ip + 1]
                    name = self._const_str(idx)
                    obj = stack.pop()
                    stack.append(GHOST if obj is GHOST else self._get_prop(obj, name))
                    frame.ip = ip + 2
                elif op == Op.SET_PROP:
                    idx = (code[ip] << 8) | code[ip + 1]
                    name = self._const_str(idx)
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
                    name = self._const_str(idx)
                    frame.ip = ip + 3
                    self._do_invoke(name, argc)
                elif op == Op.INVOKE_OG:
                    idx = (code[ip] << 8) | code[ip + 1]
                    argc = code[ip + 2]
                    name = self._const_str(idx)
                    frame.ip = ip + 3
                    self._do_invoke_og(name, argc)
                elif op == Op.CLOSURE:
                    const_idx = (code[ip] << 8) | code[ip + 1]
                    pos = ip + 2
                    tag, proto_idx = self.const_pool.entries[const_idx]
                    proto = self.protos[proto_idx]
                    upvalues = []
                    for _ in range(proto.upvalue_count):
                        is_local = code[pos]; uv_idx = code[pos + 1]
                        pos += 2
                        if is_local:
                            upvalues.append(self._capture_upvalue(frame.slot_base + uv_idx))
                        else:
                            upvalues.append(frame.closure.upvalues[uv_idx])
                    stack.append(Closure(proto, upvalues))
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
                    path = self._const_str(idx)
                    stack.append(self._do_import(path, mode))
                    frame.ip = ip + 3
                elif op == Op.EXPORT:
                    idx = (code[ip] << 8) | code[ip + 1]
                    frame.ip = ip + 2  # M7 gives this real semantics; value stays on stack
                elif op == Op.SQUAD:
                    idx = (code[ip] << 8) | code[ip + 1]
                    stack.append(Squad(self._const_str(idx)))
                    frame.ip = ip + 2
                elif op == Op.METHOD:
                    idx = (code[ip] << 8) | code[ip + 1]
                    name = self._const_str(idx)
                    method = stack.pop()
                    squad = stack[-1]
                    if name == "spawn":
                        squad.spawn = method
                    else:
                        squad.methods[name] = method
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
                if self._unwind(err, base_frame_count):
                    continue
                raise

    # -- error unwinding ----------------------------------------------------

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
                raise WhoDis(f"'{name}' isn't in module '{obj.name}'.", roast=f"`{name}` who? never heard of them.")
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
        if obj is GHOST:
            raise GhostError(f"can't read '{name}' off ghost.", roast="you're talking to a ghost, king.")
        raise WhoDis(
            f"a {type_name(obj)} doesn't have '{name}' (yet).",
            roast=f"`{name}` who? never heard of them.",
        )

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
        raise TypeVibeMismatch(
            f"a {type_name(a)} and a {type_name(b)} do NOT have the same energy.",
            roast="a numba and a yapstring do NOT have the same energy." if {type_name(a), type_name(b)} == {"numba", "yapstring"} else f"a {type_name(a)} and a {type_name(b)} do NOT have the same energy.",
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

    def _do_invoke_og(self, name: str, argc: int) -> None:
        stack = self.stack
        arg_start = len(stack) - argc
        me = stack[arg_start - 1]
        if not isinstance(me, Instance) or me.squad.superclass is None:
            raise NotACallableRizz("'og' has no superclass here.", roast="'og' who? you're not in a squad.")
        method = me.squad.superclass.find_method(name)
        if method is None:
            raise WhoDis(f"'{me.squad.superclass.name}' doesn't do '{name}'.", roast=f"`{me.squad.superclass.name}` doesn't do `{name}`. that's not its thing.")
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
