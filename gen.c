#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include "gen.h"
#include "mem.h"
#include "symbol.h"
#include "prim.h"
#include "vm.h"

#define DUMP_CORE (*(int*)0=0)

int
truep(datum d)
{
    return d != nil;
}

void
die(const char *m)
{
    size_t n = strlen(m);
    write(2, "\n", 1);
    write(2, m, n);
    write(2, "\n", 1);
    DUMP_CORE;
}

datum
die1(const char *m, datum d)
{
    size_t n = strlen(m);
    write(2, "\n", 1);
    write(2, m, n);
    write(2, ": ", 1);
    prfmt(2, "%o\n", d);
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
        if (caar(alist) == d) return car(alist);
    }
    return nil; /* false */
}

