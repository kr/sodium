
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include "pair.h"
#include "vm.h"
#include "obj.h"
#include "prim.h"
#include "config.h"

int gc_in_progress = 0;
struct pair *busy_pairs, *old_pairs;
static struct pair *free_pairs;
size_t free_index = 0, scan_index = 0, dead_index = 0;
static datum become_a = nil, become_b = nil;

// int /*bool*/
// pairp(datum x) {
//     return objp(x) &&
//         (((pair)x) >= busy_pairs) &&
//         (((pair)x) < &busy_pairs[HEAP_SIZE]);
// }

void
init_mem(void)
{
    busy_pairs = malloc(sizeof(struct pair) * HEAP_SIZE);
#if GC_DEBUG
    printf("busy_pairs = %p\n", busy_pairs);
#endif
    if (!busy_pairs) die("init_mem -- out of memory");
    free_index = 0;
}

#define CLIP_LEN(l) ((l) & 0x00ffffff)
#define DATUM_INFO(t,l) (((t)<<24)|CLIP_LEN(l))
#define DATUM_TYPE(i) ((i) >> 24)
#define DATUM_TYPE_ARRAY 0x01
#define DATUM_TYPE_BYTES 0x02
#define DATUM_TYPE_OBJ 0x03
#define DATUM_TYPE_UNDEAD 0x04
#define DATUM_TYPE_BROKEN_HEART 0xff
#define DATUM_LEN(i) ((i) & 0x00ffffff)
#define SET_DATUM_TYPE(o,t) {(o)->info = DATUM_INFO(t, DATUM_LEN((o)->info));}

void
dump_obj(datum o)
{
    static int rec = 0;
    int i, len = DATUM_LEN(((pair) o)->info);
    printf("\nbusy_pair base: %p    busy_pair top: %p\n",
           busy_pairs, &busy_pairs[HEAP_SIZE]);
    if (old_pairs) {
        printf("old_pair base: %p    old_pair top: %p\n",
               old_pairs, &old_pairs[HEAP_SIZE]);
    } else {
        printf("no old_pair\n");
    }

    printf("  tag: %d   len: %d\n", DATUM_TYPE(((pair) o)->info), len);
    if (rec) return;
    rec = 1;
    if (len > 10) len = 10;
    for (i = 0; i < len; i++) {
        printf("  # item %d: ", i);
        pr(((pair) o)->datums[i]);
    }
    rec = 0;
}

inline pair
relocate(pair p)
{
    pair np;
    int len;

    if (!in_pair_range(p)) return p;

#if GC_DEBUG_BH
    if (DATUM_TYPE(p->info) == DATUM_TYPE_BROKEN_HEART) {
        printf("found broken heart %p -> %p\n", p, car(p));
    }
#endif

    if (DATUM_TYPE(p->info) == DATUM_TYPE_BROKEN_HEART) return car(p);

#if GC_DEBUG
    printf("relocating pair (type %d) at %p\n", DATUM_TYPE(p->info), p);
#endif

#if GC_DEBUG_STR
    if (DATUM_TYPE(p->info) == DATUM_TYPE_BYTES) {
        printf("copying bytes at %p\n", p);
    }
#endif

    np = &free_pairs[free_index++];
    np->info = p->info;
    if (DATUM_TYPE(p->info) == DATUM_TYPE_BYTES) {
        np->info = DATUM_INFO(DATUM_TYPE_BYTES, DATUM_LEN(p->info));
    }
    for (len = DATUM_LEN(p->info); len--;) {
        np->datums[len] = p->datums[len];
        ++free_index;
    }

#if GC_DEBUG
    printf("...copied to %p\n", np);
#endif

#if GC_DEBUG_STR
    if (DATUM_TYPE(p->info) == DATUM_TYPE_BYTES) {
        printf("copied ``%s'' to ``%s''\n",
                (char *) p->datums, (char *) np->datums);
    }
#endif

    p->info = DATUM_INFO(DATUM_TYPE_BROKEN_HEART, DATUM_LEN(p->info));
    return car(p) = np;
}

