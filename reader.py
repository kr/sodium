import re
import lx
import lexer as T
from pair import cons, list, nil
from util import traced, report_compile_error

current_pos_info = {}
def record_pos_info(f):
    def d(self, *a, **k):
        p = self.p
        r = f(self, *a, **k)
        if isinstance(r, (cons, tuple)) and r:
          current_pos_info[r] = p
        return r
    return d

class ReadError(RuntimeError):
    pass

# In BNF comments, lower case names are nonterminals and
# ALL CAPS names are terminals.

MESSAGE_TOKENS = (T.BINOP, T.UNOP)
class Parser:
    def __init__(self, tokens):
        current_pos_info.clear()
        self.tokens = tokens
        self.next()

    def __repr__(self): return '[parser]'

    def next(self):
        if not self.tokens: return
        self.peek, self.lexeme, self.p = self.tokens.pop(0)

    def xmatch(self, *types):
        peek, lexeme, pos = self.peek, self.lexeme, self.p
        self.match(*types)
        return peek, lexeme, pos

    def match(self, *types):
        peek, lexeme = self.peek, self.lexeme
        if peek not in types:
            if len(types) > 1:
                more = '\n  expected one of: ' + ', '.join(map(T.name, types))
            else:
                more = ' expected ' + T.name(types[0])
            ex = ReadError('found %s,%s' % (T.name(peek), more))
            report_compile_error(ex,
                    file=self.p[0], line=self.p[1], char=self.p[2])
        self.next()
        return lexeme

    def try_match(self, *types):
        return (self.peek in types) and self.match(*types)

    def parse(self): return self.__program()

    @record_pos_info
    def __program(self):
        '''
        prot: line* EOF

        '''

        lines = self.match_loop(self.__line, T.EOF)
        self.match(T.EOF)
        return lines


    @record_pos_info
    def __line(self):
        '''
        line: expr EOL
        '''
        expr = self.__expr(T.EOL)
        self.match(T.EOL)
        return expr

    @record_pos_info
    def __expr(self, *follow):
        '''
        expr: head binexpr*
            : head binexpr* "." expr
            : head binexpr* ":" expr
            : head binexpr* ":" EOL block
        '''
        head = self.__head(T.DOT, T.DOTS, *follow)
        args = self.match_loop(self.__binexpr, T.DOT, T.DOTS, *follow)
        body = head.append(args)

        if self.peek in follow:
          if len(body) == 1: return body.car() # remove extra parens
          return body

        if self.peek == T.DOT:
          self.match(T.DOT)
          more = self.__expr(*follow)
        elif self.peek == T.DOTS:
          self.match(T.DOTS)
          if self.try_match(T.EOL):
              more = self.__block()
          else:
              more = list(self.__expr(*follow))
        else:
          raise RuntimeError('internal error')

        return body.append(more)

    @record_pos_info
    def __head(self, *follow):
        '''
        head:
            : BINOP UNOP* [BINOP]
            : atom UNOP* [BINOP]
        '''
        if self.peek in follow: return nil

        head = self.__atom(T.BINOP)

        if self.peek not in (T.BINOP, T.UNOP): return list(head)

        while self.peek in (T.UNOP):
          head = list(head, lx.S(self.match(T.UNOP)))

        if self.peek in (T.BINOP):
          head = list(head, lx.S(self.match(T.BINOP)))

        return head

    @record_pos_info
    def __block(self):
        '''
        block: INDENT line* DEDENT
        '''
        self.match(T.INDENT)
        lines = self.match_loop(self.__line, T.DEDENT)
        self.match(T.DEDENT)
        return lines

    @record_pos_info
    def __binexpr(self):
        '''
        binexpr: unexpr
               : unexpr BINOP binexpr
        '''
        left = self.__unexpr()
        if self.peek != T.BINOP: return left

        message = lx.S(self.match(T.BINOP))
        right = self.__binexpr()
        return list(left, message, right)

    @record_pos_info
    def __unexpr(self):
        '''
        unexpr: atom UNOP*
        '''
        expr = self.__atom()
        while self.peek == T.UNOP:
            expr = list(expr, lx.S(self.match(T.UNOP)))

        return expr

    @record_pos_info
    def __atom(self, *extra):
        '''
        atom : NAME
             : DEC
             : DECF
             : HEX
             : STR
             : HEREDOC
             : "(" expr ")"
             : "[" expr "]"
             : "'" atom
        '''
        if self.peek == T.LPAR:
            self.match(T.LPAR)
            expr = self.__expr(T.RPAR)
            self.match(T.RPAR)
            return expr

        if self.peek == T.LSQU:
            self.match(T.LSQU)
            expr = self.__expr(T.RSQU)
            self.match(T.RSQU)
            return list(lx.S('bracket'), expr)

        if self.peek == T.QUOTE:
            self.match(T.QUOTE)
            atom = self.__atom()
            return list(lx.S('quote'), atom)

        type, lexeme, pos = self.xmatch(T.DEC, T.DECF, T.HEX, T.NAME, T.STR, T.HEREDOC, *extra)
        if type == T.DEC:
            return lx.Integer(lexeme)
        if type == T.HEX:
            return lx.Integer(lexeme, 16)
        if type == T.NAME:
            return lx.S(lexeme)
        if type == T.STR:
            return lx.String(unescape(lexeme[1:-1], pos)).setpos(pos)
        if type == T.DECF:
            return lx.Decimal(lexeme)
        if type == T.HEREDOC:
            return lx.ForeignString(lexeme).setpos(pos)
        return lx.S(lexeme)

    @record_pos_info
    def match_loop(self, parse, *sentinels):
        if self.peek in sentinels: return nil
        first = parse()
        return cons(first, self.match_loop(parse, *sentinels))

def report_bad_escape(pos, s, i):
    ex = ReadError('bad escape sequence %s' % s[i:i + 4])
    report_compile_error(ex, file=pos[0], line=pos[1], char=pos[2] + 1 + i)

def unescape(s, pos):
    i = 0
    l = len(s)
    r = [ ]
    while i < l:
        c = s[i]
        i += 1
        if c == '\\':
            if s[i] in '01234567':
              n = int(s[i])
              i += 1
              if i < l and s[i] in '01234567':
                n = (n << 3) | int(s[i])
                i += 1
                if i < l and s[i] in '01234567':
                  n = (n << 3) | int(s[i])
                  i += 1

              c = chr(n)
            elif s[i] == 'x':
              i += 1
              if i >= l: report_bad_escape(pos, s, i - 2)
              if s[i] not in '0123456789abcdef':
                report_bad_escape(pos, s, i - 2)

              n = int(s[i], 16)
              i += 1
              if i < l and s[i] in '0123456789abcdef':
                n = (n << 4) | int(s[i], 16)
                i += 1

              c = chr(n)
            else:
              c = tab[s[i]]
              i += 1
        r.append(c)
    return ''.join(r)

tab = {
    'n':"\n",
    't':"\t",
    'v':"\v",
    'b':"\b",
    'r':"\r",
    'f':"\f",
    'a':"\a",
    '\\':"\\",
    '?':"?",
    "'":"'",
    '"':'"',
}

def parse(tokens):
    return Parser(tokens).parse()

def read(s, name='<string>'):
    return parse(T.lex(s, name))

if __name__ == '__main__':
    import sys
    s = open(sys.argv[1]).read()
    prog = read(s, sys.argv[1])
    for expr in prog:
      print expr
