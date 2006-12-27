import re
import lx
import lexer as T
from pair import cons, list, nil

# lower case names are nonterminals
# ALL CAPS names are terminals

BNF = r'''

program : stmt* EOF

stmt : mole EOL
     : expr '.' EOL
     | mole ':' EOL INDENT stmt+ DEDENT

expr : atom
     | '(' ')'
     | '(' mole ')'
     | "'" expr

mole : expr+
     | expr SMESS expr*
     | expr IMESS expr*
     | NAME '::' expr

atom : NAME
     | INT
     | DEC
     | STR


'''


class Parser:
    def __init__(self, tokens):
        self.tokens = tokens
        self.next()

    def next(self):
        try:
            self.peek, self.lexeme, self.p = self.tokens.next()
        except StopIteration, e:
            self.peek, self.lexeme, self.p = T.EOF, '', 'EOF'

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
stmt : mole EOL
     : expr '.' EOL
     | mole ':' EOL INDENT stmt+ DEDENT
        '''

        first = self.__expr()
        if self.peek == T.DOT:
            self.match(T.DOT)
            self.match(T.EOL)
            return first
        rexprs = self.__mole_tail(first, T.EOL, T.DOTS)
        extra = nil
        terminator, x = self.xmatch(T.EOL, T.DOTS)
        if terminator == T.DOTS:
            self.match(T.EOL)
            self.match(T.INDENT)
            extra = self.match_loop(self.__stmt, T.DEDENT)
            self.match(T.DEDENT)
        return rexprs.reverse(extra)

    # returns list in reverse order
    def __mole_tail(self, first, *follow):
        '''
mole : expr+
     | expr SMESS expr*
     | expr IMESS expr*
     | NAME ':=' expr
        '''

        rtail = list(first)
        stype, lexeme = self.peek, None
        if stype in (T.SMESS, T.IMESS, T.ASSIGN):
            stype, lexeme = self.xmatch(T.SMESS, T.IMESS, T.ASSIGN)
            if stype == T.ASSIGN:
                # TODO check that first is a NAME
                return list(self.__expr(), first, lx.S('set!'))
            rtail = cons(lx.S(lexeme), rtail)

        while self.peek not in follow:
            rtail = cons(self.__expr(), rtail)
        return rtail

    def __expr(self):
        '''
expr : atom
     | '(' ')'
     | '(' mole ')'
     | "'" expr
        '''

        if self.peek == T.LPAR:
            self.match(T.LPAR)
            if self.peek == T.RPAR:
                mole = nil
            else:
                mole = self.__mole(T.RPAR)
            self.match(T.RPAR)
            return mole
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
        '''
        type, lexeme = self.xmatch(T.INT, T.DEC, T.NAME, T.STR)
        if type == T.INT:
            return lx.Integer(lexeme)
        if type == T.NAME:
            return lx.S(lexeme)
        if type == T.STR:
            return lx.String(lexeme[1:-1])
        if type == T.DEC:
            return lx.Decimal(lexeme)

    def __mole(self, *follow):
        return self.__mole_tail(self.__expr(), *follow).reverse()

    def match_loop(self, parse, *sentinels):
        rl = nil
        while self.peek not in sentinels:
            rl = cons(parse(), rl)
        return rl.reverse()

def make_invoke(op, first, lexeme, tail):
    return list(lx.S(op), first, (lx.S('quote'), lx.S(lexeme[1:])),
            (lx.S('list'),) + tail)

def parse(tokens):
    return Parser(tokens).parse()

def read(s):
    return parse(T.lex(s))
