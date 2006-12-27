import operator

class RuntimeError(Exception):
    pass

def objectify(x):
    if isinstance(x, int): return Integer(x)
    if isinstance(x, float): return Decimal(x)
    if isinstance(x, str): return String(x)
    return x

def compose(f, g): return lambda *a: f(g(*a))
op_add = compose(objectify, operator.add)
op_sub = compose(objectify, operator.sub)
op_mod = compose(objectify, operator.mod)
op_eq = compose(objectify, operator.eq)
def op_smod(f, *a): return objectify(operator.mod(f, a))

class S(str):
    all = {}
    def __new__(clas, s):
        if s not in clas.all:
            clas.all[s] = str.__new__(clas, s)
        return clas.all[s]
    def __repr__(self): return self

class Integer(int):
    pass
setattr(Integer, '+', op_add)
setattr(Integer, '-', op_sub)
setattr(Integer, '%', op_mod)
setattr(Integer, '=', op_eq)

class Decimal(float):
    pass
setattr(Decimal, '+', op_add)
setattr(Decimal, '-', op_sub)
setattr(Decimal, '%', op_mod)
setattr(Decimal, '=', op_eq)


class String(str):
    pass
setattr(String, '+', op_add)
setattr(String, '-', op_sub)
setattr(String, '%', op_smod)
setattr(String, '=', op_eq)

def pair2tuple(pair):
    return pair.car(), pair.cdr()

