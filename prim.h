#ifndef prim_h
#define prim_h

#include "gen.h"

typedef datum(*prim_meth)(datum, datum);
typedef prim_meth(*prim)(datum, datum);

void setup_global_env(datum env);
prim_meth get_primitive_method(datum proc, datum message);
datum apply_prim_meth(prim_meth meth, datum proc, datum argl);
int prim_funcp(datum x);

void pr(datum d);

#define MAX_PRIMS 8

/*extern prim prims[MAX_PRIMS];
extern char *prim_names[MAX_PRIMS];*/

prim_meth prim_int(datum rcv, datum msg);
prim_meth prim_pair(datum rcv, datum msg);
prim_meth prim_nil(datum rcv, datum msg);
prim_meth prim_str(datum rcv, datum msg);
prim_meth prim_sym(datum rcv, datum msg);

#endif /*prim_h*/
