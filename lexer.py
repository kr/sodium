import re

def lex(s):
    bol = True
    nesting = 0
    levels = [0]

    p = 0
    l = 1
    c = 1
    while len(s):
        type, lexeme = scan(s)
        if type == INVALID:
            raise '%d:%d: illegal character %r' % (l, c, s[0])
        if type == EOL:
            if not (bol or nesting):
                yield type, lexeme, lc(l, c)
                bol = True
        else:

            if bol and not nesting:
                bol = False
                if type == SPACE:
                    col = len(lexeme)
                else:
                    col = 0
                if col > levels[-1]:
                    yield INDENT, spaces(col - levels[-1]), lc(l, col)
                    levels.append(col)
                else:
                    while col < levels[-1]:
                        levels.pop()
                        yield DEDENT, '', lc(l, c)
                    if col > levels[-1]:
                        raise '%d:%d: indent levels do not match' % (l, col)

            if type == LPAR: nesting += 1
            if type == RPAR: nesting -= 1

            if type != SPACE:
                # TODO # type = classify(lexeme)
                yield type, lexeme, lc(l, c)
        s = s[len(lexeme):]
        p += len(lexeme)
        c += len(lexeme)
        if type == EOL:
            c = 1
            l += 1
    col = 0
    while col < levels[-1]:
        levels.pop()
        yield DEDENT, '', lc(l, c)

def lc(l, c): return '%d:%d' % (l, c)

def spaces(n):
    return ''.join(( ' ' for i in xrange(n) ))

def scan(s):
    for type in scan_order:
        p = rules[type]
        m = p.match(s)
        if m: return type, m.group(0)
    return INVALID, ''

def compile_res(**R):
    for k in R.iterkeys(): R[k] = re.compile(R[k])
    return R

def name(t): return t

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
QUOTE   = 'QUOTE'
STR     = 'STR'
SPACE   = 'SPACE'
EOL     = 'EOL'

scan_order = (DEC, INT, SMESS, IMESS, NAME, ASSIGN, DOT, DOTS, LPAR, RPAR, QUOTE, STR, EOL, SPACE)

# special chars are . : ( ) '
rules = compile_res(
    DEC    = r'-?[0-9]+\.[0-9]*',
    INT    = r'-?[0-9]+',
    SMESS  = r"\.[^()'" + '"' + "\s#:\.][^()'" + '"' + "\s#]*",
    IMESS  = r":[^()'" + '"' + "\s#:\.][^()'" + '"' + "\s#]*",
    NAME   = r"[^()'" + '"' + "\s#:\.][^()'" + '"' + "\s#]*",
    ASSIGN = r'::',
    DOT    = r'\.',
    DOTS   = r':',
    LPAR   = r'\(',
    RPAR   = r'\)',
    QUOTE  = r"'",
    STR    = r'"[^"]*"',
    EOL    = r' *(:?#.*)?\n',
    SPACE  = r' +',
)

