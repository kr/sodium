#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include "gen.h"
#include "prim.h"
#include "obj.h"
#include "st.h"
#include "vm.h"

static prim_meth prim_isp(datum rcv, datum msg);
static prim_meth prim_cons(datum rcv, datum msg);
static prim_meth prim_make_array(datum rcv, datum msg);
static prim_meth prim_list(datum rcv, datum msg);
static prim_meth prim_rep(datum rcv, datum msg);
static prim_meth prim_pr(datum rcv, datum msg);
static prim_meth prim_error(datum rcv, datum msg);
static prim_meth prim_call(datum rcv, datum msg);
static prim_meth prim_inspector(datum rcv, datum msg);

static prim prims[MAX_PRIMS] = {
    prim_isp,
    prim_cons,
    prim_make_array,
    prim_list,
    prim_rep,
    prim_pr,
    prim_error,
    prim_call,
    prim_inspector,
};

static char *prim_names[MAX_PRIMS] = {
    "is?",
    "cons",
    "make-array",
    "list",
    "rep",
    "pr",
    "error",
    "call",
    "inspector",
};

void
setup_global_env(datum env)
{
    int i;

    for (i = 0; i < MAX_PRIMS; i++) {
        define(env, &prims[i], intern(prim_names[i]));
    }
}

