
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include "pair.h"
#include "vm.h"
#include "obj.h"
#include "prim.h"
#include "config.h"

int gc_in_progress = 0, become_keep_b = 0;
struct pair *busy_pairs, *old_pairs, *fz_list = nil;
static struct pair *free_pairs;
size_t free_index = 0, scan_index = 0;
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

#define cgr(x) (((pair) (x))->datums[2])
#define CLIP_LEN(l) ((l) & 0x00ffffff)
#define DATUM_INFO(t,l) (((t)<<24)|CLIP_LEN(l))
#define DATUM_TYPE(i) ((i) >> 24)
#define DATUM_TYPE_ARRAY 0x01
#define DATUM_TYPE_BYTES 0x02
#define DATUM_TYPE_OBJ 0x03
#define DATUM_TYPE_UNDEAD 0x04
#define DATUM_TYPE_FZ 0x05
#define DATUM_TYPE_BROKEN_HEART 0xff
#define DATUM_LEN(i) ((i) & 0x00ffffff)
#define SET_DATUM_TYPE(o,t) {(o)->info = DATUM_INFO(t, DATUM_LEN((o)->info));}
#define IS_BROKEN_HEART(o) (DATUM_TYPE((o)->info) == DATUM_TYPE_BROKEN_HEART)

#define FZ_LEN 3

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
    if (IS_BROKEN_HEART(p)) {
        printf("found broken heart %p -> %p\n", p, car(p));
    }
#endif

    if (IS_BROKEN_HEART(p)) return car(p);

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
    pair np, fzp, *fz_prev;
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
        if (become_b != new_become_b && !become_keep_b) {
            car(become_b) = new_become_a;
        }
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
    fz_list = relocate(fz_list);
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

    scan_index = 0;

#if GC_DEBUG
    printf("scan_index is %d, free_index is %d\n", scan_index, free_index);
#endif
    while (scan_index < free_index) {
        np = &free_pairs[scan_index++];
        ++live;
        switch (DATUM_TYPE(np->info)) {
            case DATUM_TYPE_ARRAY:
            case DATUM_TYPE_UNDEAD:
                for (i = DATUM_LEN(np->info); i--;) {
                    np->datums[i] = relocate(np->datums[i]);
                    ++scan_index;
                }
                break;
            case DATUM_TYPE_OBJ:
            case DATUM_TYPE_FZ:
                np->datums[0] = relocate(np->datums[0]);
                np->datums[1] = relocate(np->datums[1]);
                /* fall through */
            case DATUM_TYPE_BYTES:
                scan_index += DATUM_LEN(np->info);
                break;
        }
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

    /* Now process the finalizer list. For each entry:
     *  - if the object survived, update the finalizer's pointer
     *  - else, call the free function and remove the finalizer
     */
    fz_prev = &fz_list;
    for (fzp = fz_list; fzp != nil; fzp = cdr(fzp)) {
        pair p = (pair) cgr(fzp);
        if (IS_BROKEN_HEART(p)) {
            fzp->datums[2] = car(p);
            fz_prev = (pair *) &cdr(fzp); /* update the prev pointer */
        } else {
            ((na_fn_free) fzp->datums[0])(DATUM_LEN(p->info) > 2 ? cgr(p) : 0);
            *fz_prev = cdr(fzp); /* remove fzp from the list */
        }
    }

    free(old_pairs);
    old_pairs = 0;

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
    if (len > 0) car(p) = x;
    if (len > 1) cdr(p) = y;
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
    p = internal_cons(DATUM_TYPE_ARRAY, len, nil, nil);
    for (;len--;) p->datums[len] = nil;
    return p;
}

void
become(datum *a, datum *b, int keep_b)
{
    become_a = *a;
    become_b = *b;
    become_keep_b = keep_b;
    gc(2, a, b);
}

datum
make_bytes(uint bytes_len)
{
    uint words_len;

    words_len = max((bytes_len + 3) / 4, 1);
    return internal_cons(DATUM_TYPE_BYTES, words_len, nil, nil);
}

datum
grow_obj(datum *op, uint len, na_fn_free fn)
{
    pair fz;
    int olen, ex = fn ? FZ_LEN : 0;
    datum d, o = *op;

    if (!obj_tag_matches(o)) return nil;
    olen = DATUM_LEN(((pair)o)->info) + len;
    d = internal_cons(DATUM_TYPE_OBJ, olen + ex, car(o), cdr(o));
    if (fn) {
        ((pair) d)->info = DATUM_INFO(DATUM_TYPE_OBJ, olen);
        fz = (pair) d + olen;
        fz->info = DATUM_INFO(DATUM_TYPE_FZ, FZ_LEN);
        fz->datums[0] = fn;
        fz->datums[1] = fz_list;
        fz->datums[2] = d;
        fz_list = fz;
    }

    become(op, &d, 1);
    return d;
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
