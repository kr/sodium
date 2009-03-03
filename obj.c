#include "gen.h"
#include "obj.h"
#include "mem.h"
#include "st.h"
#include "prim.h"

#include "vm.h"

datum int_surrogate, bytes_surrogate, pair_surrogate,
      symbol_surrogate;

static datum
get_primitive_surrogate(datum d)
{
    if (intp(d)) return int_surrogate;
    if (symbolp(d)) return symbol_surrogate;

    if (pairp(d)) return pair_surrogate;
    if (bytesp(d)) return bytes_surrogate;
    return 0;
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

    return (datum) *d;
}

static method_table
obj_method_table(datum d)
{
    if (intp(d) || symbolp(d)) {
        d = get_primitive_surrogate(d);
    }

    return (method_table) d[-1];
}

uint *
closure_method(datum d, datum name)
{
    int i, n;
    method_table table;

    table = obj_method_table(d);

    n = datum2int(table->size);
    for (i = 0; i < n; ++i) {
        if (table->items[i].name == name) return table->items[i].addr;
    }
    return die1("closure_method -- no such method", name);
}

int
closure_has_method(datum d, datum name)
{
    int i, n;
    method_table table;

    if (intp(d) || symbolp(d)) {
        d = get_primitive_surrogate(d);
    }

    table = (method_table) d[-1];

    n = datum2int(table->size);
    for (i = 0; i < n; ++i) {
        if (table->items[i].name == name) return 1;
    }
    return 0;
}

int
closures_same_type(datum a, datum b)
{
    return obj_method_table(a) == obj_method_table(b);
}

datum
closure_methods(datum d)
{
    int i, n;
    method_table table;
    datum methods = nil;

    if (intp(d) || symbolp(d)) {
        d = get_primitive_surrogate(d);
    }

    /* This pointer is stable. A garbage collection will not invalidate it */
    table = (method_table) d[-1];

    n = datum2int(table->size);
    for (i = 0; i < n; ++i) methods = cons(table->items[i].name, methods);
    return methods;
}
