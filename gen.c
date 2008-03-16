#include <stdio.h>
#include <stdlib.h>
#include "gen.h"
#include "mem.h"
#include "obj.h"
#include "st.h"
#include "prim.h"
#include "vm.h"

#define DUMP_CORE (*(int*)0=0)

int
truep(datum d)
{
    return !!d;
}

void
die(const char *m)
{
    fprintf(stderr, "\n%s\n", m);
    DUMP_CORE;
}

datum
die1(const char *m, datum d)
{
    fprintf(stderr, "\n%s: ", m);
    pr(d);
    DUMP_CORE;
    return nil;
}

datum
memq(datum d, datum list)
{
    for (; list; list = cdr(list)) {
        if (car(list) == d) return list;
    }
    return nil; /* false */
}

datum
assq(datum d, datum alist)
{
    for (; alist; alist = cdr(alist)) {
        if (!arrayp(car(alist))) die("assq -- alist must be a list of pairs");
        if (caar(alist) == d) return car(alist);
    }
    return nil; /* false */
}

