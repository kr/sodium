#include <unistd.h>
#include <errno.h>
#include <stdlib.h>
#include <ctype.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <string.h>
#include <setjmp.h>
#include "vm.h"
#include "gen.h"
#include "mem.h"
#include "prim.h"
#include "symbol.h"
#include "lxc.h"
#include "pair.h"
#include "array.h"
#include "bytes.h"
#include "module.na.h"
#include "prelude.h"
#include "str.h"
#include "str.na.h"
#include "nil.h"
#include "config.h"
#include "index.h"

#define OP_NOP 0x00
#define OP_LW 0x01
#define OP_SW 0x02
#define OP_GOTO_REG 0x03
#define OP_PUSH 0x04
#define OP_POP 0x05
#define OP_QUIT 0x06
#define OP_GOTO_LABEL 0x07
#define OP_MOV 0x08
#define OP_unused1 0x09
#define OP_ADDI 0x0a
#define OP_LOAD_ADDR 0x0b
#define OP_BF 0x0c
#define OP_BPRIM 0x0d
#define OP_LOAD_IMM 0x0e
#define OP_CONS 0x0f
#define OP_APPLY_PRIM_METH 0x10
#define OP_MAKE_CLOSURE 0x11
#define OP_CLOSURE_METHOD 0x12
#define OP_SETBANG 0x13
#define OP_LOAD_OFF 0x14
#define OP_DEFINE 0x15
#define OP_LOOKUP 0x16
#define OP_LEXICAL_LOOKUP 0x17
#define OP_LEXICAL_SETBANG 0x18
#define OP_EXTEND_ENVIRONMENT 0x19
#define OP_MAKE_SELFOBJ 0x1a
#define OP_CLOSURE_METHOD2 0x1b
#define OP_LEXICAL_LOOKUP_TAIL 0x1c
#define OP_LEXICAL_SETBANG_TAIL 0x1d
#define OP_SI 0x1e
#define OP_NOP2 0x1f

#define I_OP(i) (((i) >> 27) & 0x1f)
#define I_D(i) ((i) & 0x7ffffff)
#define I_L(i) ((i) & 0x7ffffff)
#define I_R(i) (((i) >> 22) & 0x1f)
#define I_RD(i) ((i) & 0x3fffff)
#define I_RR(i) (((i) >> 17) & 0x1f)
#define I_RI(i) (((i) >> 12) & 0x3ff)
#define I_RRR(i) (((i) >> 12) & 0x1f)
#define I_RRD(i) ((i) & 0x1ffff)
#define I_RRI(i) ((i) & 0x1ffff)
#define I_RII(i) ((i) & 0xfff)
#define I_RRRR(i) (((i) >> 7) & 0x1f)

#define MAGIC_LEN 8

uint quit_inst[1] = {0x30000000};

datum genv, regs[REG_COUNT];

datum stack = nil;

datum run0_sym, ok_sym, reap0_sym, live_sym, dead_sym;

const size_t ime_mtab_body = 1;

static char *instr_names[32] = {
    "OP_NOP",
    "OP_LW",
    "OP_SW",
    "OP_GOTO_REG",
    "OP_PUSH",
    "OP_POP",
    "OP_QUIT",
    "OP_GOTO_LABEL",
    "OP_MOV",
    "<unused1>",
    "OP_ADDI",
    "OP_LOAD_ADDR",
    "OP_BF",
    "OP_BPRIM",
    "OP_LOAD_IMM",
    "OP_CONS",
    "OP_APPLY_PRIM_METH",
    "OP_MAKE_CLOSURE",
    "OP_CLOSURE_METHOD",
    "OP_SETBANG",
    "OP_LOAD_OFF",
    "OP_DEFINE",
    "OP_LOOKUP",
    "OP_LEXICAL_LOOKUP",
    "OP_LEXICAL_SETBANG",
    "OP_EXTEND_ENVIRONMENT",
    "OP_MAKE_SELFOBJ",
    "OP_CLOSURE_METHOD2",
    "OP_LEXICAL_LOOKUP_TAIL",
    "OP_LEXICAL_SETBANG_TAIL",
    "OP_SI",
    "OP_NOP2",
};

