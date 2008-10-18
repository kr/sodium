
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include "mem.h"
#include "vm.h"
#include "obj.h"
#include "prim.h"
#include "config.h"
#if GC_DEBUG
#include <stdio.h>
#endif

int gc_in_progress = 0, become_keep_b = 0;
struct chunk *busy_chunks, *old_chunks, *fz_list = nil;
static struct chunk *free_chunks;
size_t free_index = 0, scan_index = 0;
static datum become_a = nil, become_b = nil;

#if GC_DEBUG
size_t reloc_ct;
#endif

// int /*bool*/
// arrayp(datum x) {
//     return closurep(x) &&
//         (((chunk)x) >= busy_chunks) &&
//         (((chunk)x) < &busy_chunks[HEAP_SIZE]);
// }

void
init_mem(void)
{
    busy_chunks = malloc(sizeof(struct chunk) * HEAP_SIZE);
#if GC_DEBUG
    printf("busy_chunks = %p\n", busy_chunks);
#endif
    if (!busy_chunks) die("init_mem -- out of memory");
    free_index = 0;
}

#define CLIP_LEN(l) ((l) & 0x0fffffff)
#define DATUM_INFO(t,l) (((l) << 4) | ((t) & 0xf))
#define DATUM_TYPE(i) ((i) & 0xf)
#define DATUM_LEN(i) ((i) >> 4)

#define DATUM_TYPE_PAIR 3
#define DATUM_TYPE_CLOSURE 5
#define DATUM_TYPE_ARRAY 7
#define DATUM_TYPE_BYTES 9
#define DATUM_TYPE_STR 11
#define DATUM_TYPE_FZ 13
#define DATUM_TYPE_BROKEN_HEART 15

#define IS_BROKEN_HEART(o) (DATUM_TYPE((o)->info) == DATUM_TYPE_BROKEN_HEART)

#define FZ_LEN 3

static inline void
relocate(datum *d)
{
    chunk p, np;
    int len;

    p = *d;

    /* don't try to relocate an unboxed int */
    if (((uint)p) & 1) return;

#if GC_DEBUG
    if (!in_chunk_range(p)) printf("ignoring %p\n", p);
#endif

    if (!in_chunk_range(p)) return;

#if GC_DEBUG_BH
    if (IS_BROKEN_HEART(p)) {
        printf("found broken heart %p -> %p\n", p, p->datums[0]);
    }
#endif

    if (IS_BROKEN_HEART(p)) {
        *d = p->datums[0];
        return;
    }

#if GC_DEBUG
    reloc_ct++;
    printf("relocating chunk type %d (size %d) at %p\n",
            DATUM_TYPE(p->info),
            DATUM_LEN(p->info),
            p);
#endif

#if GC_DEBUG_STR
    if (DATUM_TYPE(p->info) == DATUM_TYPE_BYTES) {
        printf("copying bytes at %p\n", p);
    }
#endif

    np = &free_chunks[free_index++];
    np->info = p->info;
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
    *d = p->datums[0] = np;
}

static void
gc(int c, ...)
{
    int i, live = 0;
    chunk np, fzp, *fz_prev;
    free_index = 0;
    datum old_become_a, old_become_b, *dp;
    va_list ap;

    if (gc_in_progress) die("ran out of memory during GC");
    gc_in_progress = 1;

#if GC_DEBUG
    printf("BEGIN GC\n");
#endif

    free_chunks = malloc(sizeof(struct chunk) * HEAP_SIZE);
    if (!free_chunks) die("gc -- out of memory");

#if GC_DEBUG
    printf("relocating roots\n");
    reloc_ct = 0;
#endif

    if (become_a && become_b) {
        old_become_a = become_a;
        old_become_b = become_b;
        relocate(&become_a);
        relocate(&become_b);
        if (old_become_a != become_a) {
            ((chunk) old_become_a)->datums[0] = become_b;
        }
        if (old_become_b != become_b && !become_keep_b) {
            ((chunk) old_become_b)->datums[0] = become_a;
        }
    }

    relocate((datum *) &stack);
    relocate(&genv);
    relocate(&int_surrogate);
    relocate(&str_surrogate);
    relocate(&bytes_surrogate);
    relocate(&pair_surrogate);
    relocate(&array_surrogate);
    relocate(&nil_surrogate);
    relocate(&symbol_surrogate);
    relocate((datum *) &fz_list);
    va_start(ap, c);
    for (; c; --c) {
        dp = va_arg(ap, datum *);
        relocate(dp);
    }
    va_end(ap);
    for (i = 0; i < REG_COUNT; ++i) {
        relocate(&regs[i]);
    }
    for (i = 0; i < static_datums_fill; ++i) {
        relocate(&static_datums[i]);
    }

#if GC_DEBUG
    printf("relocated %d roots\n", reloc_ct);
#endif

    scan_index = 0;

#if GC_DEBUG
    printf("scan_index is %d, free_index is %d\n", scan_index, free_index);
#endif
    while (scan_index < free_index) {
        np = &free_chunks[scan_index++];
        ++live;
        switch (DATUM_TYPE(np->info)) {
            case DATUM_TYPE_ARRAY:
                for (i = DATUM_LEN(np->info); i--;) {
                    relocate(&np->datums[i]);
                    ++scan_index;
                }
                break;
            case DATUM_TYPE_PAIR:
            case DATUM_TYPE_CLOSURE:
            case DATUM_TYPE_FZ:
                relocate(&np->datums[0]);
                relocate(&np->datums[1]);
                /* fall through */
            case DATUM_TYPE_STR:
            case DATUM_TYPE_BYTES:
                scan_index += DATUM_LEN(np->info);
                break;
            default:
#if GC_DEBUG
                printf("woah! got type %d\n", DATUM_TYPE(np->info));
#endif
                assert(0);
        }
    }

    old_chunks = busy_chunks;
    busy_chunks = free_chunks;
#if GC_DEBUG
    printf("busy_chunks = %p\n", busy_chunks);
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
    for (fzp = fz_list; fzp != nil; fzp = fzp->datums[1]) {
        chunk p = (chunk) fzp->datums[2];
        if (IS_BROKEN_HEART(p)) {
            fzp->datums[2] = p->datums[0];
            fz_prev = (chunk *) &fzp->datums[1]; /* update the prev pointer */
        } else {
            ((na_fn_free) fzp->datums[0])(DATUM_LEN(p->info) > 2 ? p->datums[2] : 0);
            *fz_prev = fzp->datums[1]; /* remove fzp from the list */
        }
    }

    free(old_chunks);
    old_chunks = 0;

#if GC_DIE
    die("GC_DIE");
#endif

    gc_in_progress = 0;
    become_a = become_b = nil;
}

