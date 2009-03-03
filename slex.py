import re

def nop(*a, **k): pass
def I(a): return a

class Lexer:
    def __init__(self, rules, init=nop, before=nop, after=nop, filter=I):
        def compile(rules):
            def even_out(rules):
                res = []
                for r in rules:
                    if len(r) < 3: r = r + (nop,)
                    res.append(r)
                return res

            crules = []
            for name, pat, action in even_out(rules):
                r = re.compile(pat, re.DOTALL)
                crules.append((name, r, action))
            return crules

        self.init = init
        self.before = before
        self.after = after
        self.filter = filter
        self.rules = compile(rules)
        self.ruled = dict(((n,(r,a)) for n,r,a in self.rules))

    def add_rule(self, name, pat, action):
        t = (re.compile(pat, re.DOTALL), action)
        self.rules.append((name,) + t)
        self.ruled[name] = t

    def rm_rule(self, name):
        self.rules.remove((name,) + self.ruled.pop(name))

    def lex(self, s, fname='<string>'):
        def more(s):
            def choose(s):
                def better(a, b):
                    if not a: return False
                    if not b: return True
                    al = a[1]
                    bl = b[1]
                    return len(al) > len(bl)

                best = None
                for name, r, action in self.rules:
                    m = r.match(s)
                    if m:
                        lexeme = m.group(0)
                        if (not best) or len(lexeme) > len(best[1]):
                            best = (name, lexeme)

                if not best: raise 'lexical error at %s:%d:%d' % pos()
                return best

            def update_line_count(lexeme):
                nlc = lexeme.count('\n')
                self.line += nlc
                if nlc == 0:
                    self.col += len(lexeme) - lexeme.rfind('\n')
                else:
                    self.col = 1

            def decorate(toks, p):
                return [(n,l,p) for n,l in toks]

            def pos():
                return self.fname, self.line, self.col

            name, lexeme = choose(s)
            rest = s[len(lexeme):]

            prepend = self.before(self, rest, name, lexeme, pos())
            if prepend is None: prepend = ()
            prepend = decorate(prepend, pos())

            r, action = self.ruled[name]
            res = action(self, rest, name, lexeme, pos())
            if res is None: res = ((name, lexeme),)
            res = decorate(res, pos())

            update_line_count(lexeme)

            append = self.after(self, rest, name, lexeme, pos())
            if append is None: append = ()
            append = decorate(append, pos())

            return self.filter(prepend + res + append), rest

        self.fname = fname
        self.line, self.col = 1, 1
        self.init(self, s)
        tokens = []
        while True:
            toks, s = more(s)
            tokens.extend(toks)
            if len(tokens) and tokens[-1][0] == 'EOF': break
        return tokens