#define USAGE "Usage: vm <file.lxc>\n"
void
usage(void)
{
    write(2, USAGE, sizeof(USAGE));
    exit(1);
}

void
bail(const char *m)
{
    prfmt(2, "%s: %s\n", m, strerror(errno));
    exit(2);
}

void
bailx(const char *m)
{
    prfmt(2, "%s\n", m);
    exit(3);
}

static uint
read_int(int f)
{
    uint n, r;
    r = read(f, &n, 4);
    if (r != 4) {
        prfmt(1, "could not read int. r = %u\n", r);
        die("could not read int");
    }
    return ntohl(n);
}

char
readc(int f)
{
    int r;
    char c;
    r = read(f, &c, 1);
    if (r < 0) bail("read() failed");
    if (r < 1) bailx("unexpected end of file");
    return c;
}

static void
load_instrs(int f, uint n, uint *insts)
{
    uint i;
    for (i = 0; i < n; i++) {
        insts[i] = read_int(f);
    }
}

void
check_magic(int f)
{
    size_t r;
    char magic[MAGIC_LEN];
    r = read(f, magic, MAGIC_LEN);
    if (r != MAGIC_LEN) bail("can't read magic");
    if (strncmp(magic, "\x89LX1\x0d\n\x1a\n", MAGIC_LEN) != 0) die("bad magic");
}

static void
link_syms(uint *insts, size_t *sym_offsets)
{
    for (; *sym_offsets; sym_offsets++) {
        size_t loc = *sym_offsets;
        loc += datum2int(insts[loc]); /* insts[loc] is pc-relative */
        insts[*sym_offsets] = (size_t) intern_str(insts + loc);
    }
}

int
imep(datum x)
{
    return !(1 & (size_t) x) && x[-1] == (size_t) ime_mtab;
}

static void
setbang(datum val, datum name)
{
    datum vals, names;
    vals = car(genv);
    names = cdr(genv);
    for (; names != nil; vals = cdr(vals), names = cdr(names)) {
        if (car(names) == name) {
            car(vals) = val;
            return;
        }
    }
    die1("set! -- no such variable", name);
}

static void
define(datum val, datum name)
{
    datum p;
    datum vals, names;
    vals = car(genv);
    names = cdr(genv);
    for (; names != nil; vals = cdr(vals), names = cdr(names)) {
        if (car(names) == name) {
            car(vals) = val;
            return;
        }
    }

    /* make sure that these get updated properly during a garbage collection */
    regs[R_VM0] = genv;
    p = cons(val, car(regs[R_VM0]));
    car(regs[R_VM0]) = p;
    p = cons(name, cdr(regs[R_VM0]));
    cdr(regs[R_VM0]) = p;
}

static datum
lookup(datum name)
{
    datum vals, names;
    vals = car(genv);
    names = cdr(genv);
    for (; names != nil; vals = cdr(vals), names = cdr(names)) {
        if (car(names) == name) return car(vals);
    }
    return die1("lookup -- no such variable", name);
}

datum
lexical_lookup(datum env, uint level, uint index, int tail)
{
    datum cell;

    for (;level--;) env = cdr(env);
    cell = car(env);
    for (;index--;) cell = cdr(cell);
    if (tail) return cell;
    return car(cell);
}

datum
lexical_setbang(datum env, uint level, uint index, int tail, datum val)
{
    /*prfmt(1fdfc9d96950441dabc8e29ab380d2fc78a8b4798, "\n\n\nlexical_setbang(%p, %d, %d, %p)\n", env, level, index, val);*/

    for (;level--;) /*pr(env),*/ env = cdr(env);
    /*pr(env);*/
    /*pr(cell);*/
    if (tail) {
        datum *mcel = &car(env);
        for (;index--;) mcel = &cdr(*mcel);
        *mcel = val;
    } else {
        datum cell = car(env);
        for (;index--;) cell = cdr(cell);
        car(cell) = val;
    }
    return ok_sym;
}

