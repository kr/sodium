from env import *

l = [0]
def traced(f):
    def d(*a):
        s = ' ' * l[0]
        print s + ('calling %s%s' % (f.__name__, rep(a)))
        l[0] += 1
        r = f(*a)
        l[0] -= 1
        print s+'-->', r
        return r
    return d

