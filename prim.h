#ifndef prim_h
#define prim_h

#include "gen.h"

typedef datum(*prim_meth)(datum, datum);

void pr(datum d);

#endif /*prim_h*/