static datum
extend_environment(datum env, datum argl, datum formals)
{
    return cons(argl, env);
}

static datum
make_closure(size_t size, size_t *mtab, datum env)
{
    return make_record(size, mtab, env, nil);
}

static uint *
closure_method(datum d, datum name)
{
    int i, n;
    method_table table;

    table = (method_table) datum_mtab(d);

    n = datum2int(table->size);
    for (i = 0; i < n; ++i) {
        if (table->items[i].name == name) {
            datum pos = (datum) &table->items[i].addr;
            return pos + datum2int(*pos);
        }
    }
    return die1("closure_method -- no such method", name);
}

static datum
closure_method2(datum d, datum name1, datum name2)
{
    int i, n;
    method_table table;

    table = (method_table) datum_mtab(d);

    n = datum2int(table->size);
    for (i = 0; i < n; ++i) {
        if (table->items[i].name == name1) {
            datum pos = (datum) &table->items[i].addr;
            return pos + datum2int(*pos);
        }
    }
    for (i = 0; i < n; ++i) {
        if (table->items[i].name == name2) {
            datum pos = (datum) &table->items[i].addr;
            return pos + datum2int(*pos);
        }
    }
    prfmt(2, "closure_method2 -- no such method: %o or %o\n", name1, name2);
    return die1("closure_method2 -- no such method", nil);
}

#define sign_ext_imm(x) (((ssize_t) (I_RRI(x) << 15)) >> 15)

