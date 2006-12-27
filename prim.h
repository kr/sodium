#ifndef prim_h
#define prim_h

#include "gen.h"

void setup_global_env(datum env);
datum apply_primitive_proc(datum proc, datum msg, datum argl);
int prim_funcp(datum x);

typedef datum(*prim)(datum, datum, datum);

void pr(datum d);

#define MAX_PRIMS 10

/*extern prim prims[MAX_PRIMS];
extern char *prim_names[MAX_PRIMS];*/

datum prim_int(datum rcv, datum msg, datum args);
datum prim_pair(datum rcv, datum msg, datum args);
datum prim_nil(datum rcv, datum msg, datum args);
datum prim_str(datum rcv, datum msg, datum args);
datum prim_sym(datum rcv, datum msg, datum args);
datum prim_file(datum rcv, datum msg, datum args);

datum prim_isp(datum rcv, datum msg, datum args);
datum prim_cons(datum rcv, datum msg, datum args);
datum prim_make_array(datum rcv, datum msg, datum args);
datum prim_list(datum rcv, datum msg, datum args);
datum prim_rep(datum rcv, datum msg, datum args);
datum prim_pr(datum rcv, datum msg, datum args);
datum prim_error(datum rcv, datum msg, datum args);
datum prim_call(datum rcv, datum msg, datum args);
datum prim_open(datum rcv, datum msg, datum args);
datum prim_inspector(datum rcv, datum msg, datum args);

#endif /*prim_h*/