static datum
internal_cons(unsigned char type, uint len, datum x, datum y)
{
    chunk p;

    if ((free_index + (len + 1)) >= HEAP_SIZE) gc(2, &x, &y);
    if ((free_index + (len + 1)) >= HEAP_SIZE) die("cons -- OOM after gc");
    p = &busy_chunks[free_index++];
    p->info = DATUM_INFO(type, len);
    if (len > 0) p->datums[0] = x;
    if (len > 1) p->datums[1] = y;
    free_index += len;
    return p;
}

datum
cons(datum x, datum y)
{
    return internal_cons(DATUM_TYPE_PAIR, 2, x, y);
}

datum
make_array(uint len)
{
    chunk p;

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
grow_closure(datum *op, uint len, na_fn_free fn, void *data)
{
    chunk fz;
    closure c = datum2closure(*op);
    int olen, ex = fn ? 1 + FZ_LEN : 0;
    datum d;

    olen = DATUM_LEN(c->info) + len;
    regs[R_GC0] = *op;
    d = internal_cons(DATUM_TYPE_CLOSURE, olen + ex, c->env, c->table);
    *op = regs[R_GC0];
    regs[R_GC0] = nil;
    if (len) ((chunk) d)->datums[2] = data;
    if (fn) {
        ((chunk) d)->info = DATUM_INFO(DATUM_TYPE_CLOSURE, olen);
        fz = (chunk) d + 1 + olen;
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
make_closure(datum env, uint *table)
{
    return internal_cons(DATUM_TYPE_CLOSURE, 2, env, (datum) table);
}

datum
make_bytes(uint bytes_len)
{
    uint words_len;

    words_len = 1 + (bytes_len + 3) / 4;
    return internal_cons(DATUM_TYPE_BYTES, words_len, (datum) bytes_len, nil);
}

datum
make_str(size_t size)
{
    uint words_size;

    words_size = 1 + (size + 3) / 4;
    return internal_cons(DATUM_TYPE_STR, words_size, (datum) size, nil);
}

size_t
bytes_len(datum bytes)
{
    chunk p;
    if (!bytesp(bytes)) die1("bytes_contents -- not an instance of bytes", bytes);
    p = (chunk) bytes;
    return (size_t) p->datums[0];
}

char *
bytes_contents(datum bytes)
{
    chunk p;
    if (!bytesp(bytes)) die1("bytes_contents -- not an instance of bytes", bytes);
    p = (chunk) bytes;
    return (char *) &p->datums[1];
}

static chunk
datum2arraychunk(datum d)
{
    if (!arrayp(d)) die1("datum2arraychunk -- not an array", d);
    return (chunk) d;
}

#define acc(x,i) (((chunk)(x))->datums[(uint)(i)])

datum
array_get(datum arr, uint index)
{
    chunk p = datum2arraychunk(arr);
    if (index >= DATUM_LEN(p->info)) die("array_get -- index out of bounds");
    return acc(arr, index);
}

datum
array_put(datum arr, uint index, datum val)
{
    chunk p = datum2arraychunk(arr);
    if (index >= DATUM_LEN(p->info)) die("array_put -- index out of bounds");
    return acc(arr, index) = val;
}

uint
array_len(datum arr)
{
    chunk p = datum2arraychunk(arr);
    return DATUM_LEN(p->info);
}

int
pair_tag_matches(datum o)
{
    chunk p = (chunk) o;
    return DATUM_TYPE(p->info) == DATUM_TYPE_PAIR;
}

int
closure_tag_matches(datum o)
{
    chunk p = (chunk) o;
    return DATUM_TYPE(p->info) == DATUM_TYPE_CLOSURE;
}

int
array_tag_matches(datum arr)
{
    chunk p = (chunk) arr;
    return DATUM_TYPE(p->info) == DATUM_TYPE_ARRAY;
}

int
bytes_tag_matches(datum arr)
{
    chunk p = (chunk) arr;
    return DATUM_TYPE(p->info) == DATUM_TYPE_BYTES;
}

int
str_tag_matches(datum str)
{
    chunk p = (chunk) str;
    return DATUM_TYPE(p->info) == DATUM_TYPE_STR;
}


int
broken_heart_tag_matches(datum bh)
{
    chunk p = (chunk) bh;
    return DATUM_TYPE(p->info) == DATUM_TYPE_BROKEN_HEART;
}

inline pair
datum2pair(datum d)
{
    if (!in_chunk_range(d)) die1("not a pair", d);
    if (!pair_tag_matches(d)) die1("not a pair", d);
    return (pair) d;
}

inline closure
datum2closure(datum d)
{
    if (!in_chunk_range(d)) die1("not a closure", d);
    if (!closure_tag_matches(d)) die1("not a closure", d);
    return (closure) d;
}
