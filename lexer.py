import slex

DEDENT  = 'DEDENT'
INDENT  = 'INDENT'
EOL     = 'EOL'
EOF     = 'EOF'
INVALID = 'INVALID'
INT     = 'INT'
DEC     = 'DEC'
NAME    = 'NAME'
SMESS   = 'SMESS'
IMESS   = 'IMESS'
ASSIGN  = 'ASSIGN'
DOT     = 'DOT'
DOTS    = 'DOTS'
LPAR    = 'LPAR'
RPAR    = 'RPAR'
LSQU    = 'LSQU'
RSQU    = 'RSQU'
QUOTE   = 'QUOTE'
STR     = 'STR'
SPACE   = 'SPACE'
EOL     = 'EOL'
FOREIGN = 'FOREIGN'

# anything except parens, quotes, space, colon, and dot
name_class = r"[^\][()'" + '"' + "\s#:\.]"

def spaces(n):
    return ''.join(( ' ' for i in xrange(n) ))

def init(l, s):
    l.bol, l.line, l.col = True, 1, 1
    l.nesting = 0
    l.levels = [0]

def bef(l, s, name, lexeme):
    if l.nesting or not l.bol: return ()
    if name is EOL: return ()

    if name is SPACE:
        col = len(lexeme)
    else:
        col = 0

    if col > l.levels[-1]:
        l.levels.append(col)
        return (INDENT, spaces(col - l.levels[-2]), l.pos()),

    res = []
    while col < l.levels[-1]:
        l.levels.pop()
        res.append((DEDENT, '', l.pos()))
    if col > l.levels[-1]:
        raise '%d:%d: indent levels do not match' % (0, 0)
    return tuple(res)

def aft(l, s, name, lexeme):
    l.bol = (name == EOL)

def eol(l, s, name, lexeme):
    if l.bol or l.nesting: return ()
    return (name, lexeme, l.pos()),

def space(l, s, name, lexeme):
    return ()

def inest(l, s, name, lexeme):
    l.nesting += 1

def dnest(l, s, name, lexeme):
    l.nesting -= 1

# special chars are . : ( ) '
lexer = slex.Lexer((
    (FOREIGN, r'<<<<.*?>>>>'),
    (DEC,     r'-?([0-9]*\.[0-9]+|[0-9]+)'),
    (INT,     r'-?[0-9]+',             ),
    (SMESS,   ':' + name_class + '+',  ),
    (IMESS,   r'\.' + name_class + r'+|\+|\*',),
    (NAME,    name_class + '+',        ),
    (ASSIGN,  r'::',                   ),
    (DOT,     r'\.',                   ),
    (DOTS,    r':',                    ),
    (LPAR,    r'\(',                   inest),
    (RPAR,    r'\)',                   dnest),
    (LSQU,    r'\[',                   inest),
    (RSQU,    r'\]',                   dnest),
    (QUOTE,   r"'",                    ),
    (STR,     r'"([^"\\]|\\.)*"',      ),
    (EOL,     r' *(:?#[^\n]*)?\n', eol),
    (SPACE,   r' +',                   space),
    (EOF,     r'$',                    ),
), init, bef, aft)

lex = lexer.lex
def name(t): return t

if __name__ == '__main__':
  import sys
  s = open(sys.argv[1]).read()
  for tok in lexer.lex(s):
      print tok[0], tok[1]
