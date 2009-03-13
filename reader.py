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
        if isinstance(p, (cons, tuple, list)):
          current_pos_info[r] = p
        return r
    return d

class ReadError(RuntimeError):
    pass

# In BNF comments, lower case names are nonterminals and
# ALL CAPS names are terminals.

MESSAGE_TOKENS = (T.IMESS, T.SMESS, T.ASSIGN)
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

    def __program(self):
        '''
program : stmt* EOF
        '''
        stmts = self.match_loop(self.__stmt, T.EOF)
        self.match(T.EOF)
        return stmts

    @record_pos_info
    def __stmt(self):
        '''
stmt : mole
     | mole EOL
        '''

        exprs = self.__mole(T.EOL)
        self.try_match(T.EOL)
        return exprs


    @record_pos_info
    def __mole(self, *follow):
        '''
mole : 
     | '.' expr
     | IMESS stuff
     | expr stuff

     | expr tail
     | expr SMESS tail
     | expr IMESS tail
     | NAME '::' expr
        '''

        if self.peek in follow: return nil
        if self.peek == T.DOT:
          self.match(T.DOT)
          return self.__expr()
        if self.peek in MESSAGE_TOKENS:
          first = lx.S(self.match(*MESSAGE_TOKENS))
        else:
          first = self.__expr()

        strip = (self.peek in MESSAGE_TOKENS)
        res = self.__molex(first, *follow)
        if strip: res = res.car()

        if self.peek == T.DOTS:
            x = res.append(self.__tail(*follow))
            return x

        return res

    def __message(self):
      stype, lexeme, pos = self.xmatch(*MESSAGE_TOKENS)
      if stype == T.ASSIGN: return lx.S(lexeme)
      return lx.Mess(lexeme)

    def gobble_messages_reverse(self, l):
      if self.peek not in MESSAGE_TOKENS: return l
      return self.gobble_messages_reverse(cons(self.__message(), l))

    def make_head(self, l):
      a = l.car()
      d = l.cdr()
      if d.nullp(): return a
      return list(self.make_head(d), a)

    def __molex(self, first, *follow):
      '''
molex : MESS* tail
      '''

      rhead = self.gobble_messages_reverse(list(first))
      tail = cons(rhead.car(), self.__inner_tail(*follow + (T.DOTS,)))
      if rhead.cdr().nullp():
        return tail
      head = self.make_head(rhead.cdr())
      x = list(cons(head, tail))
      return x

    def __inner_tail(self, *follow):
        '''
inner_tail :
           | expr inner_tail
        '''
        if self.peek in follow: return nil
        return self.__molex(self.__expr(), *follow)


    def __tail(self, *follow):
        '''
tail :
     | ':' mole
     | ':' EOL INDENT stmt+ DEDENT
        '''

        if self.peek != T.DOTS: return nil

        self.match(T.DOTS)
        if self.peek != T.EOL: return list(self.__mole(*follow))
        self.match(T.EOL)
        self.match(T.INDENT)
        exprs = self.match_loop(self.__stmt, T.DEDENT)
        self.match(T.DEDENT)
        return exprs

    @record_pos_info
    def __expr(self):
        '''
expr : atom
     | '(' ')'
     | '(' mole ')'
     | '[' mole ']'
     | "'" expr
        '''

        if self.peek == T.LPAR:
            self.match(T.LPAR)
            mole = self.__mole(T.RPAR)
            self.match(T.RPAR)
            return mole
        elif self.peek == T.LSQU:
            self.match(T.LSQU)
            mole = self.__mole(T.RSQU)
            self.match(T.RSQU)
            return list(lx.S(':shorthand-fn:'), mole)
        elif self.peek == T.QUOTE:
            self.match(T.QUOTE)
            expr = self.__expr()
            return list(lx.S('quote'), expr)
        else:
            return self.__atom()

    def __atom(self):
        '''
atom : NAME
     | INT
     | DEC
     | STR
     | HEREDOC
        '''
        type, lexeme, pos = self.xmatch(T.INT, T.DEC, T.NAME, T.STR, T.HEREDOC)
        if type == T.INT:
            return lx.Integer(lexeme)
        if type == T.NAME:
            return lx.S(lexeme)
        if type == T.STR:
            return lx.String(unescape(lexeme[1:-1])).setpos(pos)
        if type == T.DEC:
            return lx.Decimal(lexeme)
        if type == T.HEREDOC:
            return lx.ForeignString(lexeme).setpos(pos)

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

def make_invoke(op, first, lexeme, tail):
    return list(lx.S(op), first, (lx.S('quote'), lx.S(lexeme[1:])),
            (lx.S('list'),) + tail)

def parse(tokens):
    return Parser(tokens).parse()

def read(s, name='<string>'):
    return parse(T.lex(s, name))

if __name__ == '__main__':
    import sys
    s = open(sys.argv[1]).read()
    print read(s, sys.argv[1])
