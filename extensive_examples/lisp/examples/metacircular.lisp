; metacircular.lisp — a Lisp evaluator, written in the Lisp it evaluates.
;
; The evaluator in eval.funny is written in FunnyLang. This one is written in
; Lisp, runs on that one, and is about forty lines: symbols look up, pairs
; dispatch on their head, everything else is itself. That is the whole of the
; idea, and it is why a language that can express it is worth having.
;
; There is no `define` in the evaluated language here, and so no assignment to
; make recursion out of. Recursion arrives instead through the Y combinator --
; a function that takes a function expecting itself, and hands it itself. It
; looks like a trick and is really just the fixed point of a functional.

(define (zip-pairs names values)
  (if (null? names)
      '()
      (cons (cons (car names) (car values))
            (zip-pairs (cdr names) (cdr values)))))

(define (lookup name env)
  (if (null? env)
      (error "unbound variable:" name)
      (let ((hit (assq name (car env))))
        (if hit
            (cdr hit)
            (lookup name (cdr env))))))

(define (third x) (car (cddr x)))
(define (fourth x) (car (cdr (cddr x))))

(define (my-eval expr env)
  (cond ((symbol? expr) (lookup expr env))
        ((not (pair? expr)) expr)
        ((eq? (car expr) 'quote) (cadr expr))
        ((eq? (car expr) 'if)
         (if (my-eval (cadr expr) env)
             (my-eval (third expr) env)
             (my-eval (fourth expr) env)))
        ((eq? (car expr) 'lambda)
         (list 'closure (cadr expr) (third expr) env))
        (else
         (my-apply (my-eval (car expr) env)
                   (map (lambda (argument) (my-eval argument env))
                        (cdr expr))))))

(define (my-apply f args)
  (cond ((eq? (car f) 'primitive) (apply (cadr f) args))
        ((eq? (car f) 'closure)
         (my-eval (third f) (cons (zip-pairs (cadr f) args) (fourth f))))
        (else (error "not a procedure:" f))))

(define global
  (list (list (cons '+ (list 'primitive +))
              (cons '- (list 'primitive -))
              (cons '* (list 'primitive *))
              (cons '< (list 'primitive <))
              (cons '= (list 'primitive =)))))

; Arithmetic first, to show the thing works at all.
(displayln (my-eval '(+ 1 2) global))
(displayln (my-eval '((lambda (x y) (* x y)) 6 7) global))
(displayln (my-eval '(if (< 1 2) 'yes 'no) global))

; A closure really closes: the inner lambda still sees x after the outer one
; has returned.
(displayln (my-eval '(((lambda (x) (lambda (y) (+ x y))) 10) 5) global))

; And now fib, with no define anywhere in the evaluated program.
(define y-combinator
  '(lambda (f)
     ((lambda (x) (f (lambda (v) ((x x) v))))
      (lambda (x) (f (lambda (v) ((x x) v)))))))

(define fib-maker
  '(lambda (fib)
     (lambda (n)
       (if (< n 2)
           n
           (+ (fib (- n 1)) (fib (- n 2)))))))

(define fib-expression (list y-combinator fib-maker))

(displayln (my-eval (list fib-expression 10) global))

; Computed once and kept. An interpreter running on an interpreter pays both
; costs, so evaluating this twice would double the slowest thing in the
; example for no extra evidence.
(define meta-fib (my-eval (list fib-expression 12) global))
(displayln meta-fib)

; The same answer the host Lisp gives, which is the point of calling it
; meta-circular.
(define (host-fib n) (if (< n 2) n (+ (host-fib (- n 1)) (host-fib (- n 2)))))
(displayln (= meta-fib (host-fib 12)))
