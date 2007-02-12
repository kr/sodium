#include <stdio.h>
#include "gen.h"
#include "obj.h"
#include "pair.h"
#include "st.h"
#include "prim.h"

#include "vm.h"

datum int_surrogate, str_surrogate, pair_surrogate, nil_surrogate;

datum
make_compiled_obj(datum env, uint *table)
{
    pair p = cons(env, (datum) table);
    return pair2obj(p);
}

static datum
get_primitive_surrogate(datum obj)
{
    if (intp(obj)) return int_surrogate;
    if (stringp(obj)) return str_surrogate;
    if (pairp(obj)) return pair_surrogate;
    if (obj == nil) return nil_surrogate;
    return nil;
}

datum
compiled_obj_env(datum obj)
{
    pair p;
    datum surrogate;

    if ((surrogate = get_primitive_surrogate(obj))) {
        datum frame = cons(obj, nil);
        return cons(frame, genv);
    }

    if (!compiled_objp(obj)) die1("not a compiled object", obj);
    p = obj2pair(obj);
    return car(p);
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

    if (!compiled_objp(obj)) {
        return (uint *) get_primitive_method(obj, name);
    }

    table = (datum *) cdr(obj2pair(obj));

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

    if (!compiled_objp(obj)) die1("compiled_obj_has_method -- bad object", obj);

    table = (datum *) cdr(obj2pair(obj));

    n = datum2int(*(table++));
    for (; n--; table += 2) {
        if (*table == name) return 1;
    }
    return 0;
}
