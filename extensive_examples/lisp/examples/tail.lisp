; tail.lisp — a loop written as recursion, which is the only way to write one
; here.
;
; The host raises TooDeepBro past ten thousand frames, and every loop below runs
; twelve thousand iterations -- past that limit on purpose. None of them uses
; a host frame at all: the evaluator overwrites its own expression and
; environment for a call in tail position and goes round its loop, so depth
; stays flat however many iterations there are.

(define (count-to n acc)
  (if (= n 0)
      acc
      (count-to (- n 1) (+ acc 1))))

(displayln (count-to 12000 0))

; A tail call through `cond` is a tail call too.
(define (count-cond n acc)
  (cond ((= n 0) acc)
        (else (count-cond (- n 1) (+ acc 1)))))

(displayln (count-cond 12000 0))

; So is one through the last arm of `and`/`or`, and through a `begin` or a
; `let` body.
(define (count-begin n acc)
  (if (= n 0)
      acc
      (begin (count-begin (- n 1) (+ acc 1)))))

(displayln (count-begin 12000 0))

(define (count-let n acc)
  (if (= n 0)
      acc
      (let ((next (- n 1)))
        (count-let next (+ acc 1)))))

(displayln (count-let 12000 0))

; Mutual recursion in tail position, which a trampoline handles and a
; stack-growing evaluator does not.
(define (even-steven? n) (if (= n 0) #t (odd-rod? (- n 1))))
(define (odd-rod? n) (if (= n 0) #f (even-steven? (- n 1))))

(displayln (even-steven? 12000))
(displayln (odd-rod? 12000))

; Not a tail call: this one builds 1 + (1 + (1 + ...)) and does use host
; frames, so it is kept small deliberately. The difference between this and
; the first definition is the whole point of the file.
(define (count-deep n)
  (if (= n 0)
      0
      (+ 1 (count-deep (- n 1)))))

(displayln (count-deep 100))
