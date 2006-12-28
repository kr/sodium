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

static void
dump_datum_helper(datum d, int s)
{
    int i;
    for (i = 0; i < s; i++) printf("    ");
    printf("dumping datum %p: ", d);
    if (((uint) d) == 0xffffffff) {
        printf("BROKEN HEART");
    } else if (intp(d)) {
        printf("int\n  %d", datum2int(d));
    } else if (pairp(d)) {
        printf("pair:\n");
        dump_datum_helper(car(d), s + 1);
        dump_datum_helper(cdr(d), s + 1);
    } else if (compiled_objp(d)) {
        printf("<compiled obj>");
    } else if (symbolp(d)) {
        dump_symbol(d);
    } else if (prim_funcp(d)) {
        printf("<func %p>", d);
    } else if (!d) {
        printf("nil");
    } else {
        printf("<unknown>");
    }
    printf("\n");
}

void
dump_datum(datum d)
{
    dump_datum_helper(d, 0);
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

