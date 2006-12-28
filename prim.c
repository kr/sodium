#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include "gen.h"
#include "prim.h"
#include "obj.h"
#include "st.h"
#include "vm.h"

static prim prims[MAX_PRIMS] = {
    prim_isp,
    prim_cons,
    prim_make_array,
    prim_list,
    prim_rep,
    prim_pr,
    prim_error,
    prim_call,
    prim_open,
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
    "open",
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

datum
apply_primitive_proc(datum proc, datum message, datum argl)
{
    prim p;
    if (!symbolp(message)) {
        die1("apply_primitive_proc -- not a symbol", message);
    }
    if (prim_funcp(proc)) {
        p = *(prim *) proc;
    } else if (intp(proc)) {
        p = prim_int;
    } else if (compiled_objp(proc)) {
        die1("apply_primitive_proc -- not a primitive", proc);
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
        die1("apply_primitive_proc -- unknown object", proc);
    }
    return p(proc, message, argl);
}

datum
prim_int(datum rcv, datum message, datum args)
{
    /* TODO check that arithmetic result doesn't overflow */
    /* TODO check that arithmetic result isn't a broken heart tag */
    if (message == equals_sym) return (datum) (rcv == car(args));
    if (message == minus_sym)
        return int2datum(datum2int(rcv) - datum2int(car(args)));
    if (message == plus_sym)
        return int2datum(datum2int(rcv) + datum2int(car(args)));
    die1("prim_int -- unknown message", message);
    return nil;
}

datum
prim_pair(datum rcv, datum message, datum args)
{
    int i;
    pair p = rcv;
    if (message == set_cdr_sym) {
        cdr(p) = car(args);
        return ok_sym;
    }
    if (message == car_sym) return car(p);
    if (message == cdr_sym) return cdr(p);
    if (message == get_sym) {
        if (!intp(car(args))) die1("prim_pair -- not an int", car(args));
        i = datum2int(car(args));
        return array_get(rcv, i);
    }
    if (message == put_sym) {
        if (!intp(car(args))) die1("prim_pair -- not an int", car(args));
        i = datum2int(car(args));
        array_put(rcv, i, cadr(args));
        return ok_sym;
    }
    die1("prim_pair -- unknown message", message);
    return nil;
}

datum
prim_nil(datum proc, datum message, datum args)
{
    /*if (message == car_sym) return car(p);
    if (message == cdr_sym) return cdr(p);*/
    die1("prim_nil -- unknown message", message);
    return nil;
}

#define MAX_BUF 1024

datum
prim_str(datum rcv, datum message, datum args)
{
    uint n, i;
    datum str, ar = args;
    char *f, *s, *a, *fmt = string_contents(rcv);


    if (message == percent_sym) {
        /* first, find a upper bound on the string size */
        n = 1;
        for (f = fmt; *f; f++) {
            if (*f != '%') { n++; continue; }
            switch (*++f) {
                case 'c':
                    ar = cdr(ar);
                    /* fall through */
                case '%':
                    n++;
                    break;
                case 'd':
                    ar = cdr(ar);
                    n += 20;
                    break;
                case 'r': /* repr */
                    /* fall through */
                case 'p': /* pretty */
                    car(ar) = make_string_init("###");
                    /* fall through */
                case 's':
                    n += strlen(string_contents(car(ar)));
                    ar = cdr(ar);
                    break;
                default:
                    die("prim_str -- unknown formatting code");
            }
        }

        str = make_string(n);
        s = string_contents(str);

        /* second, format the string */
        for (f = fmt; *f; f++) {
            if (*f != '%') {
                *s++ = *f;
                continue;
            }
            switch (*++f) {
                case 'c':
                    die("prim_str -- OOPS I have not implemented chars yet");
                    args = cdr(args);
                    break;
                case '%':
                    *s++ = '%';
                    break;
                case 'd':
                    sprintf(s, "%d", datum2int(car(args)));
                    s += strlen(s);
                    args = cdr(args);
                    break;
                case 'r': /* repr */
                    /* fall through */
                case 'p': /* pretty */
                    car(args) = make_string_init("###");
                    /* fall through */
                case 's':
                    a = string_contents(car(args));
                    i = strlen(a);
                    memcpy(s, a, i);
                    args = cdr(args);
                    s += i;
                    break;
                default:
                    die("prim_str -- unknown formatting code");
            }
        }

        return str;
    }
    dump_datum(rcv);
    return die1("prim_str -- unknown message", message);
}

datum
prim_sym(datum rcv, datum message, datum args)
{
    /*char *s = (char *) rcv;*/
    dump_datum(rcv);
    return die1("prim_sym -- unknown message", message);
}

static size_t
fsize(FILE *f)
{
    int r;
    struct stat sbuf;

    r = fstat(fileno(f), &sbuf);
    if (r) die("fsize -- cannot stat");
    return sbuf.st_size;
}

datum
prim_file(datum rcv, datum msg, datum args)
{
    uint r, len;
    datum str;
    FILE *f = cdr(rcv);
    char *s;

    if (!f) die("prim_file -- this file is closed");
    if (msg == read_sym) {
        len = fsize(f);
        str = make_string(len);
        for (s = string_contents(str); len; len -= r) {
            r = fread(s, sizeof(char), len, f);
            s += r;
        }
        return str;
    } else if (msg == write_sym) {
        if (args == nil) die("prim_file:write -- not enough args");
        s = string_contents(car(args));
        for (len = strlen(s); len; len -= r) {
            r = fwrite(s, sizeof(char), len, f);
            s += r;
        }
        return ok_sym;
    } else if (msg == destroy_sym || msg == close_sym) {
        fclose(f);
        cdr(rcv) = nil;
        return ok_sym;
    }
    return die1("prim_file -- unknown message", msg);
}

/* global functions */

datum
prim_isp(datum proc, datum message, datum args)
{
    if (message == run_sym) return (datum) (car(args) == cadr(args));
    return die1("prim_isp -- unknown message", message);
}

datum
prim_cons(datum proc, datum message, datum args)
{
    if (message == run_sym) return cons(car(args), cadr(args));
    return die1("prim_cons -- unknown message", message);
}

datum
prim_make_array(datum proc, datum message, datum args)
{
    if (message == run_sym) {
        if (!intp(car(args))) die1("prim_make_array -- not an int", car(args));
        return make_array(datum2int(car(args)));
    }
    return die1("prim_make_array -- unknown message", message);
}

datum
prim_list(datum proc, datum message, datum args)
{
    if (message == run_sym) return args;
    return die1("prim_list -- unknown message", message);
}

datum
prim_rep(datum proc, datum message, datum args)
{
    printf("TODO implement me -- prim_rep\n");
    return nil;
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
        printf("<unknown-object>");
    }
}

