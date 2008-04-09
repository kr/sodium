#include <unistd.h>
#include <stdio.h>
#include <errno.h>
#include <stdlib.h>
#include <ctype.h>
#include <netinet/in.h>
#include <string.h>
#include <setjmp.h>
#include "vm.h"
#include "gen.h"
#include "mem.h"
#include "obj.h"
#include "prim.h"
#include "st.h"
#include "lxc.h"
#include "config.h"
#include "module-index.h"

#define MAGIC_LEN 8

datum *static_datums = (datum *)0;
size_t static_datums_cap = 0, static_datums_fill = 0, static_datums_base;

uint quit_inst[1] = {0x30000000};

datum genv, regs[REG_COUNT];
chunk stack = nil;

#define MAX_INSTR_SETS 50
uint *instr_bases[MAX_INSTR_SETS], *instr_ends[MAX_INSTR_SETS];
int instr_sets = 0;

datum run_sym, ok_sym;

#if VM_DEBUG > 0
static char *instr_names[32] = {
    "OP_NOP",
    "OP_DATUM",
    "OP_ADDR",
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
    "OP_MAKE_ARRAY",
    "OP_DEFINE",
    "OP_LOOKUP",
    "OP_LEXICAL_LOOKUP",
    "OP_LEXICAL_SETBANG",
    "OP_EXTEND_ENVIRONMENT",
};
#endif

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
    exit(1);
}

static void
insert_datum(datum d)
{
    if (static_datums_fill >= static_datums_cap) die("too many static datums");
    static_datums[static_datums_fill++] = d;
}

static uint
read_int(FILE *f)
{
    uint n, r;
    r = fread(&n, 4, 1, f);
    if (r != 1) {
        prfmt(1, "could not read int. r = %u\n", r);
        die("could not read int");
    }
    return ntohl(n);
}

static datum
load_int(FILE *f)
{
    uint n;
    n = read_int(f);
    return (datum) n; /* n is pseudo-boxed already */
}

static datum
init_int(uint value)
{
    if ((value & 1) != 1) {
        prfmt(1, "bad static int value 0x%x\n", value);
        die("bad static int value\n");
    }
    return (datum) value;
}

static datum
init_pointer(uint value)
{
    return (datum) value;
}

static datum
load_bigint(FILE *f)
{
    die("this is a long integer... teach me how to handle those");
    return nil;
}

static datum
init_bigint(uint value)
{
    die("this is a long integer... teach me how to handle those");
    return nil;
}

static char *
read_bytes(FILE *f, size_t n)
{
    size_t r;
    char *s;
    s = (char *)malloc((n + 1) * sizeof(char));
    s[n] = '\0';
    for (; n; n -= r) {
        r = fread(s, 1, n, f);
        if (!r) die("could not read");
    }
    return s;
}

static datum
load_str(FILE *f)
{
    size_t size, len;
    char *s;
    size = read_int(f);
    len = read_int(f);
    s = read_bytes(f, size);
    return make_str_init(size, len, s);
}

static datum
init_str(uint value)
{
    size_t size, len;
    char *s = (char *) value;

    size = len = strlen(s);

    return make_str_init(size, len, s);
}

char
readc(FILE *f)
{
    int c;
    c = getc(f);
    if (c == EOF) bail("cannot read");
    return (char)c;
}

static datum
load_symbol(FILE *f)
{
    int l = 1;
    char c, *s = NULL;
    datum sym;
    c = readc(f);
    do {
        s = realloc(s, l++ * sizeof(char));
        s[l - 2] = c;
        c = readc(f);
    } while (c != '\0');
    s = realloc(s, l++ * sizeof(char));
    s[l - 2] = '\0';
    sym = intern(s);
    free(s);
    return sym;
}

static datum
init_symbol(uint value)
{
    const char *s = (const char *) value;
    return intern(s);
}

