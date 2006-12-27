
def pr(obj): print obj

def rep(l):
    if isinstance(l, tuple):
        return '(' + ' '.join(map(rep, l)) + ')'
    return repr(l)

def error(s):
    raise RuntimeError, s

def isp(a, b): return a is b

class make_dict(dict):
    def get(self, k):
        return self[k]
    def put_(self, k, v):
        self[k] = v
    def delete_(self, k):
        del self[k]
setattr(make_dict, 'put!', make_dict.put_)
setattr(make_dict, 'delete!', make_dict.delete_)

