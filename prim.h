#ifndef prim_h
#define prim_h

#include "gen.h"

typedef datum(*prim_meth)(datum, datum);
typedef prim_meth(*prim)(datum, datum);

prim_meth get_primitive_method(datum proc, datum message);
datum apply_prim_meth(prim_meth meth, datum proc, datum argl);

void pr(datum d);

prim_meth prim_pair(datum rcv, datum msg);
prim_meth prim_nil(datum rcv, datum msg);
prim_meth prim_sym(datum rcv, datum msg);

#endif /*prim_h*/
