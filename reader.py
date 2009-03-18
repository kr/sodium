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
            : mess mess*
            : atom mess*
        mess: BINOP
            : UNOP
        '''
        if self.peek in follow: return nil

        head = self.__atom(T.BINOP, T.UNOP)

        if self.peek not in (T.BINOP, T.UNOP): return list(head)

        while self.peek in (T.BINOP, T.UNOP):
          head = list(head, lx.S(self.match(T.BINOP, T.UNOP)))

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
        atom = self.__atom()

        first = atom
        while self.peek == T.UNOP:
            first = list(first, lx.S(self.match(T.UNOP)))

        return first

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
            return list(lx.S(':shorthand-fn:'), expr)

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
            return lx.String(unescape(lexeme[1:-1])).setpos(pos)
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

def unescape(s):
    i = 0
    l = len(s)
    r = [ ]
    while i < l:
        c = s[i]
        if c == '\\':
            i += 1
            c = tab[s[i]]
        # TODO octal and hex escape sequnces
        r.append(c)
        i += 1
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
