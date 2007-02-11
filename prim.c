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
    } else if (pairp(proc)) {
        p = prim_pair;
    } else if (proc == nil) {
        p = prim_nil;
    } else if (symbolp(proc)) {
        p = prim_sym;
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

static datum
prim_pair_set_cdr(datum rcv, datum args)
{
    pair p = rcv;
    cdr(p) = car(args);
    return ok_sym;
}

static datum
prim_pair_car(datum rcv, datum args)
{
    pair p = rcv;
    return car(p);
}

static datum
prim_pair_cdr(datum rcv, datum args)
{
    pair p = rcv;
    return cdr(p);
}

static datum
prim_pair_get(datum rcv, datum args)
{
    int i;
    if (!intp(car(args))) die1("prim_pair -- not an int", car(args));
    i = datum2int(car(args));
    return array_get(rcv, i);
}

static datum
prim_pair_put(datum rcv, datum args)
{
    int i;
    if (!intp(car(args))) die1("prim_pair -- not an int", car(args));
    i = datum2int(car(args));
    array_put(rcv, i, cadr(args));
    return ok_sym;
}

prim_meth
prim_pair(datum rcv, datum message)
{
    if (message == set_cdr_sym) return prim_pair_set_cdr;
    if (message == car_sym) return prim_pair_car;
    if (message == cdr_sym) return prim_pair_cdr;
    if (message == get_sym) return prim_pair_get;
    if (message == put_sym) return prim_pair_put;
    return (prim_meth) die1("prim_pair -- unknown message", message);
}

prim_meth
prim_nil(datum proc, datum message)
{
    return (prim_meth) die1("prim_nil -- unknown message", message);
}

prim_meth
prim_sym(datum rcv, datum message)
{
    /*char *s = (char *) rcv;*/
    pr(rcv);
    return (prim_meth) die1("prim_sym -- unknown message", message);
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
