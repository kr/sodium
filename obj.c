#include <stdio.h>
#include "gen.h"
#include "obj.h"
#include "pair.h"
#include "st.h"
#include "prim.h"

#include "vm.h"

datum int_surrogate, str_surrogate, pair_surrogate, nil_surrogate,
      symbol_surrogate;

static datum
get_primitive_surrogate(datum d)
{
    if (d == nil) return nil_surrogate;
    if (intp(d)) return int_surrogate;
    if (symbolp(d)) return symbol_surrogate;

    if (bytesp(d)) return str_surrogate;
    if (pairp(d)) return pair_surrogate;
    return nil;
}

static datum
replace_1st_of_1st(datum env, datum x)
{
  datum frame;

  regs[R_VM0] = cdr(env);
  frame = cons(x, cdar(env));
  return cons(frame, regs[R_VM0]);
}

datum
closure_env(datum d)
{
    datum surrogate;

    if ((surrogate = get_primitive_surrogate(d))) {
        datum env = closure_env(surrogate);
        return replace_1st_of_1st(env, d);
    }

    if (!closurep(d)) die1("not a closure", d);
    return car(d);
}

uint *
closure_method(datum d, datum name)
{
    int n;
    datum *table;
    datum surrogate;

    if ((surrogate = get_primitive_surrogate(d))) {
        return closure_method(surrogate, name);
    }

    table = (datum *) cdr(d);

    n = datum2int(*(table++));
    for (; n--; table += 2) {
        if (*table == name) return (uint *) *(table + 1);
    }
    return die1("closure_method -- no such method", name);
}

int
closure_has_method(datum d, datum name)
{
    int n;
    datum *table;
    datum surrogate;

    if ((surrogate = get_primitive_surrogate(d))) {
        return closure_has_method(surrogate, name);
    }

    if (!closurep(d)) die1("closure_has_method -- bad closure", d);

    table = (datum *) cdr(d);

    n = datum2int(*(table++));
    for (; n--; table += 2) {
        if (*table == name) return 1;
    }
    return 0;
}

int
closures_same_type(datum a, datum b)
{
    datum surrogate;

    if ((surrogate = get_primitive_surrogate(a))) {
      return closures_same_type(surrogate, b);
    }

    if ((surrogate = get_primitive_surrogate(b))) {
      return closures_same_type(a, surrogate);
    }

    return cdr(a) == cdr(b);
}

datum
closure_methods(datum d)
{
    int n;
    datum *table;
    datum surrogate, methods = nil;

    if ((surrogate = get_primitive_surrogate(d))) {
        return closure_methods(surrogate);
    }

    if (!closurep(d)) die1("closure_methods -- bad closure", d);

    /* This pointer is stable. A garbage collection will not invalidate it */
    table = (datum *) cdr(d);

    n = datum2int(*(table++));
    for (; n--; table += 2) methods = cons(*table, methods);
    return methods;
}
