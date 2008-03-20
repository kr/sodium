import re

def nop(*a, **k): pass

class Lexer:
    def __init__(self, rules, init=nop, before=nop, after=nop):
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
        self.rules = compile(rules)
        self.ruled = dict(((n,(r,a)) for n,r,a in self.rules))

    def pos(self):
        return self.fname, self.line, self.col

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

                if not best: raise 'lexical error'
                return best

            def update_line_count(lexeme):
                nlc = lexeme.count('\n')
                self.line += nlc
                if nlc == 0: self.col = 1
                self.col += len(lexeme) - lexeme.rfind('\n')

            name, lexeme = choose(s)
            rest = s[len(lexeme):]

            prepend = self.before(self, rest, name, lexeme)
            if prepend is None: prepend = ()

            r, action = self.ruled[name]
            res = action(self, rest, name, lexeme)
            if res is None: res = ((name, lexeme, self.pos()),)

            update_line_count(lexeme)

            append = self.after(self, rest, name, lexeme)
            if append is None: append = ()

            return prepend + res + append, rest

        self.fname = fname
        self.line, self.col = 1, 1
        self.init(self, s)
        tokens = []
        while True:
            toks, s = more(s)
            tokens.extend(toks)
            if len(tokens) and tokens[-1][0] == 'EOF': break
        return tokens
