# Grammar

    prog: line* EOF
    line: expr EOL
    expr: body
        : body "."
        : body "." expr
        : body ":" expr
        : body ":" EOL block
    body:
        : atom mess* binexpr*
    block: INDENT line* DEDENT
    mess: BINOP
        : UNOP
    binexpr: unexpr
           : unexpr BINOP binexpr
    unexpr: atom UNOP*
    atom: SYMBOL
        : NUMBER
        : STRING
        : "(" expr ")"
        : "[" expr "]"
        : "'" atom

## Example 1

    def (f x) (* x x)

    def (g .) (print "hi")

## Example 2

    def a: obj:
      (foo x y):
        print x
        print y
      (bar .):
        print "hello"
