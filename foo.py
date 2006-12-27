#!/usr/bin/env python2.4

import sys
import pair
import reader
import lx

#exp = reader.read('''
#def (fact n):
#    def x 1
#    def y '(a b (c d) e f)
#    pr ("computing fact %d" % n)
#    if (n = 1):
#        x .
#        (fact (n - 1)) * n
#''')

exp = reader.read('''
def x '(a b (c d))
def y '((c d) e f)
''')

#exp = exp.car()
exp = pair.cons(lx.begin_s, exp)
print 'compiling', repr(exp)

code = lx.compile(exp, lx.S('val'), lx.S('next'), pair.nil)
#print 'code is', code.format()
code.assemble(sys.stdout)
print
