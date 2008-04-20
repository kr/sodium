#ifndef vm_h
#define vm_h

#include <stdlib.h>
#include "mem.h"

#define OP_NOP 0x00
#define OP_DATUM 0x01
#define OP_ADDR 0x02
#define OP_GOTO_REG 0x03
#define OP_PUSH 0x04
#define OP_POP 0x05
#define OP_QUIT 0x06
#define OP_GOTO_LABEL 0x07
#define OP_MOV 0x08
#define OP_CLOSURE_ENV 0x09
#define OP_LIST 0x0a
#define OP_LOAD_ADDR 0x0b
#define OP_BF 0x0c
#define OP_BPRIM 0x0d
#define OP_LOAD_IMM 0x0e
#define OP_CONS 0x0f
#define OP_APPLY_PRIM_METH 0x10
#define OP_MAKE_CLOSURE 0x11
#define OP_CLOSURE_METHOD 0x12
#define OP_SETBANG 0x13
#define OP_MAKE_ARRAY 0x14
#define OP_DEFINE 0x15
#define OP_LOOKUP 0x16
#define OP_LEXICAL_LOOKUP 0x17
#define OP_LEXICAL_SETBANG 0x18
#define OP_EXTEND_ENVIRONMENT 0x19

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

#define I_OP(i) (((i) >> 27) & 0x1f)
#define I_D(i) ((i) & 0x7ffffff)
#define I_L(i) ((i) & 0x7ffffff)
#define I_R(i) (((i) >> 22) & 0x1f)
#define I_RD(i) ((i) & 0x3fffff)
#define I_RR(i) (((i) >> 17) & 0x1f)
#define I_RI(i) (((i) >> 12) & 0x3ff)
#define I_RRR(i) (((i) >> 12) & 0x1f)
#define I_RRD(i) ((i) & 0x1ffff)
#define I_RII(i) ((i) & 0xfff)
#define I_RRRR(i) (((i) >> 7) & 0x1f)

void check_env(datum e);

int addrp(datum d);

void dict_insert(datum dict, uint *addr, datum name);

/* signal an error if env is not the global environment */
void setbang(datum env, datum val, datum name);
void define(datum env, datum val, datum name);

datum lookup(datum env, datum name);
datum compile_module(datum name);
void execute_file(const char *name);
datum lexical_lookup(datum env, uint level, uint index);
datum lexical_setbang(datum env, uint level, uint index, datum val);
datum extend_environment(datum env, datum argl, datum formals);
datum call(datum o, datum m, chunk a);
datum report_error(datum args);

extern datum regs[REG_COUNT];
extern chunk stack;
extern datum genv;

extern datum run_sym, ok_sym;

extern datum *static_datums;
extern size_t static_datums_fill;

void bail(const char *m);

#endif /*vm_h*/
