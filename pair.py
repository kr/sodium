from typ import Integer

class cons:
    def __init__(self, a, d):
        self.a = a
        self.d = d
    def car(self): return self.a
    def cdr(self): return self.d
    def caar(self): return self.a.car()
    def cdar(self): return self.a.cdr()
    def cadr(self): return self.d.car()
    def cddr(self): return self.d.cdr()
    def caaar(self): return self.a.caar()
    def cdaar(self): return self.a.cdar()
    def cadar(self): return self.a.cadr()
    def cddar(self): return self.a.cddr()
    def caadr(self): return self.d.caar()
    def cdadr(self): return self.d.cdar()
    def caddr(self): return self.d.cadr()
    def cdddr(self): return self.d.cddr()
    def caaaar(self): return self.a.caaar()
    def cdaaar(self): return self.a.cdaar()
    def cadaar(self): return self.a.cadar()
    def cddaar(self): return self.a.cddar()
    def caadar(self): return self.a.caadr()
    def cdadar(self): return self.a.cdadr()
    def caddar(self): return self.a.caddr()
    def cdddar(self): return self.a.cdddr()
    def caaadr(self): return self.d.caaar()
    def cdaadr(self): return self.d.cdaar()
    def cadadr(self): return self.d.cadar()
    def cddadr(self): return self.d.cddar()
    def caaddr(self): return self.d.caadr()
    def cdaddr(self): return self.d.cdadr()
    def cadddr(self): return self.d.caddr()
    def cddddr(self): return self.d.cdddr()
    def set_car_ (self, x): self.a = x
    def set_cdr_ (self, x): self.d = x

    def nullp(self): return False

    def append(self, lst):
        return cons(self.car(), self.cdr().append(lst))

    def reverse(self, r=None):
        if r is None: r = nil
        return self.cdr().reverse(cons(self.car(), r))

    def map(self, f):
        return cons(f(self.car()), self.cdr().map(f))

    def len(self):
        return Integer(1 + self.cdr().len())

    def __iter__(self):
        while True:
            yield self.car()
            self = self.cdr()
            if self.nullp(): break

    def __repr__(self):
        return '(%s)' % self.bare_repr('')
                                                                                
    def bare_repr(self, sp):
        if isinstance(self.d, cons):
            return '%s%r%s' % (sp, self.a, self.d.bare_repr(' '))
        return '%s%r . %r' % (sp, self.a, self.d)

setattr(cons, 'set-car!', cons.set_car_)
setattr(cons, 'set-cdr!', cons.set_cdr_)

def pairp(x):
    return isinstance(x, cons) and not isinstance(x, nilcons)

def list(*vals):
    if not vals: return nil
    return cons(vals[0], list(*vals[1:]))

class nilcons(cons):
    new = None
    def __new__(cls, ign1, ign2):
        it = cls.__dict__.get('__it__')
        if it is not None: return it
        cls.__it__ = it = object.__new__(cls)
        return it

    def __init__(self, ign1, ign2):
        pass

    def nullp(self): return True

    def car(self): raise 'nil has no car'
    def cdr(self): raise 'nil has no cdr'

    def append(self, lst):
        return lst

    def reverse(self, r=None):
        if r is None: r = nil
        return r

    def map(self, f):
        return self

    def len(self):
        return Integer(0)

    def __iter__(self):
        return ().__iter__()

    def __repr__(self):
        return '()'
    def bare_repr(self, ign):
        return ''

nil = nilcons(None, None)

# vim: et ts=4
