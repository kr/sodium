#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include "gen.h"
#include "prim.h"
#include "obj.h"
#include "st.h"
#include "vm.h"

static prim_meth
prim_dummy(datum rcv, datum msg)
{
    die("prim_dummy -- can't happen");
    return (prim_meth) nil;
}

prim_meth
get_primitive_method(datum proc, datum message)
{
    prim p = prim_dummy;
    if (!symbolp(message)) {
        die1("get_primitive_method -- not a symbol", message);
    }
    if (compiled_objp(proc)) {
        die1("get_primitive_method -- not a primitive", proc);
    } else if (addrp(proc)) {
        die("addresses bomb out");
    } else if (blankp(proc)) {
        p = (prim) car(proc);
    } else {
        die1("get_primitive_method -- unknown object", proc);
    }
    return p(proc, message);
}

datum
apply_prim_meth(prim_meth meth, datum proc, datum argl)
{
    return meth(proc, argl);
}

/* global functions */

static void prx(datum d);

static void
pr_bare(datum d, char *sp)
{
    uint i, len;
    if (d == nil) return;
    fputs(sp, stdout);
    prx(car(d));
    len = array_len(d);
    if ((len == 2) && (pairp(cdr(d)) || cdr(d) == nil)) {
        pr_bare(cdr(d), " ");
    } else {
        for (i = 1; i < len; i++) {
            fputs(" . ", stdout);
            prx(array_get(d, i));
        }
    }
}

static void
prx(datum d)
{
    if (intp(d)) {
        printf("%d", datum2int(d));
    } else if (pairp(d)) {
        printf("(");
        pr_bare(d, "");
        printf(")");
    } else if (stringp(d)) {
        printf("%s", string_contents(d));
    } else if (compiled_objp(d)) {
        printf("<obj %p>", d);
    } else if (symbolp(d)) {
        pr_symbol(d);
    } else if (addrp(d)) {
        printf("<addr %p>", d);
    } else if (!d) {
        printf("()");
    } else if (blankp(d)) {
        printf("<blank>");
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
