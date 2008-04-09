#ifndef prim_h
#define prim_h

#include <stdarg.h>

#include "gen.h"

typedef datum(*prim_meth)(datum, datum);

void prx(datum d);
void pr(datum d);

void prfmt(int fd, char *fmt, ...);

#endif /*prim_h*/
