#ifndef obj_h
#define obj_h

#include <stdlib.h>
#include "gen.h"
#include "mem.h"

extern datum int_surrogate, bytes_surrogate, pair_surrogate,
             symbol_surrogate;

datum closure_env(datum d);
uint *closure_method(datum d, datum name);
int closure_has_method(datum d, datum name);
int closures_same_type(datum a, datum b);
datum closure_methods(datum d);

#endif /*obj_h*/
