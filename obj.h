#ifndef obj_h
#define obj_h

#include <stdlib.h>
#include "gen.h"
#include "pair.h"

extern datum int_surrogate, str_surrogate, pair_surrogate, nil_surrogate,
             symbol_surrogate;

datum compiled_obj_env(datum obj);
uint *compiled_obj_method(datum obj, datum name);
int compiled_obj_has_method(datum obj, datum name);

#endif /*obj_h*/
