#ifndef obj_h
#define obj_h

#include <stdlib.h>
#include "gen.h"
#include "pair.h"

extern datum int_surrogate, str_surrogate, pair_surrogate;

datum make_compiled_obj(datum env, uint *table);

#define OBJ_TAG 0x2

#define pair2obj(p) ((datum) (((uint) (p)) | OBJ_TAG))
#define obj2pair(o) ((pair) (((uint) (o)) & ~OBJ_TAG))

#define obj_sig_matches(x) ((((uint) (x)) & BOX_MASK) == OBJ_TAG)
#define compiled_objp(x) (in_pair_range(x) && obj_sig_matches(x))

datum compiled_obj_env(datum obj);
uint *compiled_obj_method(datum obj, datum name);
int compiled_obj_has_method(datum obj, datum name);

#endif /*obj_h*/
