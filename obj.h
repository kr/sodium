#ifndef obj_h
#define obj_h

#include <stdlib.h>
#include "gen.h"
#include "mem.h"

datum closure_env(datum d);
uint *closure_method(datum d, datum name);
int closure_has_method(datum d, datum name);
datum closure_methods(datum d);

#endif /*obj_h*/