static void
gc(int c, ...)
{
    int i, live = 0;
    pair np;
    free_index = 0;
    datum new_become_a, new_become_b, *dp;
    va_list ap;

    if (gc_in_progress) die("ran out of memory during GC");
    gc_in_progress = 1;

#if GC_DEBUG
    printf("BEGIN GC\n");
#endif

    free_pairs = malloc(sizeof(struct pair) * HEAP_SIZE);
    if (!free_pairs) die("gc -- out of memory");

    if (become_a && become_b) {
        new_become_a = relocate(become_a);
        new_become_b = relocate(become_b);
        if (become_a != new_become_a) car(become_a) = new_become_b;
        if (become_b != new_become_b) car(become_b) = new_become_a;
    }

    stack = relocate(stack);
    tasks = relocate(tasks);
    genv = relocate(genv);
    to_import = relocate(to_import);
    to_start = relocate(to_start);
    modules = relocate(modules);
    int_surrogate = relocate(int_surrogate);
    str_surrogate = relocate(str_surrogate);
    pair_surrogate = relocate(pair_surrogate);
    nil_surrogate = relocate(nil_surrogate);
    symbol_surrogate = relocate(symbol_surrogate);
    va_start(ap, c);
    for (; c; --c) {
        dp = va_arg(ap, datum *);
        *dp = relocate(*dp);
    }
    va_end(ap);
    for (i = 0; i < REG_COUNT; ++i) {
        regs[i] = relocate(regs[i]);
    }
    for (i = 0; i < static_datums_fill; ++i) {
        static_datums[i] = relocate(static_datums[i]);
    }

    scan_index = dead_index = 0;

scan_again:
#if GC_DEBUG
    printf("scan_index is %d, free_index is %d\n", scan_index, free_index);
#endif
    while (scan_index < free_index) {
        np = &free_pairs[scan_index++];
        ++live;
        switch (DATUM_TYPE(np->info)) {
            case DATUM_TYPE_ARRAY:
            case DATUM_TYPE_OBJ:
            case DATUM_TYPE_UNDEAD:
                for (i = DATUM_LEN(np->info); i--;) {
                    np->datums[i] = relocate(np->datums[i]);
                    ++scan_index;
                }
                break;
            case DATUM_TYPE_BYTES:
                scan_index += DATUM_LEN(np->info);
                break;
        }
    }

    if (!dead_index) {
#if GC_DEBUG
        printf("resurrecting recently deceased\n");
#endif
        /* resurrect the recently deceased, if they have unfinished business */
        while (dead_index < HEAP_SIZE) {
            np = &busy_pairs[dead_index++];
            switch (DATUM_TYPE(np->info)) {
                case DATUM_TYPE_OBJ:
#if GC_DEBUG
                    printf("scanning dead object ");
                    pr(np);
                    printf("  with env ");
                    pr(car(np));
#endif
                    if (compiled_obj_has_method(np, finalize_sym)) {
                        np = relocate(np);
                        SET_DATUM_TYPE(np, DATUM_TYPE_UNDEAD);
                    }
                    break;
            }
            dead_index += DATUM_LEN(np->info);
        }
#if GC_DEBUG
        printf("scaning again\n");
#endif
        goto scan_again; /* relocate the undead and their dependencies */
    }

    old_pairs = busy_pairs;
    busy_pairs = free_pairs;
#if GC_DEBUG
    printf("busy_pairs = %p\n", busy_pairs);
#endif
    if (free_index >= HEAP_SIZE) die("gc -- no progress");
#if GC_STATS
    printf("gc done (%d live)\n", live);
#endif /*GC_STATS*/

    free(old_pairs);
    old_pairs = 0;

    /* finalize all undead objects */
    /* we can't do this earlier because we call in to LX code and that requres
     * that the free/busy pointers are swapped back */
    dead_index = 0;
    while (dead_index < HEAP_SIZE) {
        np = &busy_pairs[dead_index++];
        switch (DATUM_TYPE(np->info)) {
            case DATUM_TYPE_UNDEAD:
                call(np, finalize_sym, nil);
                break;
        }
        dead_index += DATUM_LEN(np->info);
    }

#if GC_DIE
    die("GC_DIE");
#endif

    gc_in_progress = 0;
    become_a = become_b = nil;
}

static datum
internal_cons(unsigned char type, uint len, datum x, datum y)
{
    pair p;

    if ((free_index + (len + 1)) >= HEAP_SIZE) gc(2, &x, &y);
    if ((free_index + (len + 1)) >= HEAP_SIZE) die("cons -- OOM after gc");
    p = &busy_pairs[free_index++];
    p->info = DATUM_INFO(type, len);
    car(p) = x;
    cdr(p) = y;
    free_index += len;
    return p;
}

