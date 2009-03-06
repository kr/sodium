#include "gen.h"
#include "obj.h"
#include "mem.h"
#include "int.h"
#include "prim.h"

#include "vm.h"

static datum
get_primitive_surrogate(datum d)
{
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
    if (intp(d)) return (method_table) int_mtab;

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

    table = obj_method_table(d);

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

    /* This pointer is stable. A garbage collection will not invalidate it */
    table = obj_method_table(d);

    n = datum2int(table->size);
    for (i = 0; i < n; ++i) methods = cons(table->items[i].name, methods);
    return methods;
}
