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
    for (; list; list = item1(list)) {
        if (item0(list) == d) return list;
    }
    return nil; /* false */
}

datum
assq(datum d, datum alist)
{
    for (; alist; alist = item1(alist)) {
        if (!arrayp(item0(alist))) die("assq -- alist must be a list of pairs");
        if (item00(alist) == d) return item0(alist);
    }
    return nil; /* false */
}