void
pr(datum d)
{
    prx(d);
    printf("\n");
}

datum
prim_pr(datum proc, datum message, datum args)
{
    if (args == nil) die("cannot print nothing");
    if (message != run_sym) die("no such method");
    pr(car(args));
    return nil;
}

datum
prim_error(datum proc, datum message, datum args)
{
    printf("TODO implement me -- prim_error\n");
    return (datum) 0;
}

datum
prim_call(datum proc, datum m, datum args)
{
    datum rcv, msg, argl;
    if (m != run_sym) return die1("prim_call -- unknown message", m);
    rcv = car(args);
    msg = cadr(args);
    argl = caddr(args);
    return call(rcv, msg, argl);
}

datum
prim_open(datum rcv, datum msg, datum args)
{
    char *mode = "r";
    datum d = make_blank(2);

    if (args == nil) die("prim_open -- not enough args");
    car(d) = prim_file;
    if (cdr(args) != nil) {
        if (cadr(args) == write_sym) {
            mode = "w";
        } else if (cadr(args) != read_sym) {
            return die1("prim_open -- unknown mode", cadr(args));
        }
    }
    cdr(d) = fopen(string_contents(car(args)), mode);
    return d;
}

datum
prim_inspector(datum proc, datum m, datum args)
{
    if (m == has_methodp_sym) {
        return (datum) complied_object_has_method(car(args), cadr(args));
    }
    return die1("prim_call -- unknown message", m);
}

