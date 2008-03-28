#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include "gen.h"
#include "prim.h"
#include "obj.h"
#include "st.h"
#include "vm.h"

/* global functions */

static void
pr_array(datum d)
{
    uint i, len;
    if (d == nil) return;
    prx(item0(d));
    len = array_len(d);
    for (i = 1; i < len; i++) {
        fputs(" ", stdout);
        prx(array_get(d, i));
    }
}

static void
pr_pair(datum d, char *sp)
{
    if (d == nil) return;
    fputs(sp, stdout);
    prx(car(d));
    if (pairp(cdr(d)) || cdr(d) == nil) {
        pr_pair(cdr(d), " ");
    } else {
        fputs(" . ", stdout);
        prx(cdr(d));
    }
}

void
prx(datum d)
{
    if (!d) {
        printf("()");
    } else if (intp(d)) {
        printf("%d", datum2int(d));
    } else if (addrp(d)) {
        printf("<addr %p>", d);
    } else if (symbolp(d)) {
        pr_symbol(d);
    } else if (strp(d)) {
        str s = datum2str(d);
        printf("%*s", s->size, s->data);
    } else if (bytesp(d)) {
        printf("%s", bytes_contents(d));
    } else if (pairp(d)) {
        printf("(");
        pr_pair(d, "");
        printf(")");
    } else if (arrayp(d)) {
        printf("(array ");
        pr_array(d);
        printf(")");
    } else if (closurep(d)) {
        printf("<closure %p>", d);
    } else if (broken_heartp(d)) {
        printf("<broken heart %p>:\n", ((chunk) d)->datums[0]);
        prx(((chunk) d)->datums[0]);
    } else {
        printf("<unknown-object %p>", d);
    }
}

void
pr(datum d)
{
    prx(d);
    printf("\n");
}