void
start_body(uint *start_addr)
{
    register uint *pc, *tmp;
    uint ra, rb, rc, rd, di, level;
    int index;
    ssize_t imm;

    /* save continue register */
    stack = cons(regs[R_CONTINUE], stack);
    regs[R_CONTINUE] = quit_inst;

    regs[R_NIL] = nil;
    regs[R_GLOBAL] = genv;
    for (pc = start_addr;; ++pc) {
        register uint inst = *pc;
#if VM_DEBUG > 0
        prfmt(1, "pco=%d executing %s (0x%x at %p)\n",
                pc - start_addr, instr_names[I_OP(inst)], I_OP(inst), pc);
#endif
        switch (I_OP(inst)) {
            case OP_NOP: break;
            case OP_NOP2: break;
            case OP_QUIT: goto halt;
            case OP_LW:
                ra = I_R(inst);
                rb = I_RR(inst);
                imm = sign_ext_imm(inst);
                regs[ra] = (datum) regs[rb][imm];
                break;
            case OP_SW:
                ra = I_R(inst);
                rb = I_RR(inst);
                imm = sign_ext_imm(inst);
                if (regs[rb] + imm > busy_top) fault();
                if (regs[rb] + imm > busy_top) die("SW -- OOM after gc");
                regs[rb][imm] = (size_t) regs[ra];
                break;
            case OP_SI:
                rb = I_RR(inst);
                imm = sign_ext_imm(inst);
                tmp = (datum) *++pc;
                regs[rb][imm] = (size_t) tmp;
                break;
            case OP_GOTO_REG:
                ra = I_R(inst);
                pc = ((uint *) regs[ra]) - 1;
                break;
            case OP_PUSH:
                ra = I_R(inst);
                stack = cons(regs[ra], stack);
                break;
            case OP_POP:
                if (stack == nil) die("cannot pop; stack is empty");
                ra = I_R(inst);
                regs[ra] = car(stack);
                stack = cdr(stack);
                break;
            case OP_GOTO_LABEL:
                tmp = (uint *) *++pc;
                pc += datum2int(tmp) - 1;
                break;
            case OP_MOV:
                ra = I_R(inst);
                rb = I_RR(inst);
                regs[ra] = regs[rb];
                break;
            case OP_ADDI:
                ra = I_R(inst);
                imm = sign_ext_imm(inst);
                ((ssize_t *) regs)[ra] += imm;
            case OP_LOAD_ADDR:
                ra = I_R(inst);
                tmp = (uint *) *++pc;
                regs[ra] = pc + datum2int(tmp);
                break;
            case OP_BF:
                ra = I_R(inst);
                tmp = (uint *) *++pc;
                if (!truep(regs[ra])) pc += datum2int(tmp) - 1;
                break;
            case OP_BPRIM:
                ra = I_R(inst);
                tmp = (uint *) *++pc;
                if (imep(regs[ra])) pc += datum2int(tmp) - 1;
                break;
            case OP_LOAD_IMM:
                ra = I_R(inst);
                regs[ra] = (datum) *++pc;
                break;
            case OP_LOAD_OFF:
                ra = I_R(inst);
                di = I_RD(inst);
                regs[ra] = pc + di;
                break;
            case OP_CONS:
                ra = I_R(inst);
                rb = I_RR(inst);
                rc = I_RRR(inst);
                regs[ra] = cons(regs[rb], regs[rc]);
                break;
            case OP_APPLY_PRIM_METH:
                ra = I_R(inst);
                rb = I_RR(inst);
                rc = I_RRR(inst);
                rd = I_RRRR(inst);
                /*regs[ra] = apply_prim_meth((prim_meth) regs[rb], regs[rc], regs[rd]);*/
                regs[ra] = ((prim_meth) *regs[rb])(regs[rc], regs[rd]);
                break;
            case OP_MAKE_CLOSURE:
                ra = I_R(inst);
                rb = I_RR(inst);
                rc = I_RRR(inst);
                regs[ra] = make_closure(1, regs[rc], regs[rb]);
                break;
            case OP_MAKE_SELFOBJ:
                ra = I_R(inst);
                rb = I_RR(inst);
                regs[ra] = make_closure(0, regs[rb], nil);
                break;
            case OP_CLOSURE_METHOD:
                ra = I_R(inst);
                rb = I_RR(inst);
                regs[ra] = (datum) closure_method(regs[rb], (datum) *++pc);
                break;
            case OP_CLOSURE_METHOD2:
                ra = I_R(inst);
                rb = I_RR(inst);
                {
                  datum name1 = (datum) *++pc;
                  datum name2 = (datum) *++pc;
                  regs[ra] = closure_method2(regs[rb], name1, name2);
                }
                break;
            case OP_SETBANG:
                ra = I_R(inst);
                rb = I_RR(inst);
                setbang(regs[rb], (datum) *++pc);
                break;
            case OP_DEFINE:
                ra = I_R(inst);
                rb = I_RR(inst);
                define(regs[rb], (datum) *++pc);
                break;
            case OP_LOOKUP:
                ra = I_R(inst);
                rb = I_RR(inst);
                regs[ra] = lookup((datum) *++pc);
                break;
            case OP_LEXICAL_LOOKUP:
                ra = I_R(inst);
                level = I_RI(inst);
                index = I_RII(inst);
                regs[ra] = lexical_lookup(regs[R_ENV], level, index, 0);
                break;
            case OP_LEXICAL_SETBANG:
                ra = I_R(inst);
                level = I_RI(inst);
                index = I_RII(inst);
                lexical_setbang(regs[R_ENV], level, index, 0, regs[ra]);
                break;
            case OP_LEXICAL_LOOKUP_TAIL:
                ra = I_R(inst);
                level = I_RI(inst);
                index = I_RII(inst);
                regs[ra] = lexical_lookup(regs[R_ENV], level, index, 1);
                break;
            case OP_LEXICAL_SETBANG_TAIL:
                ra = I_R(inst);
                level = I_RI(inst);
                index = I_RII(inst);
                lexical_setbang(regs[R_ENV], level, index, 1, regs[ra]);
                break;
            case OP_EXTEND_ENVIRONMENT:
                ra = I_R(inst);
                rb = I_RR(inst);
                rc = I_RRR(inst);
                rd = I_RRRR(inst);
                regs[ra] = extend_environment(regs[rb], regs[rc], regs[rd]);
                break;
            default:
                prfmt(2, "unknown op 0x%x %s at %p\n",
                        I_OP(inst), instr_names[I_OP(inst)], pc);
                die("unknown op");
        }
    }
halt:
    /* restore continue register */
    regs[R_CONTINUE] = car(stack);
    stack = cdr(stack);
}