static datum
load_list(FILE *f)
{
    int i = 0;
    chunk l = nil;
    char c = readc(f);
    while (c != ')') {
        int n = read_int(f);
        datum x = static_datums[n + static_datums_base];
        l = cons(x, l);
        c = readc(f);
        i++;
    }
    return l;
}

static datum
init_list(uint value)
{
    spair p = (spair) value;
    chunk l = nil;
    datum x;

    while (p) {
        x = static_datums[static_datums_base + p->car];
        l = cons(x, l);
        p = (spair) p->cdr;
    }
    return l;
}

static void
load_datums(FILE *f, uint n)
{
    datum d = nil;
    char type;
    for (; n; n--) {
        type = readc(f);
        switch (type) {
            case '#': d = load_int(f); break;
            case '!': d = load_bigint(f); break;
            case '@': d = load_str(f); break;
            case '$': d = load_symbol(f); break;
            case '(': d = load_list(f); break;
            default:
                write(2, "unknown datum signifier '", 25);
                write(2, &type, 1);
                write(2, "'\n", 2);
                die("unknown datum signifier");
        }
        insert_datum(d);
    }
}

static void
init_datums(static_datums_info static_datums, uint n)
{
    uint i;
    datum d = nil;

    for (i = 0; i < n; i++) {
        switch (static_datums->types[i]) {
            case '>': d = init_pointer(static_datums->entries[i]); break;
            case '#': d = init_int(static_datums->entries[i]); break;
            case '!': d = init_bigint(static_datums->entries[i]); break;
            case '@': d = init_str(static_datums->entries[i]); break;
            case '$': d = init_symbol(static_datums->entries[i]); break;
            case '(': d = init_list(static_datums->entries[i]); break;
            default:
                write(2, "unknown datum signifier '", 25);
                write(2, &static_datums->types[i], 1);
                write(2, "'\n", 2);
                die("unknown datum signifier");
        }
        insert_datum(d);
    }
}

static void
load_labels(FILE *f, uint n, uint *lab_offsets)
{
    uint i;
    for (i = 0; i < n; i++) {
        lab_offsets[i] = read_int(f);
    }
}

static void
load_instrs(FILE *f, uint n, uint *insts)
{
    uint i;
    for (i = 0; i < n; i++) {
        insts[i] = read_int(f);
    }
}

void
check_magic(FILE *f)
{
    size_t r;
    char magic[MAGIC_LEN];
    r = fread(magic, 1, MAGIC_LEN, f);
    if (r != MAGIC_LEN) bail("can't read magic");
    if (strncmp(magic, "\x89LX1\x0d\n\x1a\n", MAGIC_LEN) != 0) die("bad magic");
}

void
nalink(uint *insts, uint inst_count, uint *lab_offsets)
{
    register uint *pc;
    uint di, li;

    for (pc = &insts[0]; pc < &insts[inst_count]; ++pc) {
        register uint inst = *pc;
        switch (I_OP(inst)) {
            case OP_CLOSURE_METHOD:
            case OP_SETBANG:
            case OP_DEFINE:
            case OP_LOOKUP:
                di = I_RRD(inst);
                di += static_datums_base;
                if (di > 0x1ffff) die("too many datums");
                *pc = (inst & 0xfffe0000) | di;
                break;
            case OP_LOAD_IMM:
            case OP_MAKE_ARRAY:
                di = I_RD(inst);
                di += static_datums_base;
                if (di > 0x3fffff) die("too many datums");
                *pc = (inst & 0xffc00000) | di;
                break;
            case OP_DATUM:
                di = I_D(inst);
                assert((di + static_datums_base) < static_datums_cap);
                *pc = (uint) static_datums[di + static_datums_base];
                break;
            case OP_ADDR:
                li = I_L(inst);
                *pc = (uint) &insts[lab_offsets[li]];
                break;
        }
    }
}

