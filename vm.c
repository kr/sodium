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
#include "module.h"
#include "prelude.h"
#include "str.h"
#include "nil.h"
#include "int.h"
#include "config.h"
#include "module-index.h"

#define MAGIC_LEN 8

uint quit_inst[1] = {0x30000000};

datum genv, regs[REG_COUNT];

datum stack = nil;

datum run_sym, ok_sym;

static const size_t ime_mtab_body = 1;
static const size_t *ime_mtab = &ime_mtab_body;

static char *instr_names[32] = {
    "OP_NOP",
    "<unused1>",
    "<unused2>",
    "OP_GOTO_REG",
    "OP_PUSH",
    "OP_POP",
    "OP_QUIT",
    "OP_GOTO_LABEL",
    "OP_MOV",
    "OP_CLOSURE_ENV",
    "OP_LIST",
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
    "<unused3>",
    "<unused4>",
    "<unused5>",
    "<unused6>",
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

void
nalink(uint *insts, uint inst_count,
        size_t *str_offsets, size_t *ime_offsets, size_t *sym_offsets)
{
    insts[-1] = (size_t) module_mtab;

    for (; *str_offsets; str_offsets++) {
        insts[*str_offsets - 1] = (size_t) str_mtab;
    }

    for (; *ime_offsets; ime_offsets++) {
        insts[*ime_offsets - 1] = (size_t) ime_mtab;
    }

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

void
setbang(datum env, datum val, datum name)
{
    datum vals, names;
    if (env != genv) die("set! -- env ought to be the global env\n");
    vals = car(env);
    names = cdr(env);
    for (; names != nil; vals = cdr(vals), names = cdr(names)) {
        if (car(names) == name) {
            car(vals) = val;
            return;
        }
    }
    die1("set! -- no such variable", name);
}

void
define(datum env, datum val, datum name)
{
    datum p;
    datum vals, names;
    if (env != genv) die("define -- env ought to be the global env\n");
    vals = car(env);
    names = cdr(env);
    for (; names != nil; vals = cdr(vals), names = cdr(names)) {
        if (car(names) == name) {
            car(vals) = val;
            return;
        }
    }

    /* make sure that these get updated properly during a garbage collection */
    regs[R_VM0] = env;
    regs[R_VM1] = cdr(env);
    p = cons(val, car(env));
    car(regs[R_VM0]) = p;
    p = cons(name, regs[R_VM1]);
    cdr(regs[R_VM0]) = p;
}

datum
lookup(datum env, datum name)
{
    datum vals, names;
    if (env != genv) die("lookup -- env ought to be the global env\n");
    vals = car(env);
    names = cdr(env);
    for (; names != nil; vals = cdr(vals), names = cdr(names)) {
        if (car(names) == name) return car(vals);
    }
    return die1("lookup -- no such variable", name);
}

datum
lexical_lookup(datum env, uint level, uint index)
{
    datum cell;

    for (;level--;) env = cdr(env);
    cell = car(env);
    for (;index--;) cell = cdr(cell);
    return car(cell);
}

datum
lexical_setbang(datum env, uint level, uint index, datum val)
{
    datum cell;

    /*prfmt(1, "\n\n\nlexical_setbang(%p, %d, %d, %p)\n", env, level, index, val);*/

    for (;level--;) /*pr(env),*/ env = cdr(env);
    /*pr(env);*/
    cell = car(env);
    /*pr(cell);*/
    for (;index--;) cell = cdr(cell);
    car(cell) = val;
    return ok_sym;
}

datum
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

datum
closure_env(datum d)
{
    return (datum) *d;
}

static void
start(uint *start_addr)
{
    register uint *pc, *tmp;
    uint ra, rb, rc, rd, di, level, index;

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
            case OP_CLOSURE_ENV:
                ra = I_R(inst);
                rb = I_RR(inst);
                regs[ra] = closure_env(regs[rb]);
                break;
            case OP_LIST:
                ra = I_R(inst);
                rb = I_RR(inst);
                regs[ra] = cons(regs[rb], nil);
                break;
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
            case OP_SETBANG:
                ra = I_R(inst);
                rb = I_RR(inst);
                setbang(regs[ra], regs[rb], (datum) *++pc);
                break;
            case OP_DEFINE:
                ra = I_R(inst);
                rb = I_RR(inst);
                define(regs[ra], regs[rb], (datum) *++pc);
                break;
            case OP_LOOKUP:
                ra = I_R(inst);
                rb = I_RR(inst);
                regs[ra] = lookup(regs[rb], (datum) *++pc);
                break;
            case OP_LEXICAL_LOOKUP:
                ra = I_R(inst);
                level = I_RI(inst);
                index = I_RII(inst);
                regs[ra] = lexical_lookup(regs[R_ENV], level, index);
                break;
            case OP_LEXICAL_SETBANG:
                ra = I_R(inst);
                level = I_RI(inst);
                index = I_RII(inst);
                lexical_setbang(regs[R_ENV], level, index, regs[ra]);
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
    size_t instr_count = 0, str_offset_count, ime_offsets = 0, sym_offset_count;
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

    load_instrs(f, instr_count, insts);

    str_offset_count = read_int(f);
    {
        size_t i, str_offsets[str_offset_count + 1];
        for (i = 0; i < str_offset_count; i++) {
            str_offsets[i] = read_int(f);
        }
        str_offsets[str_offset_count] = 0;

        sym_offset_count = read_int(f);
        {
            size_t sym_offsets[sym_offset_count + 1];
            for (i = 0; i < sym_offset_count; i++) {
                sym_offsets[i] = read_int(f);
            }
            sym_offsets[sym_offset_count] = 0;

            nalink(insts, instr_count, str_offsets,
                    &ime_offsets, sym_offsets);
        }

    }

    return insts;
}

void
start_body(uint *start_addr)
{
    regs[R_ENV] = genv;
    start(start_addr);
}

datum
call(datum o, datum m, datum a)
{
    if (!symbolp(m)) die1("call -- not a symbol", m);
    regs[R_PROC] = o;
    regs[R_ARGL] = a;
    regs[R_VM0] = closure_method(regs[R_PROC], m);
    if (!imep(regs[R_VM0])) {
        start(regs[R_VM0]);
        return regs[R_VAL];
    } else {
        return ((prim_meth) *regs[R_VM0])(regs[R_PROC], regs[R_ARGL]);
    }
}

datum
report_error(datum args)
{
    write(1, "error ", 6);
    pr(args);
    return die1("error", args);
}

datum
find_builtin_module(datum name)
{
    int i;

    if (symbolp(name)) name = (datum) name[1]; /* symbol->str */

    for (i = 0; i < lxc_modules_count; i++) {
        const char *mname = lxc_modules[i]->name;
        if (str_cmp_charstar(name, strlen(mname), mname) == 0) {
            return lxc_modules[i]->instrs;
        }
    }

    return nil;
}

int
main(int argc, char **argv)
{
    size_t i;
    datum args;

    if (argc != 2) usage();

    init_mem();
    nil_init();
    str_init();
    bytes_init();

    for (i = 0; i < lxc_modules_count; i++) {
        lxc_module mod = lxc_modules[i];
        nalink(mod->instrs, mod->instrs_count,
                mod->str_offsets, mod->ime_offsets, mod->sym_offsets);
    }

    genv = cons(nil, nil);

    run_sym = intern("run");
    ok_sym = intern("ok");

    /* must evaluate this before the call to define */
    args = cons(make_bytes_init(argv[1]), nil);
    define(genv, args, intern("*args*"));

    /* load and execute the standard prelude */
    start_body(lxc_module_prelude.instrs);

    return 0;
}