datum
read_module_file(const char *name)
{
    uint *insts;
    size_t instr_count = 0, str_offset_count, sym_offset_count;
    int f;

    f = open(name, O_RDONLY);

    /* compile the library file if necessary */
    if (-1 == f) {
        int namelen = strlen(name) - 1;
        char cmd[namelen + 8];

        memcpy(cmd, "./lx1c ", 7);
        memcpy(cmd + 7, name, namelen);
        cmd[namelen + 7] = 0;
        system(cmd);
        f = open(name, O_RDONLY);
    }

    if (-1 == f) {
        prfmt(2, "cannot open file %s\n", name);
        bail("cannot open file");
    }

    check_magic(f);

    instr_count = read_int(f);
    insts = malloc((instr_count + 2) * sizeof(uint));
    if (!insts) die("cannot allocate instr offsets");
    insts += 2; /* skip over the descriptor and mtab */
    insts[-2] = ((instr_count << 5) | 0xf);
    insts[-1] = (size_t) module_mtab;

    load_instrs(f, instr_count, insts);

    str_offset_count = read_int(f);
    {
        size_t i, str_offsets[str_offset_count + 1];
        for (i = 0; i < str_offset_count; i++) {
            str_offsets[i] = read_int(f);
        }
        str_offsets[str_offset_count] = 0;
        size_t *so = str_offsets;

        for (; *so; so++) {
            insts[*so - 1] = (size_t) str_mtab;
        }
    }

    sym_offset_count = read_int(f);
    {
        size_t i, sym_offsets[sym_offset_count + 1];
        for (i = 0; i < sym_offset_count; i++) {
            sym_offsets[i] = read_int(f);
        }
        sym_offsets[sym_offset_count] = 0;


        link_syms(insts, sym_offsets);
    }


    return insts;
}

datum
call(datum o, datum m, datum a)
{
    if (!symbolp(m)) die1("call -- not a symbol", m);

    regs[R_PROC] = o;
    regs[R_ARGL] = a;
    regs[R_ADDR] = closure_method(regs[R_PROC], m);
    if (!imep(regs[R_ADDR])) {
        start_body(regs[R_ADDR]);
        return regs[R_VAL];
    } else {
        return ((prim_meth) *regs[R_ADDR])(regs[R_PROC], regs[R_ARGL]);
    }
}

datum
report_error(datum args)
{
    write(1, "error ", 6);
    pr(args);
    return die1("error", args);
}

void
link_builtins(lxc_module *modp)
{
    lxc_module mod;
    while ((mod = *modp++)) link_syms(mod->instrs, mod->sym_offsets);
}

int
main(int argc, char **argv)
{
    datum args;

    if (argc != 2) usage();

    init_mem();

    link_builtins(lxc_modules);

    genv = cons(nil, nil);

    run0_sym = intern("run:0");
    ok_sym = intern("ok");
    reap0_sym = intern("reap:0");
    live_sym = intern("live");
    dead_sym = intern("dead");

    /* must evaluate this before the call to define */
    args = cons(make_bytes_init(argv[1]), nil);
    define(args, intern("*args*"));

    /* load and execute the standard prelude */
    start_body(lxc_module_prelude.instrs);

    return 0;
}

