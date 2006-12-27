#include <stdlib.h>
#include <stdio.h>
#include <ctype.h>
#include <netinet/in.h>
#include <string.h>
#include "vm.h"
#include "gen.h"
#include "pair.h"
#include "obj.h"
#include "prim.h"
#include "st.h"

#define MAGIC_LEN 8

datum *static_datums = (datum *)0;
size_t static_datums_cap = 0, static_datums_fill = 0, static_datums_base;

uint *label_offsets = (uint *)0, *instrs = (uint *)0;
size_t label_count = 0, instr_count = 0;

uint quit_inst[1] = {0x30000000};

datum regs[REG_COUNT];
pair stack = nil;
datum genv, tasks;

#define MAX_INSTR_SETS 50
uint *instr_bases[MAX_INSTR_SETS], *instr_ends[MAX_INSTR_SETS];
int instr_sets = 0;

datum equals_sym, minus_sym, plus_sym, percent_sym, run_sym, ok_sym,
      set_cdr_sym, car_sym, cdr_sym, emptyp_sym, remove_sym,
      has_methodp_sym, get_sym, put_sym, destroy_sym, read_sym;

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
    "OP_COMPILED_OBJ_ENV",
    "OP_LIST",
    "OP_LOAD_ADDR",
    "OP_BF",
    "OP_BPRIM",
    "OP_LOAD_IMM",
    "OP_CONS",
    "OP_APPLY_PRIMITIVE_PROC",
    "OP_MAKE_COMPILED_OBJ",
    "OP_COMPILED_OBJECT_METHOD",
    "OP_SETBANG",
    "unused opcode",
    "OP_DEFINE",
    "OP_LOOKUP",
    "OP_LEXICAL_LOOKUP",
    "OP_LEXICAL_SETBANG",
    "OP_EXTEND_ENVIRONMENT",
};
#endif

void
usage(void)
{
    fprintf(stderr, "Usage: vm <file.lxc>\n");
    exit(1);
}

void
bail(const char *m)
{
    perror(m);
    exit(1);
}

void
insert_datum(datum d)
{
    if (static_datums_fill >= static_datums_cap) die("too many static datums");
    static_datums[static_datums_fill++] = d;
}

uint
read_int(FILE *f)
{
    uint n, r;
    r = fread(&n, 4, 1, f);
    if (r != 1) {
        printf("could not read int. r = %d\n", r);
        die("could not read int");
    }
    return ntohl(n);
}

datum
load_int(FILE *f)
{
    uint n;
    n = read_int(f);
    return (datum) n; /* n is pseudo-boxed already */
}

datum
load_bigint(FILE *f)
{
    die("this is a long integer... teach me how to handle those");
    return nil;
}

char *
read_string(FILE *f, size_t n)
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

datum
load_string(FILE *f)
{
    size_t n;
    char *s;
    n = read_int(f);
    s = read_string(f, n);
    return make_string_init(s);
}

char
readc(FILE *f)
{
    int c;
    c = getc(f);
    if (c == EOF) bail("cannot read");
    return (char)c;
}

