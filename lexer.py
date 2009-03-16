import re
import slex

DEDENT  = 'DEDENT'
INDENT  = 'INDENT'
EOL     = 'EOL'
EOF     = 'EOF'
INVALID = 'INVALID'
INT     = 'INT'
DEC     = 'DEC'
NAME    = 'NAME'
UNOP    = 'UNOP'
BINOP   = 'BINOP'
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
HEREDOC = 'HEREDOC'
HEREDOC_BODY = 'HEREDOC_BODY'

# anything except parens, quotes, space, colon, and dot
name_class = r"[^()'" + '"' + "\s#:\.[\]]"
name_pat = '(?:' + name_class + r'*\w' + name_class + '*)'
non_name_class = r"[^\w()'" + '"' + "\s#:\.[\]]"
non_name_pat = non_name_class + '+'

cork = False
collector = []

def spaces(n):
    return ''.join(( ' ' for i in xrange(n) ))

def init(l, s):
    l.bol, l.line, l.col = True, 1, 1
    l.nesting = 0
    l.levels = [0]

def bef(l, s, name, lexeme, pos):
    if l.nesting or not l.bol: return ()
    if name in (EOL, HEREDOC_BODY): return ()

    if name is SPACE:
        col = len(lexeme)
    else:
        col = 0

    if col > l.levels[-1]:
        l.levels.append(col)
        return (INDENT, spaces(col - l.levels[-2])),

    res = []
    while col < l.levels[-1]:
        l.levels.pop()
        res.append((DEDENT, ''))
        res.append((EOL, ''))
    if col > l.levels[-1]:
        raise '%d:%d: indent levels do not match' % (0, 0)
    return tuple(res)


def eol(l, s, name, lexeme, pos):
    if cork:
        pat = r'.*?\n' + heredoc_tok + r'\n'
        if hd_strip: pat = r'.*?\n *' + heredoc_tok + r'\n'
        l.add_rule(HEREDOC_BODY, pat, heredoc_body)
    if l.bol or l.nesting: return ()
    return (name, lexeme),

def heredoc_body(l, s, name, lexeme, pos):
    global cork
    lexeme = lexeme[:-len(heredoc_tok)-1]
    lexeme = lexeme[:lexeme.rfind('\n')]
    collector[0] = (HEREDOC, lexeme, pos)
    l.rm_rule(HEREDOC_BODY)
    cork = False
    return ()

def space(l, s, name, lexeme, pos):
    return ()

def inest(l, s, name, lexeme, pos):
    l.nesting += 1

def dnest(l, s, name, lexeme, pos):
    l.nesting -= 1

def heredoc(l, s, name, lexeme, pos):
    global cork, hd_strip, heredoc_tok
    cork = True
    if lexeme.startswith('<<:'):
        hd_strip, heredoc_tok = False, lexeme[3:]
    else:
        hd_strip, heredoc_tok = True, lexeme[2:]

def aft(l, s, name, lexeme, pos):
    l.bol = (name in (EOL, HEREDOC_BODY))

def filter(seq):
    global collector

    collector.extend(seq)
    if cork: return ()

    ret = collector
    collector = []
    return ret

# special chars are . : ( ) '
lexer = slex.Lexer((
    (HEREDOC, r'<<:?[a-zA-Z0-9]+', heredoc),
    (DEC,     r'-?([0-9]*\.[0-9]+|[0-9]+)'),
    (INT,     r'-?[0-9]+',             ),
    (UNOP,    r'\.' + name_pat + r'|\.' + non_name_pat + '|' +
              ':' + name_pat + '|' + ':' + non_name_pat,  ),
    (BINOP,   non_name_pat + '|' + '::'),
    (NAME,    name_pat,        ),
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
), init, bef, aft, filter)

lex = lexer.lex
def name(t): return t

if __name__ == '__main__':
  import sys
  s = open(sys.argv[1]).read()
  for tok in lexer.lex(s):
      print tok[0], tok[1]
