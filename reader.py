import re
import lx
import lexer as T
from pair import cons, list, nil
from util import traced

# In BNF comments, lower case names are nonterminals and
# ALL CAPS names are terminals.

MESSAGE_TOKENS = (T.IMESS, T.SMESS, T.ASSIGN)
class Parser:
    def __init__(self, tokens):
        self.tokens = tokens
        self.next()

    def __repr__(self): return '[parser]'

    def next(self):
        if not self.tokens: return
        self.peek, self.lexeme, self.p = self.tokens.pop(0)

    def xmatch(self, *types):
        peek, lexeme = self.peek, self.lexeme
        self.match(*types)
        return peek, lexeme

    def match(self, *types):
        peek, lexeme = self.peek, self.lexeme
        if peek not in types:
            if len(types) > 1:
                raise Exception, '%s: expected one of %s but got %s' % (self.p, map(T.name, types), T.name(peek))
            else:
                raise Exception, '%s: expected %s but got %s' % (self.p, T.name(types[0]), T.name(peek))
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

    def __stmt(self):
        '''
stmt : mole
     | mole EOL
        '''

        exprs = self.__mole(T.EOL)
        self.try_match(T.EOL)
        return exprs


    def __mole(self, *follow):
        '''
mole : 
     | '.' expr
     | expr tail
     | expr SMESS tail
     | expr IMESS tail
     | NAME '::' expr
        '''

        if self.peek in follow: return nil
        if self.peek == T.DOT:
          self.match(T.DOT)
          return self.__expr()
        first = self.__expr()

        strip = (self.peek in MESSAGE_TOKENS)
        res = self.__molex(first, *follow)
        if strip: return res.car()
        return res
        #rtail = list(first)
        #stype, lexeme = self.peek, None
        #if stype in (T.SMESS, T.IMESS, T.ASSIGN):
        #    stype, lexeme = self.xmatch(T.SMESS, T.IMESS, T.ASSIGN)
        #    if stype == T.ASSIGN:
        #        # TODO check that first is a NAME
        #        return list(lx.S('set!'), first, self.__expr())
        #    return cons(first, cons(lx.S(lexeme), self.__tail(*follow)))
        #    rtail = cons(lx.S(lexeme), rtail)

        #return cons(first, self.__tail(*follow))

    def __message(self):
      stype, lexeme = self.xmatch(*MESSAGE_TOKENS)
      return lx.S(lexeme)

    def gobble_messages_reverse(self, l):
      if self.peek not in MESSAGE_TOKENS: return l
      return self.gobble_messages_reverse(cons(self.__message(), l))

    def make_head(self, l):
      a = l.car()
      d = l.cdr()
      if d.nullp(): return a
      return list(self.make_head(d), a)

    def __molex(self, first, *follow):
      rhead = self.gobble_messages_reverse(list(first))
      tail = cons(rhead.car(), self.__tail(*follow))
      if rhead.cdr().nullp():
        return tail
      head = self.make_head(rhead.cdr())
      x = list(cons(head, tail))
      return x

      #head = messages.cdr()
      #if head.nullp(): return tail
      #return cons(x, tail)

    def __tail(self, *follow):
        '''
tail :
     | ':' mole
     | ':' EOL INDENT stmt+ DEDENT
     | expr tail
        '''

        if self.peek in follow: return nil
        if self.peek == T.DOTS:
            self.match(T.DOTS)
            if self.peek != T.EOL: return list(self.__mole(*follow))
            self.match(T.EOL)
            self.match(T.INDENT)
            exprs = self.match_loop(self.__stmt, T.DEDENT)
            self.match(T.DEDENT)
            return exprs
        return self.__molex(self.__expr(), *follow)
        #return cons(self.__expr(), self.__tail(*follow))

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
     | FOREIGN
        '''
        type, lexeme = self.xmatch(T.INT, T.DEC, T.NAME, T.STR, T.FOREIGN)
        if type == T.INT:
            return lx.Integer(lexeme)
        if type == T.NAME:
            return lx.S(lexeme)
        if type == T.STR:
            return lx.String(unescape(lexeme[1:-1]))
        if type == T.DEC:
            return lx.Decimal(lexeme)
        if type == T.FOREIGN:
            return lexeme[4:-4]

    def match_loop(self, parse, *sentinels):
        rl = nil
        while self.peek not in sentinels:
            rl = cons(parse(), rl)
        return rl.reverse()

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