int
prim_funcp(datum x)
{
    return (((prim *)x) >= prims) &&
           (((prim *)x) < &prims[MAX_PRIMS]);
}

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
    if (prim_funcp(proc)) {
        p = *(prim *) proc;
    } else if (intp(proc)) {
        p = prim_int;
    } else if (compiled_objp(proc)) {
        die1("get_primitive_method -- not a primitive", proc);
    } else if (pairp(proc)) {
        p = prim_pair;
    } else if (stringp(proc)) {
        p = prim_str;
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
prim_int_equals(datum rcv, datum args)
{
    return (datum) (rcv == car(args));
}

static datum
prim_int_lt(datum rcv, datum args)
{
    return (datum) (rcv < car(args));
}

static datum
prim_int_gt(datum rcv, datum args)
{
    return (datum) (rcv > car(args));
}

static datum
prim_int_minus(datum rcv, datum args)
{
    /* TODO check that arithmetic result doesn't overflow */
    return int2datum(datum2int(rcv) - datum2int(car(args)));
}

static datum
prim_int_plus(datum rcv, datum args)
{
    /* TODO check that arithmetic result doesn't overflow */
    return int2datum(datum2int(rcv) + datum2int(car(args)));
}

static datum
prim_int_percent(datum rcv, datum args)
{
    return int2datum(datum2int(rcv) % datum2int(car(args)));
}

prim_meth
prim_int(datum rcv, datum message)
{
    if (message == equals_sym) return prim_int_equals;
    if (message == lt_sym) return prim_int_lt;
    if (message == gt_sym) return prim_int_gt;
    if (message == minus_sym) return prim_int_minus;
    if (message == plus_sym) return prim_int_plus;
    if (message == percent_sym) return prim_int_percent;
    return (prim_meth) die1("prim_int -- unknown message", message);
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

static datum
prim_str_percent(datum rcv, datum args)
{
    uint n, i;
    datum str;
    char *f, *s, *dest, *a, *fmt = copy_string_contents(rcv);


    regs[R_VM0] = regs[R_VM1] = args;

    /* first, find a upper bound on the string size */
    n = 1; /* one for the null terminator */
    for (f = fmt; *f; f++) {
        if (*f != '%') { n++; continue; }
        switch (*++f) {
            case 'c':
                regs[R_VM1] = cdr(regs[R_VM1]);
                /* fall through */
            case '%':
                n++;
                break;
            case 'd':
                regs[R_VM1] = cdr(regs[R_VM1]);
                n += 20;
                break;
            case 'r': /* repr */
                car(regs[R_VM1]) = make_string_init("$$$");
                /* fall through */
            case 'p': /* pretty */
                car(regs[R_VM1]) = make_string_init("###");
                /* fall through */
            case 's':
                n += strlen(string_contents(car(regs[R_VM1])));
                regs[R_VM1] = cdr(regs[R_VM1]);
                break;
            default:
                free(fmt);
                die("prim_str -- unknown formatting code");
        }
    }

    s = dest = malloc(sizeof(char) * n);
    if (!dest) {
        free(fmt);
        die("prim_str -- out of memory");
    }

    /* second, format the string */
    for (f = fmt; *f; f++) {
        if (*f != '%') {
            *s++ = *f;
            continue;
        }
        switch (*++f) {
            case 'c':
                free(s);
                free(fmt);
                die("prim_str -- OOPS I have not implemented chars yet");
                regs[R_VM0] = cdr(regs[R_VM0]);
                break;
            case '%':
                *s++ = '%';
                break;
            case 'd':
                sprintf(s, "%d", datum2int(car(regs[R_VM0])));
                s += strlen(s);
                regs[R_VM0] = cdr(regs[R_VM0]);
                break;
            case 'r': /* repr */
                car(regs[R_VM0]) = make_string_init("$$$");
                /* fall through */
            case 'p': /* pretty */
                car(regs[R_VM0]) = make_string_init("###");
                /* fall through */
            case 's':
                a = string_contents(car(regs[R_VM0]));
                i = strlen(a);
                memcpy(s, a, i);
                regs[R_VM0] = cdr(regs[R_VM0]);
                s += i;
                break;
            default:
                free(s);
                free(fmt);
                die("prim_str -- unknown formatting code");
        }
    }
    *s = '\0';

    str = make_string_init(dest);
    free(dest);
    free(fmt);
    return str;
}

prim_meth
prim_str(datum rcv, datum message)
{
    if (message == percent_sym) return prim_str_percent;
    pr(rcv);
    return (prim_meth) die1("prim_str -- unknown message", message);
}

prim_meth
prim_sym(datum rcv, datum message)
{
    /*char *s = (char *) rcv;*/
    pr(rcv);
    return (prim_meth) die1("prim_sym -- unknown message", message);
}

/* global functions */

static datum
prim_isp_run(datum proc, datum args)
{
    return (datum) (car(args) == cadr(args));
}

prim_meth
prim_isp(datum proc, datum message)
{
    if (message == run_sym) return prim_isp_run;
    return (prim_meth) die1("prim_isp -- unknown message", message);
}

static datum
prim_cons_run(datum proc, datum args)
{
    return cons(car(args), cadr(args));
}

prim_meth
prim_cons(datum proc, datum message)
{
    if (message == run_sym) return prim_cons_run;
    return (prim_meth) die1("prim_cons -- unknown message", message);
}

static datum
prim_make_array_run(datum proc, datum args)
{
    if (!intp(car(args))) die1("prim_make_array -- not an int", car(args));
    return make_array(datum2int(car(args)));
}

prim_meth
prim_make_array(datum proc, datum message)
{
    if (message == run_sym) return prim_make_array_run;
    return (prim_meth) die1("prim_make_array -- unknown message", message);
}

static datum
prim_list_run(datum proc, datum args)
{
    return args;
}

prim_meth
prim_list(datum proc, datum message)
{
    if (message == run_sym) return prim_list_run;
    return (prim_meth) die1("prim_list -- unknown message", message);
}

static datum
prim_rep_run(datum proc, datum args)
{
    printf("TODO implement me -- prim_rep\n");
    return nil;
}

prim_meth
prim_rep(datum proc, datum message)
{
    if (message == run_sym) return prim_rep_run;
    return (prim_meth) die1("prim_rep -- unknown message", message);
}

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
    } else if (prim_funcp(d)) {
        printf("<prim %p>", d);
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

static datum
prim_pr_run(datum proc, datum args)
{
    if (args == nil) die("cannot print nothing");
    pr(car(args));
    return ok_sym;
}

prim_meth
prim_pr(datum proc, datum message)
{
    if (message == run_sym) return prim_pr_run;
    return (prim_meth) die1("prim_pr -- unknown message", message);
}

static datum
prim_error_run(datum proc, datum args)
{
    printf("TODO implement me -- prim_error\n");
    return nil;
}

prim_meth
prim_error(datum proc, datum message)
{
    if (message == run_sym) return prim_error_run;
    return (prim_meth) die1("prim_error -- unknown message", message);
}

static datum
prim_call_run(datum proc, datum args)
{
    datum rcv, msg, argl;
    rcv = checked_car(args);
    msg = checked_cadr(args);
    argl = checked_caddr(args);
    return call(rcv, msg, argl);
}

prim_meth
prim_call(datum proc, datum m)
{
    if (m == run_sym) return prim_call_run;
    return (prim_meth) die1("prim_call -- unknown message", m);
}

static datum
prim_inspector_run(datum proc, datum args)
{
    return (datum) compiled_obj_has_method(car(args), cadr(args));
}

prim_meth
prim_inspector(datum proc, datum m)
{
    if (m == has_methodp_sym) return prim_inspector_run;
    return (prim_meth) die1("prim_call -- unknown message", m);
}
