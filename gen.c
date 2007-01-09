#include <stdio.h>
#include <stdlib.h>
#include "gen.h"
#include "pair.h"
#include "obj.h"
#include "st.h"
#include "prim.h"
#include "vm.h"

int
truep(datum d)
{
    return !!d;
}

void
die(const char *m)
{
    fprintf(stderr, "\n%s\n", m);
    exit(1);
}

datum
die1(const char *m, datum d)
{
    fprintf(stderr, "\n%s: ", m);
    pr(d);
    exit(1);
    return nil;
}

datum
memq(datum obj, datum list)
{
    for (; list; list = cdr(list)) {
        if (car(list) == obj) return list;
    }
    return nil; /* false */
}

datum
assq(datum obj, datum alist)
{
    for (; alist; alist = cdr(alist)) {
        if (!pairp(car(alist))) die("assq -- alist must be a list of pairs");
        if (caar(alist) == obj) return car(alist);
    }
    return nil; /* false */
}