datum
load_symbol(FILE *f)
{
    int l = 1;
    char *s = NULL, c;
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

datum
load_list(FILE *f)
{
    int i = 0;
    pair l = nil;
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

void
load_datums(FILE *f, uint n)
{
    datum d;
    char type;
    for (; n; n--) {
        type = readc(f);
        switch (type) {
            case '#': d = load_int(f); break;
            case '!': d = load_bigint(f); break;
            case '@': d = load_string(f); break;
            case '$': d = load_symbol(f); break;
            case '(': d = load_list(f); break;
            default:
                fprintf(stderr, "unknown datum signifier '%c'\n", type);
                die("unknown datum signifier");
        }
        insert_datum(d);
    }
}

void
load_labels(FILE *f, uint n)
{
    uint i;
    for (i = 0; i < n; i++) {
        label_offsets[i] = read_int(f);
    }
}

void
load_instrs(FILE *f, uint n)
{
    uint i;
    for (i = 0; i < n; i++) {
        instrs[i] = read_int(f);
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
load_file(char *name)
{
    FILE *f;

    f = fopen(name, "rb");
    if (!f) bail("cannot open file");

    check_magic(f);
    static_datums_cap += read_int(f);
    static_datums_base = static_datums_fill;

    static_datums = realloc(static_datums, static_datums_cap * sizeof(datum));
    if (!static_datums) die("cannot allocate static_datums");

    load_datums(f, static_datums_cap - static_datums_base);

    label_count = read_int(f);
    free(label_offsets);
    label_offsets = malloc(label_count * sizeof(uint));
    if (!label_offsets) die("cannot allocate label offsets");

    load_labels(f, label_count);

    instr_count = read_int(f);
    /*free(instrs);*/
    instrs = malloc(instr_count * sizeof(uint));
    if (!instrs) die("cannot allocate instr offsets");
    if (instr_sets >= MAX_INSTR_SETS) die("too many instruction sets");
    instr_bases[instr_sets] = instrs;
    instr_ends[instr_sets] = instrs + instr_count;
    instr_sets++;

    load_instrs(f, instr_count);
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
    pair p;
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

    for (;level--;) env = cdr(env);
    cell = car(env);
    for (;index--;) cell = cdr(cell);
    return car(cell) = val;
}

datum
extend_environment(datum env, datum argl, datum formals)
{
    return cons(argl, env);
}

void
link(void)
{
    register uint *pc;
    uint di, li;

    for (pc = &instrs[0]; pc < &instrs[instr_count]; ++pc) {
        register uint inst = *pc;
        switch (I_OP(inst)) {
            case OP_COMPILED_OBJECT_METHOD:
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
                assert(di < static_datums_cap);
                *pc = (uint) static_datums[di + static_datums_base];
                break;
            case OP_ADDR:
                li = I_L(inst);
                *pc = (uint) &instrs[label_offsets[li]];
                break;
        }
    }
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
        printf("executing %s (0x%x at %p)\n",
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
            case OP_COMPILED_OBJ_ENV:
                ra = I_R(inst);
                rb = I_RR(inst);
                regs[ra] = compiled_obj_env(regs[rb]);
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
                if (!compiled_objp(regs[ra])) pc = tmp - 1;
                break;
            case OP_LOAD_IMM:
                ra = I_R(inst);
                di = I_RD(inst);
                regs[ra] = static_datums[di];
                break;
            case OP_MAKE_ARRAY:
                ra = I_R(inst);
                di = I_RD(inst);
                die("hi\n");
                /*regs[ra] = static_datums[di];*/
                break;
            case OP_CONS:
                ra = I_R(inst);
                rb = I_RR(inst);
                rc = I_RRR(inst);
                regs[ra] = cons(regs[rb], regs[rc]);
                break;
            case OP_APPLY_PRIMITIVE_PROC:
                ra = I_R(inst);
                rb = I_RR(inst);
                rc = I_RRR(inst);
                rd = I_RRRR(inst);
                regs[ra] = apply_primitive_proc(regs[rb], regs[rc], regs[rd]);
                break;
            case OP_MAKE_COMPILED_OBJ:
                ra = I_R(inst);
                rb = I_RR(inst);
                rc = I_RRR(inst);
                regs[ra] = make_compiled_obj(regs[rb], regs[rc]);
                break;
            case OP_COMPILED_OBJECT_METHOD:
                ra = I_R(inst);
                rb = I_RR(inst);
                di = I_RRD(inst);
                d = static_datums[di];
                regs[ra] = (datum) complied_object_method(regs[rb], d);
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
                die("unknown op");
                //die("unknown op %d\n", I_OP(inst));
        }
    }
}

datum
call(datum o, datum m, pair a)
{
    regs[R_PROC] = o;
    regs[R_ARGL] = a;
    regs[R_CONTINUE] = quit_inst;
    start(complied_object_method(o, m));
    return regs[R_VAL];
}

static int
tasks_empty()
{
    return truep(call(tasks, emptyp_sym, nil));
}

static datum
next_task()
{
    return call(tasks, remove_sym, nil);
}

int
main(int argc, char **argv)
{
    if (argc != 2) usage();

    init_mem();
    genv = cons(nil, nil);

    equals_sym = intern("=");
    minus_sym = intern("-");
    plus_sym = intern("+");
    percent_sym = intern("%");
    run_sym = intern("run");
    ok_sym = intern("ok");
    set_cdr_sym = intern("set-cdr!");
    car_sym = intern("car");
    cdr_sym = intern("cdr");
    emptyp_sym = intern("empty?");
    remove_sym = intern("remove!");
    has_methodp_sym = intern("has-method?");
    get_sym = intern("get");
    put_sym = intern("put!");
    destroy_sym = intern("destroy!");
    read_sym = intern("read");

    setup_global_env(genv);
    regs[R_ENV] = genv;

    load_file("prelude.lxc");
    link();
    start(instrs);

    tasks = lookup(genv, intern("*tasks*"));

    load_file(argv[1]);
    link();
    start(instrs);

    while (!tasks_empty()) {
        call(next_task(), run_sym, nil);
    }

    return 0;
}

