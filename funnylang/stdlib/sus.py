"""`sus` — reflection / debug helpers (PLAN.md §M6 task 4)."""
from __future__ import annotations

from ..errors import TypeVibeMismatch
from ..values import GHOST, GroupChat, Instance, Module, NativeFn, Stash, to_repr, type_name


def _nf(name, fn, lo, hi=None):
    return NativeFn(name, fn, lo, hi)


def _type_of(vm, a):
    return type_name(a[0])


def _fields_of(vm, a):
    x = a[0]
    if not isinstance(x, Instance):
        return GroupChat({})
    return GroupChat(dict(x.fields))


def _is_a(vm, a):
    return type_name(a[0]) == a[1]


def _stack_trace(vm, a):
    return Stash(list(vm._build_trace(vm.frames[-1], vm.frames[-1].ip)) if vm.frames else [])


def _dump(vm, a):
    vm.stdout.write(to_repr(a[0], vm) + "\n")
    return a[0]


def _run_bytecode(vm, a):
    """NATIVE_PLAN.md N8 task 4: run compiled FunnyLang from FunnyLang.

    A self-hosted toolchain needs this — `funny test` has to observe another
    program's stdout and find out which error flavor (if any) escaped it,
    and `funny bootstrap --verify` has to run a compiler bundle three times.
    In Python those are one `VM(stdout=StringIO()); vm.interpret(unit)`;
    there was no equivalent a FunnyLang program could reach.

    A *fresh* VM with its own globals, deliberately: this is isolation, not
    `eval`. The child cannot see or disturb the caller's state.
    """
    import io

    from ..chunk import CompiledUnit  # noqa: F401  (documents the contract)
    from ..errors import FunnyError
    from ..modules import CanonicalSource, make_pak_module_loader
    from ..serializer import load_funnyc, load_funnypak
    from ..vm import VM
    from . import install_stdlib

    blob = a[0]
    if not isinstance(blob, Stash):
        raise TypeVibeMismatch(f"'run_bytecode' needs a stash of bytes, not a {type_name(blob)}.")
    try:
        data = bytes(bytearray(blob.items))
    except (TypeError, ValueError):
        raise TypeVibeMismatch("'run_bytecode' needs a stash of ints 0-255.") from None

    child = VM(stdout=io.StringIO())
    install_stdlib(child)
    args = a[1] if len(a) > 1 and isinstance(a[1], Stash) else Stash([])
    child.program_args = [str(x) for x in args.items]

    flavor = message = None
    exit_code = 0
    try:
        if data[:9] == b"FUNNYPAK\x00":
            modules, entry_name = load_funnypak(data)
            child.module_loader = make_pak_module_loader(modules, entry_name)
            child.interpret(modules[entry_name], CanonicalSource(entry_name))
        else:
            child.interpret(load_funnyc(data), None)
    except SystemExit as exc:
        # dip(n) is a clean exit, not a failure. Caught here rather than let
        # through: the *child* asked to exit, not the process running it.
        exit_code = exc.code if isinstance(exc.code, int) else 0
    except FunnyError as err:
        flavor, message = err.flavor, err.message
        exit_code = 69 if err.flavor == "ComputerExploded" else 1
    except Exception as exc:  # a malformed blob that got past the loader
        flavor, message = "BytecodeVersionMismatch", str(exc)
        exit_code = 1

    return GroupChat({
        "out": child.stdout.getvalue(),
        "flavor": flavor if flavor is not None else GHOST,
        "message": message if message is not None else GHOST,
        "code": exit_code,
    })


# NATIVE_PLAN.md N8 task 5: a REPL session is `run_bytecode`'s twin, kept
# alive between calls so `yo x = 1` on one line is still there on the next.
# Sessions are addressed by a small integer rather than a value the program
# holds, which keeps them out of the collector on the C side; the two
# implementations stay symmetric by doing it the same way here.
_SESSIONS: dict[int, object] = {}
_NEXT_SESSION = [0]


def _new_session(vm, a):
    import io  # noqa: F401  (parity with the C implementation's own imports)

    from ..vm import VM
    from . import install_stdlib

    child = VM()
    install_stdlib(child)
    child.repl_globals = {}
    child.repl_exports = {}
    _NEXT_SESSION[0] += 1
    _SESSIONS[_NEXT_SESSION[0]] = child
    return _NEXT_SESSION[0]


def _close_session(vm, a):
    if isinstance(a[0], bool) or not isinstance(a[0], int):
        raise TypeVibeMismatch(f"'close_session' needs a session id, not a {type_name(a[0])}.")
    _SESSIONS.pop(a[0], None)
    return GHOST


def _run_in(vm, a):
    from ..errors import FunnyError, OutOfPocket
    from ..serializer import load_funnyc

    if isinstance(a[0], bool) or not isinstance(a[0], int):
        raise TypeVibeMismatch(f"'run_in' needs a session id, not a {type_name(a[0])}.")
    child = _SESSIONS.get(a[0])
    if child is None:
        raise OutOfPocket(f"there's no open session {a[0]}.")
    blob = a[1]
    if not isinstance(blob, Stash):
        raise TypeVibeMismatch(f"'run_in' needs a stash of bytes, not a {type_name(blob)}.")
    try:
        data = bytes(bytearray(blob.items))
    except (TypeError, ValueError):
        raise TypeVibeMismatch("'run_in' needs a stash of ints 0-255.") from None
    if len(a) > 2 and isinstance(a[2], Stash):
        child.program_args = [str(x) for x in a[2].items]

    # Output is deliberately *not* captured: it goes to the caller's own
    # stream, so a long-running input streams and an `ask()` prompt appears
    # before its input.
    child.stdout = vm.stdout
    flavor = message = repr_text = None
    exit_code = 0
    try:
        unit = load_funnyc(data)
    except Exception as exc:
        flavor, message, exit_code = "BytecodeVersionMismatch", str(exc), 1
    else:
        try:
            value = child.run_repl_unit(unit, None, child.repl_globals, child.repl_exports)
            repr_text = to_repr(value, child)
        except SystemExit as exc:
            exit_code = exc.code if isinstance(exc.code, int) else 0
        except FunnyError as err:
            flavor, message = err.flavor, err.message
            exit_code = 69 if err.flavor == "ComputerExploded" else 1

    return GroupChat({
        "repr": repr_text if repr_text is not None else GHOST,
        "flavor": flavor if flavor is not None else GHOST,
        "message": message if message is not None else GHOST,
        "code": exit_code,
    })


def build() -> Module:
    members = {
        "type_of": _nf("type_of", _type_of, 1),
        "fields_of": _nf("fields_of", _fields_of, 1),
        "is_a": _nf("is_a", _is_a, 2),
        "stack_trace": _nf("stack_trace", _stack_trace, 0),
        "dump": _nf("dump", _dump, 1),
        "run_bytecode": _nf("run_bytecode", _run_bytecode, 1, 2),
        "new_session": _nf("new_session", _new_session, 0),
        "run_in": _nf("run_in", _run_in, 2, 3),
        "close_session": _nf("close_session", _close_session, 1),
    }
    return Module("sus", members)
