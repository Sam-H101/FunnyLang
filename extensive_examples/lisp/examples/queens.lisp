; queens.lisp — the eight queens, by backtracking.
;
; A board is a list of column numbers, one per row placed so far, most recent
; first. That makes "is this placement safe" a walk down the list comparing
; each earlier queen at a growing diagonal distance, and it makes backtracking
; free: an abandoned branch is simply a list nobody kept.

(define (safe? positions)
  (define (ok? here rest distance)
    (cond ((null? rest) #t)
          ((= here (car rest)) #f)
          ((= (abs (- here (car rest))) distance) #f)
          (else (ok? here (cdr rest) (+ distance 1)))))
  (if (null? positions)
      #t
      (ok? (car positions) (cdr positions) 1)))

(define (solutions board-size)
  (define (place rows-left positions)
    (if (= rows-left 0)
        (list positions)
        (append-map
          (lambda (column)
            (let ((tried (cons column positions)))
              (if (safe? tried)
                  (place (- rows-left 1) tried)
                  '())))
          (range 1 (+ board-size 1)))))
  (place board-size '()))

(define (count-solutions n)
  (length (solutions n)))

(displayln (count-solutions 4))
(displayln (count-solutions 5))
(displayln (count-solutions 6))
(displayln (count-solutions 7))

; Board eight is the famous one: 92 solutions. It is not run here because it
; costs about five seconds, and this file is a golden that four separate
; verification passes run. The README quotes the number and the cost.

; The first solution for a board of six, as columns from the last row placed
; back to the first.
(displayln (car (solutions 6)))

; A board too small to hold any: three queens on three rows cannot avoid each
; other.
(displayln (count-solutions 3))
(displayln (count-solutions 2))
