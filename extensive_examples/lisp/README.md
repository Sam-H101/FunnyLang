# lisp — a Lisp, in FunnyLang

A Scheme-ish interpreter: reader, evaluator, macros, and a REPL. About 1,600 lines of interpreter
and 450 more of examples and golden, and it runs the classic programs — fib, eight queens, and a
Lisp evaluator written in itself.

```console
$ funny run extensive_examples/lisp/lisp.funny
funnylisp. (exit) or ctrl-d to leave.
> (define (fib n) (if (< n 2) n (+ (fib (- n 1)) (fib (- n 2)))))
fib
> (fib 20)
6765
> (map (lambda (x) (* x x)) '(1 2 3))
(1 4 9)
> (exit)

$ funny run extensive_examples/lisp/lisp.funny examples/queens.lisp
$ funny run extensive_examples/lisp/lisp.funny -e "(* 6 7)"
42
```

| | |
|---|---|
| `values.funny` | What a Lisp value is, and how to print one |
| `reader.funny` | Text to s-expressions: tokenizer, then parser |
| `eval.funny` | Environments, and the trampolined evaluator |
| `builtins.funny` | The procedures not written in Lisp |
| `prelude.lisp` | The ones that are |
| `lisp.funny` | The command line and the REPL |
| `examples/*.lisp` | Programs, each with its committed `.expected` output |
| `test_lisp.funny` | The golden: the reader, the evaluator, and every example |

`funny test extensive_examples` runs the last one on every platform CI builds.

## Tail calls, and why there is a trampoline

There are no loops in this Lisp. A loop is a procedure that calls itself as the last thing it does,
and the host raises `TooDeepBro` past ten thousand frames, so a naive evaluator would die at ten
thousand iterations.

It does not recurse for a call in tail position. It overwrites its own `expr` and `env` and goes
round its loop again, which costs no host frame at all — so depth stays flat however many
iterations there are. `examples/tail.lisp` runs twelve thousand, deliberately past that limit,
through `if`, `cond`, `begin`, `let`, and a pair of mutually recursive procedures. Its last
definition is the counter-example: `(+ 1 (count-deep ...))` is *not* a tail call, does use host
frames, and is kept to a hundred on purpose.

Evaluating a test, or an argument, still recurses in the ordinary way. That is correct, and it is
why deeply *nested* expressions are bounded by the host stack while the number of iterations is
not.

## What is in the language

Integers, floats, strings, symbols, booleans, pairs and the empty list. `define`, `lambda` with
closures and a rest argument, `if`, `cond` with `else`, `let`, `quote`, `quasiquote` with `unquote`
and `unquote-splicing`, `set!`, `begin`, `and`, `or`, and `define-macro`.

Integers are FunnyLang's own, which are arbitrary precision, so `(fact 30)` is exact at 33 digits
rather than a float that stopped being exact around 2^53. Division stays exact when it divides
exactly — `(/ 6 3)` is `2`, not `2.0` — because there are no rationals here and a float would
spread through everything it touched.

Sixty-three built-in procedures, and a prelude written in the Lisp itself holding fourteen more
plus the three classic macros. `when` is the reason macros exist: it cannot be a procedure,
because a procedure's arguments are all evaluated before it runs, and the whole point is that the
body must not run when the test is false. `examples/macros.lisp` proves that with a counter, and
proves `my-or` evaluates its first argument exactly once.

## What it measured

An 8-core machine under WSL2, though nothing here uses more than one core. Each figure includes
about 0.8 s of starting the interpreter and loading the prelude, which is most of the cost of the
small examples.

| | what it exercises | time |
|---|---|---|
| `fib.lisp` | recursion, exact big integers, the division corners | 1.6 s |
| `macros.lisp` | `define-macro`, single evaluation, nested quasiquote | 0.8 s |
| `queens.lisp` | backtracking, boards up to seven | 2.4 s |
| `metacircular.lisp` | a Lisp evaluator, in Lisp, running fib through the Y combinator | 2.9 s |
| `tail.lisp` | twelve thousand tail calls, five ways | 3.1 s |
| `test_lisp.funny` | all five, plus the reader and evaluator checks | 7.5 s |

That works out at roughly 25 microseconds per Lisp procedure call. It is an abstract-syntax-tree
interpreter whose cons cells are host maps, so a call allocates a frame and every value carries a
tag; there is no compilation step and no attempt at one.

Two numbers the golden does not pay for: board eight has **92** solutions and takes **5.6 s**, and
the meta-circular evaluator computing `(fib 20)` instead of `(fib 12)` takes about **160 s** —
an interpreter running on an interpreter pays both costs, which is exactly the thing worth seeing.

## What it is not

- **Not Scheme, and not R7RS.** No `call/cc`, no `dynamic-wind`, no `values`, no vectors,
  characters, ports or string mutation, no `let*`, `letrec` or named `let`, no `do`, no internal
  `define` hoisting, and no tail position inside `map`.
- **No numeric tower.** Integers and floats, and that is all: `(/ 1 3)` is a float, not the exact
  rational a Scheme would give you. Exactness is preserved only where the division comes out even.
- **Macros are unhygienic.** `define-macro` is the classic kind, so a macro that introduces a
  binding can capture one of yours. `my-or` in the prelude is written the careful way and says so;
  a hygienic `syntax-rules` is a different and much larger thing.
- **No block comments and no `#|...|#`.** A comment runs from `;` to the end of the line.
- **Errors are the host's, with a Lisp form attached.** There is no `guard`, no condition system
  and no way to catch an error from inside the Lisp. A program that fails, fails to the command
  line with the offending form quoted.
- **The REPL has no history or editing.** It reads a line, and holds it if the parens are still
  open. Arrow keys are whatever your terminal does with them.
- **It is slow, and not because of a bug.** See the numbers above. Making it fast would mean
  compiling to something, which is a different example.
