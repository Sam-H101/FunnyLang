# FunnyLang bytecode reference

FunnyLang compiles to bytecode for a stack-based VM. This document is the frozen ISA (PLAN.md §5)
plus a worked walkthrough of an actual compiled program.

All jump offsets are unsigned 16-bit, big-endian, relative to the address right after the
instruction's own operands. Constant-pool indices are unsigned 16-bit big-endian unless noted.
Local and upvalue slots are a single unsigned byte (max 256 locals per function).

## Opcode table

| # | Name | Operands | Stack effect |
|---|---|---|---|
| 0 | `NOP` | — | — |
| 1 | `CONST` | u16 idx | → value |
| 2 | `GHOST` | — | → ghost |
| 3 | `FAX` | — | → true |
| 4 | `CAP` | — | → false |
| 5 | `POP` | — | value → |
| 6 | `DUP` | — | a → a a |
| 7 | `SWAP` | — | a b → b a |
| 8 | `GET_LOCAL` | u8 slot | → value |
| 9 | `SET_LOCAL` | u8 slot | value → value |
| 10 | `GET_GLOBAL` | u16 nameIdx | → value |
| 11 | `SET_GLOBAL` | u16 nameIdx | value → value |
| 12 | `DEF_GLOBAL` | u16 nameIdx | value → |
| 13 | `GET_UPVAL` | u8 idx | → value |
| 14 | `SET_UPVAL` | u8 idx | value → value |
| 15 | `CLOSE_UPVAL` | — | value → |
| 16 | `GET_PROP` | u16 nameIdx | obj → value |
| 17 | `SET_PROP` | u16 nameIdx | obj value → value |
| 18 | `GET_PROP_SAFE` | u16 nameIdx | obj → value \| ghost |
| 19 | `GET_INDEX` | — | obj key → value |
| 20 | `SET_INDEX` | — | obj key value → value |
| 21 | `GET_SLICE` | — | obj start stop step → value |
| 22 | `ADD` | — | a b → c |
| 23 | `SUB` | — | a b → c |
| 24 | `MUL` | — | a b → c |
| 25 | `DIV` | — | a b → c |
| 26 | `IDIV` | — | a b → c |
| 27 | `MOD` | — | a b → c |
| 28 | `POW` | — | a b → c |
| 29 | `NEG` | — | a → -a |
| 30 | `NOT` | — | a → boolski |
| 31 | `BNOT` | — | a → ~a |
| 32 | `BAND` | — | a b → c |
| 33 | `BOR` | — | a b → c |
| 34 | `BXOR` | — | a b → c |
| 35 | `SHL` | — | a b → c |
| 36 | `SHR` | — | a b → c |
| 37 | `EQ` | — | a b → boolski |
| 38 | `NEQ` | — | a b → boolski |
| 39 | `LT` | — | a b → boolski |
| 40 | `LE` | — | a b → boolski |
| 41 | `GT` | — | a b → boolski |
| 42 | `GE` | — | a b → boolski |
| 43 | `IN` | — | a b → boolski |
| 44 | `JUMP` | u16 off | — (ip += off) |
| 45 | `JUMP_IF_FALSE` | u16 off | cond → |
| 46 | `JUMP_IF_TRUE` | u16 off | cond → |
| 47 | `JUMP_IF_FALSE_KEEP` | u16 off | cond → cond (for `&&`) |
| 48 | `JUMP_IF_TRUE_KEEP` | u16 off | cond → cond (for `\|\|`) |
| 49 | `JUMP_IF_GHOST_KEEP` | u16 off | a → a (for `??` and `?.`) |
| 50 | `LOOP` | u16 back | — (ip -= back) |
| 51 | `CALL` | u8 argc | fn a1..an → result |
| 52 | `INVOKE` | u16 nameIdx, u8 argc | obj a1..an → result |
| 53 | `INVOKE_OG` | u16 nameIdx, u8 argc | obj a1..an → result |
| 54 | `CLOSURE` | u16 protoIdx, then n×(u8 isLocal, u8 index) | → closure |
| 55 | `RETURN` | — | value → (pops frame) |
| 56 | `BUILD_STASH` | u16 n | n values → stash |
| 57 | `BUILD_GROUPCHAT` | u16 n | 2n values → groupchat |
| 58 | `BUILD_STRING` | u16 n | n values → yapstring (interpolation) |
| 59 | `YAP` | u8 argc, u8 newline | n values → |
| 60 | `CHUCK` | — | value → (unwinds) |
| 61 | `TRY_PUSH` | u16 handlerOff, u16 finallyOff | — |
| 62 | `TRY_POP` | — | — |
| 63 | `SQUAD` | u16 nameIdx | → class |
| 64 | `METHOD` | u16 nameIdx | class closure → class |
| 65 | `INHERIT` | — | super class → super class |
| 66 | `IMPORT` | u16 pathIdx, u8 mode | → module \| bindings |
| 67 | `EXPORT` | u16 nameIdx | value → value |
| 68 | `ITER_NEW` | — | iterable → iterator |
| 69 | `ITER_NEXT` | u16 doneOff | iterator → iterator value |
| 70 | `HALT` | — | — |
| 71 | `JUMP_LONG` | u32 off | — (ip += off) |
| 72 | `LOOP_LONG` | u32 back | — (ip -= back) |
| 73 | `PTR_LOCAL` | u8 slot, u16 nameIdx | → pointa |
| 74 | `PTR_GLOBAL` | u16 nameIdx | → pointa |
| 75 | `PTR_UPVAL` | u8 idx, u16 nameIdx | → pointa |
| 76 | `PTR_INDEX` | — | obj key → pointa |
| 77 | `PTR_PROP` | u16 nameIdx | obj → pointa |
| 78 | `DEREF` | — | pointa → value |
| 79 | `SET_DEREF` | — | pointa value → value |

