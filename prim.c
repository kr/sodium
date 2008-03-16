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
pr_bare(datum d, char *sp)
{
    uint i, len;
    if (d == nil) return;
    fputs(sp, stdout);
    prx(item0(d));
    len = array_len(d);
    if ((len == 2) && (arrayp(item1(d)) || item1(d) == nil)) {
        pr_bare(item1(d), " ");
    } else {
        for (i = 1; i < len; i++) {
            fputs(" . ", stdout);
            prx(array_get(d, i));
        }
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
    } else if (bytesp(d)) {
        printf("%s", bytes_contents(d));
    } else if (arrayp(d)) {
        printf("(");
        pr_bare(d, "");
        printf(")");
    } else if (closurep(d)) {
        printf("<closure %p>", d);
    } else if (broken_heartp(d)) {
        printf("<broken heart %p>:\n", item0(d));
        prx(item0(d));
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