int
addrp(datum d)
{
    uint *x = (uint *) d;
    int i;
    for (i = 0; i < instr_sets; i++) {
        if (x >= instr_bases[i] && x < instr_ends[i]) return 1;
    }
    return 0;
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
    chunk p;
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

void
start(uint *start_addr)
{
    register uint *pc, *tmp;
    uint ra, rb, rc, rd, di, level, index;
    datum d;

    regs[R_NIL] = nil;
    regs[R_GLOBAL] = genv;
    for (pc = start_addr;; ++pc) {
        register uint inst = *pc;
#if VM_DEBUG > 0
        prfmt(1, "executing %s (0x%x at %p)\n",
                instr_names[I_OP(inst)], I_OP(inst), pc);
#endif
        switch (I_OP(inst)) {
            case OP_NOP: break;
            case OP_QUIT: return;
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
                pc = tmp - 1;
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
                regs[ra] = (uint *) *++pc;
                break;
            case OP_BF:
                ra = I_R(inst);
                tmp = (uint *) *++pc;
                if (!truep(regs[ra])) pc = tmp - 1;
                break;
            case OP_BPRIM:
                ra = I_R(inst);
                tmp = (uint *) *++pc;
                //if (!closurep(regs[ra])) pc = tmp - 1;
                if (!addrp(regs[ra])) pc = tmp - 1;
                break;
            case OP_LOAD_IMM:
                ra = I_R(inst);
                di = I_RD(inst);
                regs[ra] = static_datums[di];
                break;
            case OP_MAKE_ARRAY:
                ra = I_R(inst);
                di = I_RD(inst);
                die("OP_MAKE_ARRAY -- implement me");
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
                regs[ra] = ((prim_meth) regs[rb])(regs[rc], regs[rd]);
                break;
            case OP_MAKE_CLOSURE:
                ra = I_R(inst);
                rb = I_RR(inst);
                rc = I_RRR(inst);
                regs[ra] = make_closure(regs[rb], regs[rc]);
                break;
            case OP_CLOSURE_METHOD:
                ra = I_R(inst);
                rb = I_RR(inst);
                di = I_RRD(inst);
                d = static_datums[di];
                regs[ra] = (datum) closure_method(regs[rb], d);
                break;
            case OP_SETBANG:
                ra = I_R(inst);
                rb = I_RR(inst);
                di = I_RRD(inst);
                d = static_datums[di];
                /*d = (datum) *++pc;*/
                setbang(regs[ra], regs[rb], d);
                break;
            case OP_DEFINE:
                ra = I_R(inst);
                rb = I_RR(inst);
                di = I_RRD(inst);
                d = static_datums[di];
                //d = (datum) *++pc;
                define(regs[ra], regs[rb], d);
                break;
            case OP_LOOKUP:
                ra = I_R(inst);
                rb = I_RR(inst);
                di = I_RRD(inst);
                d = static_datums[di];
                //d = (datum) *++pc;
                regs[ra] = lookup(regs[rb], d);
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
                prfmt(1, "unknown op 0x%x at %p\n", I_OP(inst), pc);
                die("unknown op");
        }
    }
}

static char *
find_module_file(const char *mname)
{
    char *name;
    uint len = strlen(mname);

    name = malloc(sizeof(char) * (len + 9));
    if (!name) die("out of memory allocating file name");

    memcpy(name, "lib/", 4);
    memcpy(name + 4, mname, len);
    if (strcmp(mname + len - 4, ".lxc") == 0) {
        name[4 + len] = 0;
    } else {
        memcpy(name + 4 + len, ".lxc", 5);
    }
    return name;
}

static uint *
load_module_file(const char *name)
{
    uint *insts, *label_offsets;
    uint label_count = 0;
    size_t instr_count = 0;
    FILE *f;

    f = fopen(name, "rb");

    /* compile the library file if necessary */
    if (!f) {
        int namelen = strlen(name);
        char cmd[namelen + 7];

        memcpy(cmd, "./lx1c ", 7);
        memcpy(cmd + 7, name, namelen);
        system(cmd);
        f = fopen(name, "rb");
    }

    if (!f) {
        prfmt(2, "cannot open file %s\n", name);
        bail("cannot open file");
    }

    check_magic(f);

    static_datums_cap += read_int(f);
    static_datums_base = static_datums_fill;

    static_datums = realloc(static_datums, static_datums_cap * sizeof(datum));
    if (!static_datums) die("cannot allocate static_datums");

    load_datums(f, static_datums_cap - static_datums_base);

    label_count = read_int(f);
    label_offsets = malloc(label_count * sizeof(uint));
    if (!label_offsets) die("cannot allocate label offsets");

    load_labels(f, label_count, label_offsets);

    instr_count = read_int(f);
    insts = malloc(instr_count * sizeof(uint));
    if (!insts) die("cannot allocate instr offsets");
    if (instr_sets >= MAX_INSTR_SETS) die("too many instruction sets");
    instr_bases[instr_sets] = insts;
    instr_ends[instr_sets] = insts + instr_count;
    instr_sets++;

    load_instrs(f, instr_count, insts);

    nalink(insts, instr_count, label_offsets);
    free(label_offsets);

    return insts;
}

static uint *
load_lxc_module(lxc_module mod)
{
    static_datums_cap += mod->static_datums_count;
    static_datums_base = static_datums_fill;

    static_datums = realloc(static_datums, static_datums_cap * sizeof(datum));
    if (!static_datums) die("cannot allocate static_datums");
    init_datums(&mod->static_datums, static_datums_cap - static_datums_base);

    if (instr_sets >= MAX_INSTR_SETS) die("too many instruction sets");
    instr_bases[instr_sets] = mod->instrs;
    instr_ends[instr_sets] = mod->instrs + mod->instrs_count;
    instr_sets++;

    nalink(mod->instrs, mod->instrs_count, mod->label_offsets);

    return mod->instrs;
}

static uint *
load_module(const char *mname)
{
    int i;
    uint *insts;
    char *name;

    for (i = 0; i < lxc_modules_count; i++) {
        if (strcmp(lxc_modules[i]->name, mname) == 0) {
            return load_lxc_module(lxc_modules[i]);
        }
    }

    name = find_module_file(mname);
    insts = load_module_file(name);
    free(name);
    return insts;
}

static void
start_body(uint *start_addr)
{
    regs[R_ENV] = genv;
    start(start_addr);
}

void
execute_file(const char *file)
{
    start_body(load_module_file(file));
}

datum
call(datum o, datum m, chunk a)
{
    if (!symbolp(m)) die1("call -- not a symbol", m);
    regs[R_PROC] = o;
    regs[R_ARGL] = a;
    regs[R_VM0] = closure_method(regs[R_PROC], m);
    if (addrp(regs[R_VM0])) {
        stack = cons(regs[R_CONTINUE], stack); // save
        regs[R_CONTINUE] = quit_inst;
        start(regs[R_VM0]);
        regs[R_CONTINUE] = car(stack); stack = cdr(stack); // restore
        return regs[R_VAL];
    } else {
        return ((prim_meth) regs[R_VM0])(regs[R_PROC], regs[R_ARGL]);
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
compile_module(datum name)
{
    start_body(load_module(symbol2charstar(name)));
    return regs[R_VAL]; /* return value from module */
}

int
main(int argc, char **argv)
{
    datum args;

    if (argc != 2) usage();

    init_mem();
    genv = cons(nil, nil);

    run_sym = intern("run");
    ok_sym = intern("ok");

    /* must evaluate this before the call to define */
    args = cons(make_bytes_init(argv[1]), nil);
    define(genv, args, intern("*args*"));

    /* load and execute the standard prelude */
    start_body(load_module("prelude"));

    return 0;
}

