#ifndef vm_h
#define vm_h

#include <stdlib.h>
#include "lxc.h"
#include "mem.h"
#include "nil.h"

#define REG_COUNT 13
/*#define REG_COUNT 32*/
#define R_NIL 0
#define R_GLOBAL 1
#define R_PROC 2
#define R_VAL 3
#define R_ARGL 4
#define R_CONTINUE 5
#define R_ADDR 6
#define R_ENV 7
#define R_TMP 8
#define R_VM0 9
#define R_VM1 10
#define R_GC0 11
#define R_VOID 12

int imep(datum x);

/* signal an error if env is not the global environment */
void setbang(datum env, datum val, datum name);
void define(datum env, datum val, datum name);

datum lookup(datum env, datum name);

void start_body(uint *start_addr);
datum read_module_file(const char *name);
datum lexical_lookup(datum env, uint level, uint index, int tail);
datum lexical_setbang(datum env, uint level, uint index, int tail, datum val);
datum call(datum o, datum m, datum a);
datum report_error(datum args);
datum closure_env(datum d);

extern datum regs[REG_COUNT];
extern datum stack;
extern datum genv;

extern datum run0_sym, ok_sym, reap0_sym, live_sym, dead_sym;

void bail(const char *m);

extern const size_t ime_mtab_body;
#define ime_mtab (&ime_mtab_body)

#endif /*vm_h*/
