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

    if (stringp(obj)) return str_surrogate;
    if (pairp(obj)) return pair_surrogate;
    return nil;
}

datum
compiled_obj_env(datum obj)
{
    datum surrogate;

    if ((surrogate = get_primitive_surrogate(obj))) {
        datum frame = cons(obj, nil);
        return cons(frame, genv);
    }

    if (!objp(obj)) die1("not a compiled object", obj);
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
