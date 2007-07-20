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
get_primitive_surrogate(datum obj)
{
    if (obj == nil) return nil_surrogate;
    if (intp(obj)) return int_surrogate;
    if (symbolp(obj)) return symbol_surrogate;

    if (bytesp(obj)) return str_surrogate;
    if (pairp(obj)) return pair_surrogate;
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
compiled_obj_env(datum obj)
{
    datum surrogate;

    if ((surrogate = get_primitive_surrogate(obj))) {
        datum env = compiled_obj_env(surrogate);
        return replace_1st_of_1st(env, obj);
    }

    if (!objp(obj) && !undeadp(obj)) die1("not a compiled object", obj);
    return car(obj);
}

uint *
compiled_obj_method(datum obj, datum name)
{
    int n;
    datum *table;
    datum surrogate;

    if ((surrogate = get_primitive_surrogate(obj))) {
        return compiled_obj_method(surrogate, name);
    }

    table = (datum *) cdr(obj);

    n = datum2int(*(table++));
    for (; n--; table += 2) {
        if (*table == name) return (uint *) *(table + 1);
    }
    return die1("compiled_obj_method -- no such method", name);
}

int
compiled_obj_has_method(datum obj, datum name)
{
    int n;
    datum *table;
    datum surrogate;

    if ((surrogate = get_primitive_surrogate(obj))) {
        return compiled_obj_has_method(surrogate, name);
    }

    if (!objp(obj)) die1("compiled_obj_has_method -- bad object", obj);

    table = (datum *) cdr(obj);

    n = datum2int(*(table++));
    for (; n--; table += 2) {
        if (*table == name) return 1;
    }
    return 0;
}

int
compiled_objs_same_type(datum obj1, datum obj2)
{
    datum surrogate;

    if ((surrogate = get_primitive_surrogate(obj1))) {
      return compiled_objs_same_type(surrogate, obj2);
    }

    if ((surrogate = get_primitive_surrogate(obj2))) {
      return compiled_objs_same_type(obj1, surrogate);
    }

    return cdr(obj1) == cdr(obj2);
}

datum
compiled_obj_methods(datum obj)
{
    int n;
    datum *table;
    datum surrogate, methods = nil;

    if ((surrogate = get_primitive_surrogate(obj))) {
        return compiled_obj_methods(surrogate);
    }

    if (!objp(obj)) die1("compiled_obj_methods -- bad object", obj);

    /* This pointer is stable. A garbage collection will not invalidate it */
    table = (datum *) cdr(obj);

    n = datum2int(*(table++));
    for (; n--; table += 2) methods = cons(*table, methods);
    return methods;
}