datum
cons(datum x, datum y)
{
    return internal_cons(DATUM_TYPE_ARRAY, 2, x, y);
}

datum
make_array(uint len)
{
    pair p;

    if (len < 1) return nil;
    if (len != CLIP_LEN(len)) die("make_array -- too big");
    if ((free_index + (len + 1)) >= HEAP_SIZE) gc(0);
    if ((free_index + (len + 1)) >= HEAP_SIZE) die("make_array -- OOM after gc");
    p = &busy_pairs[free_index++];
    p->info = DATUM_INFO(DATUM_TYPE_ARRAY, len);
    free_index += len;
    for (;len--;) {
        p->datums[len] = nil;
    }
    return p;
}

void
become(datum a, datum b)
{
    become_a = a;
    become_b = b;
    gc(0);
}

datum
make_bytes(uint len)
{
    pair p;
    uint words;

    words = max(len / 4 + ((len % 4) ? 1 : 0), 1);

    if ((free_index + (words + 1)) >= HEAP_SIZE) gc(0);
    if ((free_index + (words + 1)) >= HEAP_SIZE) die("make_bytes -- OOM after gc");

    p = &busy_pairs[free_index++];
    p->info = DATUM_INFO(DATUM_TYPE_BYTES, words);
    free_index += words;
    return p;
}

datum
make_obj_with_extra(datum o, uint len)
{
    int olen;

    if (!obj_tag_matches(o)) return nil;
    olen = DATUM_LEN(((pair)o)->info);
    if (olen != 2) return nil;
    return internal_cons(DATUM_TYPE_OBJ, olen + len, car(o), cdr(o));
}

datum
make_compiled_obj(datum env, uint *table)
{
    return internal_cons(DATUM_TYPE_OBJ, 2, env, (datum) table);
}

datum
make_bytes_init_len(const char *s, int len)
{
    datum d = make_bytes(len + 1);
    strcpy(bytes_contents(d), s);
    bytes_contents(d)[len] = '\0';
    return d;
}

datum
make_bytes_init(const char *s)
{
    return make_bytes_init_len(s, strlen(s));
}

char *
bytes_contents(datum str)
{
    pair p;
    if (!bytesp(str)) die1("bytes_contents -- not an instance of bytes", str);
    p = (pair) str;
    return (char *) p->datums;
}

/* caller must free the bytes returned by this function */
char *
copy_bytes_contents(datum str)
{
    uint n;
    char *s, *x = bytes_contents(str);
    n = strlen(x) + 1;
    s = malloc(sizeof(char) * n);
    memcpy(s, x, n);
    return s;
}

static pair
datum2pair(datum arr)
{
    if (!pairp(arr)) die1("datum2pair -- not an array", arr);
    return (pair) arr;
}

#define acc(x,i) (((pair)(x))->datums[(uint)(i)])

datum
array_get(datum arr, uint index)
{
    pair p = datum2pair(arr);
    if (index >= DATUM_LEN(p->info)) die("array_get -- index out of bounds");
    return acc(arr, index);
}

void
array_put(datum arr, uint index, datum val)
{
    pair p = datum2pair(arr);
    if (index >= DATUM_LEN(p->info)) die("array_put -- index out of bounds");
    acc(arr, index) = val;
}

uint
array_len(datum arr)
{
    pair p = datum2pair(arr);
    return DATUM_LEN(p->info);
}

int
array_tag_matches(datum arr)
{
    pair p = (pair) arr;
    return DATUM_TYPE(p->info) == DATUM_TYPE_ARRAY;
}

int
bytes_tag_matches(datum arr)
{
    pair p = (pair) arr;
    return DATUM_TYPE(p->info) == DATUM_TYPE_BYTES;
}

int
obj_tag_matches(datum o)
{
    pair p = (pair) o;
    return DATUM_TYPE(p->info) == DATUM_TYPE_OBJ;
}

int
undead_tag_matches(datum o)
{
    pair p = (pair) o;
    return DATUM_TYPE(p->info) == DATUM_TYPE_UNDEAD;
}

int
broken_heart_tag_matches(datum bh)
{
    pair p = (pair) bh;
    return DATUM_TYPE(p->info) == DATUM_TYPE_BROKEN_HEART;
}
