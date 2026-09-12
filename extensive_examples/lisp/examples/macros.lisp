; macros.lisp — why a macro is not a procedure.

; `when` cannot be written as a procedure. A procedure's arguments are all
; evaluated before it runs, so the body would run whatever the test said. A
; macro receives the body unevaluated and decides.

(displayln (when #t 'ran))
(displayln (when #f 'ran))
(displayln (unless #f 'ran))

; The proof: a body with a side effect, under a false test, must not happen.
(define noise 0)
(define (make-noise) (set! noise (+ noise 1)) 'noisy)

(when #f (make-noise))
(displayln noise)

(when #t (make-noise))
(displayln noise)

; `my-or` evaluates its first argument exactly once. The naive expansion
;   (if a a b)
; evaluates it twice, which a counter makes visible.
(set! noise 0)
(displayln (my-or (make-noise) 'fallback))
(displayln noise)

; And it still falls through when the first argument is false.
(displayln (my-or #f 'fallback))

; A macro is an ordinary value in the environment, so you can ask what it is.
(displayln (quote (when test body)))

; Macros expanding into macros work, because expansion happens on the way in
; and the result is evaluated as any other form.
(define-macro (unless-zero n . body)
  `(unless (= ,n 0) ,@body))

(displayln (unless-zero 5 'not-zero))
(displayln (unless-zero 0 'not-zero))

; Quasiquote nests: an inner one raises the level, so this unquote belongs to
; the inner backquote and survives the outer one.
(define x 'outer)
(displayln `(a ,x `(b ,x)))
