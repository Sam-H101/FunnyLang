; prelude.lisp — the part of this Lisp that is written in itself.
;
; Everything here could have been a built-in. It is not, because a Lisp that
; can define its own control structures is the thing worth showing, and because
; every line below is another line of evaluator being exercised every time the
; REPL starts.
;
; Loaded automatically; `--no-prelude` leaves it out.

; -- the classic three macros ------------------------------------------------
;
; A macro receives its arguments unevaluated and returns a form to evaluate in
; their place. `when` is the reason macros exist: you cannot write it as a
; procedure, because a procedure's arguments are evaluated before it runs, and
; the whole point is that the body must not run when the test is false.

(define-macro (when test . body)
  `(if ,test (begin ,@body) '()))

(define-macro (unless test . body)
  `(if ,test '() (begin ,@body)))

; `my-or` is the classic example of what unhygienic macros get wrong, so it is
; here spelled the careful way. The obvious expansion evaluates `a` twice:
;   (my-or (big-computation) b)  ->  (if (big-computation) (big-computation) b)
; Binding it first evaluates it once, and the temporary is named something no
; caller would write.
(define-macro (my-or a b)
  `(let ((my-or-value ,a))
     (if my-or-value my-or-value ,b)))

; -- lists -------------------------------------------------------------------

(define (caar x) (car (car x)))
(define (cdar x) (cdr (car x)))
(define (last lst)
  (if (null? (cdr lst))
      (car lst)
      (last (cdr lst))))

(define (list-tail lst k)
  (if (= k 0)
      lst
      (list-tail (cdr lst) (- k 1))))

(define (assoc key alist)
  (cond ((null? alist) #f)
        ((equal? key (caar alist)) (car alist))
        (else (assoc key (cdr alist)))))

; Written with an accumulator so it is a tail call, which is how you write a
; loop here. The evaluator's trampoline is what makes that safe for a list of
; any length.
(define (length-of lst)
  (define (go lst n)
    (if (null? lst) n (go (cdr lst) (+ n 1))))
  (go lst 0))

(define (range from to)
  (if (>= from to)
      '()
      (cons from (range (+ from 1) to))))

(define (sum lst) (reduce + 0 lst))
(define (product lst) (reduce * 1 lst))

(define (any? pred lst)
  (cond ((null? lst) #f)
        ((pred (car lst)) #t)
        (else (any? pred (cdr lst)))))

(define (every? pred lst)
  (cond ((null? lst) #t)
        ((pred (car lst)) (every? pred (cdr lst)))
        (else #f)))

(define (append-map f lst)
  (if (null? lst)
      '()
      (append (f (car lst)) (append-map f (cdr lst)))))

; -- output ------------------------------------------------------------------

(define (displayln x) (display x) (newline))

(define (print-all lst)
  (for-each displayln lst))