80–99 are reserved for future opcodes; numbering is never reused. `JUMP_LONG`/`LOOP_LONG` (added
during M11's hardening pass) are wide-offset counterparts of `JUMP`/`LOOP`, emitted only when a
single function's body is so large a u16 offset can't reach — ordinary programs never produce
them. See `PLAN.md` §16 for the full design rationale, including how a conditional jump (which
can't itself grow a wider operand without changing its opcode identity) gets extended reach via a
short trampoline into a `JUMP_LONG`.

`PTR_LOCAL`/`PTR_GLOBAL`/`PTR_UPVAL`/`PTR_INDEX`/`PTR_PROP` (added in M15) each build a `pointa` —
a safe reference to a place, never a raw address — over one of the five forms `&` can produce
(PLAN.md §3.10). `PTR_LOCAL`/`PTR_UPVAL` box the addressed slot via the same `Upvalue` mechanism
closures use for captures, so an `&x` and a closure capturing `x` alias each other; `PTR_INDEX`/
`PTR_PROP` just remember the container and the key/name, since a stash/groupchat/instance is
already a reference type. `DEREF`/`SET_DEREF` read/write through one. `PTR_LOCAL`'s and
`PTR_UPVAL`'s `nameIdx` operand is otherwise-redundant display data only (for `.where()`/`to_yap`);
`BYTECODE_VERSION` went to `2` alongside these seven, since a `.funnyc` using them is genuinely
unreadable by a v1 runtime.

`TRY_PUSH`'s `handlerOff`/`finallyOff` use `0xFFFF` as an explicit "absent" sentinel (no `my_bad` /
no `regardless`), since a real offset of `0` is reachable. Both are relative to the address right
after `TRY_PUSH`'s own operands, the same "ip after the instruction" convention every jump uses.

`ITER_NEXT` operates in place on the iterator already on top of the stack: not-done leaves
`[iterator, value]`; done leaves `[iterator]` untouched (doesn't consume it) and jumps to
`doneOff`. The iterator lives in exactly one persistent stack slot for the whole loop.

## A worked example

```funny
bet add(a, b) {
    bounce a + b
}
yap add(2, 3)
```

`funny xray` disassembles it as two protos — the function, and the implicit top-level script that
calls it:

```
== bet add (arity 2) ==
0000  line 2  GET_LOCAL     0
0002          GET_LOCAL     1
0004          ADD
0005          RETURN
0006  line 1  GHOST
0007          RETURN

== <script> (entry) (arity 0) ==
0000  line 1  CLOSURE       0    ; proto #0
0003          DEF_GLOBAL    1    ; "add"
0006  line 4  GET_GLOBAL    1    ; "add"
0009          CONST         2    ; 2
0012          CONST         3    ; 3
0015          CALL          2
0017          YAP           1 1
0020          GHOST
0021          RETURN
```

Reading `add`'s body: `GET_LOCAL 0`/`GET_LOCAL 1` push parameters `a` and `b` (slots 0 and 1 — a
function's own parameters always start at slot 0, or slot 1 for a method, where slot 0 is the
implicit `me`), `ADD` pops both and pushes their sum, `RETURN` pops the frame with that sum as the
result. The `GHOST; RETURN` at offset 6 is unreachable dead code the compiler always appends after
a function body, in case control falls off the end without an explicit `bounce` — here it never
runs, since the real `RETURN` at offset 5 already returned.

The script: `CLOSURE 0` builds a closure over proto #0 (`add`) and pushes it; `DEF_GLOBAL 1` pops
it into the global named by constant #1 (`"add"`). Then `GET_GLOBAL 1` fetches `add` back,
`CONST 2`/`CONST 3` push the two literal arguments, `CALL 2` invokes it with `argc=2` and pushes
the result, and `YAP 1 1` prints `argc=1` values followed by a newline (`newline=1` — `mumble`
compiles the same instruction with `newline=0`). The trailing `GHOST; RETURN` is the top-level
script's own implicit return value, same as any function.

## `.funnyc` file format

All multi-byte integers are big-endian. A `str` field is a u32 byte length followed by that many
UTF-8 bytes.

```
magic          6 bytes   b"FUNNY\x00"
version        u16       BYTECODE_VERSION (currently 2; pointers, added in M15, bumped it from 1)
flags          u16       bit 0 = has_debug_info
source_name    str
const_count    u32
consts         const_count × { tag u8, payload }
                 tag 0 = ghost        (no payload)
                 tag 1 = boolski      (u8)
                 tag 2 = int          (u8 sign, u32 byte_len, big-endian magnitude bytes)
                 tag 3 = float        (8 bytes, IEEE-754 big-endian)
                 tag 4 = yapstring    (str)
                 tag 5 = proto_ref    (u32 index into the proto table)
proto_count    u32
protos         proto_count × {
                 name str
                 arity u8
                 default_count u8
                 is_variadic u8
                 upvalue_count u8
                 max_stack u16
                 local_count u8
                 code u32 len + bytes
                 line_count u32
                 lines line_count × { code_offset u32, line u32, col u32 }
               }
entry_proto    u32
```

An int constant's magnitude uses the *minimum* number of bytes that fit it — `0` has `byte_len 0`
(no magnitude bytes at all), matching `n_bytes = ceil(value.bit_length() / 8)`. An int `1` and a
float `1.0` are always distinct constants; the const pool dedupes by `(tag, value)`, never across
tags.

Here's `yap "hi"` (100 bytes) with every field labeled — extracted with an actual field-by-field
parse, not hand-counted, since it's easy to get byte offsets subtly wrong by eye:

```
46 55 4e 4e 59 00              magic "FUNNY\x00"
00 02                          version = 2
00 01                          flags = 1 (has_debug_info)
00 00 00 08 "hi.funny"         source_name
00 00 00 01                    const_count = 1
  04 00 00 00 02 "hi"          const #0: tag 4 (string), len 2, "hi"
00 00 00 01                    proto_count = 1
  00 00 00 08 "<script>"       proto #0 name
  00                           arity = 0
  00                           default_count = 0
  00                           is_variadic = 0
  00                           upvalue_count = 0
  00 01                        max_stack = 1
  00                           local_count = 0
  00 00 00 08                  code length = 8
    01 00 00                     CONST 0        ; "hi", at offset 0
    3b 01 01                     YAP 1 1        , at offset 3
    02                           GHOST                    , at offset 6
    37                           RETURN                   , at offset 7
  00 00 00 02                  line_count = 2
    00 00 00 00 00 00 00 05      offset 0, line 1, col 5  (the `"hi"` literal starts after `yap `)
    00 00 00 03 00 00 00 01      offset 3, line 1, col 1  (the statement's own start column)
00 00 00 00                    entry_proto = 0
```

The line table only gets a new entry when the (line, col) actually changes from the previous
instruction — `GHOST`/`RETURN` at offsets 6–7 reuse the entry at offset 3, so there's no separate
row for them.

## `.funnypak` (linked bundle) format

A `.funnypak` links a whole dependency tree — the entry module plus every module it (transitively)
imports — into one self-contained file with no filesystem access needed at run time; internal
`gimme` paths are resolved by canonical logical name, not by touching disk again.

```
magic          9 bytes   b"FUNNYPAK\x00"
version        u16
module_count   u32
modules        module_count × { logical_name str, funnyc u32 len + bytes }
entry_name     str
```

`funny build` produces a `.funnypak` automatically whenever the entry file has more than one
module in its dependency tree; a single, dependency-free file compiles to a plain `.funnyc`
instead. `funny run` picks the right loader by file content, not extension.

## Native executable layout

`funny yeet` appends a compiled program directly onto `funnyrt`, the runtime stub — a native
binary that is the VM with no compiler in it, since a shipped executable only ever runs
already-compiled bytecode:

```
[ funnyrt bytes ][ payload bytes ][ b"FUNNYYEET" 9 bytes ][ payload_len u64 big-endian ]
```

The trailer is exactly 17 bytes, **magic first**. (`PLAN.md` §5.4's diagram shows the length
before the magic; the order above is what the packager actually writes and what the stub reads.
Following the code, not the diagram.)

At startup the stub finds its own file, seeks to `filesize - 17`, checks the magic, reads
`payload_len`, seeks back to `filesize - 17 - payload_len`, and loads the payload from there — no
temp files, no extraction. Running `funnyrt` itself, with nothing appended, says so and exits 2.

The payload is a `.funnyc` for a single-file program and a `.funnypak` for one with imports; the
stub tells them apart by magic, not by anything in the trailer.
