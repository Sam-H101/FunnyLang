; fib.lisp — the one everybody writes first, and the reason integers matter.

(define (fib n)
  (if (< n 2)
      n
      (+ (fib (- n 1)) (fib (- n 2)))))

(displayln (fib 20))
(displayln (map fib (range 0 15)))

; Integers here are FunnyLang's own, which are arbitrary precision, so this is
; exact rather than a float that stopped being exact around 2^53.
(define (fact n)
  (if (= n 0)
      1
      (* n (fact (- n 1)))))

(displayln (fact 30))
(displayln (= (fact 20) (* (fact 19) 20)))

; Division stays exact when it divides exactly, because there are no rationals
; here and 2.0 would spread through everything it touched.
(displayln (/ 6 3))
(displayln (/ 7 2))
(displayln (quotient 7 2))
(displayln (remainder -7 2))
(displayln (modulo -7 2))
