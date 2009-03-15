import sys
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

def print_context(err):
  if hasattr(err, 'context'):
    for exp in err.context:
      exp = str(exp)
      if len(exp) > 70: exp = exp[:70] + '...'
      print >>sys.stderr, '  at', exp

def report_compile_error(ex, file=None, line=None, char=None):
  location = '<unknown>'
  if file: location = file
  if line:
    location += ':' + str(line)
    if char: location += ':' + str(char)
  print >>sys.stderr, '%s: Compile Error: %s' % (location, ex)
  print_context(ex)
  exit(3)
