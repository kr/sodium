
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include "mem.h"
#include "vm.h"
#include "obj.h"
#include "prim.h"
#include "pair.h"
#include "array.h"
#include "bytes.h"
#include "str.h"
#include "config.h"
#if GC_DEBUG
#include <stdio.h>
#endif

/*bool*/
#define in_busy_chunk_range(x) (((x) >= busy_chunks) && \
                               ((x) < &busy_chunks[HEAP_SIZE]))
#define in_old_chunk_range(x) (old_chunks && ((x) >= old_chunks) && \
                              ((x) < &old_chunks[HEAP_SIZE]))
#define in_chunk_range(x) (in_busy_chunk_range(x) || in_old_chunk_range(x))

int gc_in_progress = 0, become_keep_b = 0;
datum busy_chunks, old_chunks, fz_list = nil;
static datum free_chunks;
static size_t free_index = 0, scan_index = 0;
static datum become_a = nil, become_b = nil;

void
init_mem(void)
{
    busy_chunks = malloc(sizeof(datum) * HEAP_SIZE);
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

#define IS_BROKEN_HEART(o) (((o) & 0xf) == DATUM_TYPE_BROKEN_HEART)

#define FZ_LEN 3

#if GC_DEBUG
static const char *datum_types[] = {
    "<unused>",
    "DATUM_TYPE_PAIR",
    "DATUM_TYPE_CLOSURE",
    "DATUM_TYPE_ARRAY",
    "DATUM_TYPE_BYTES",
    "DATUM_TYPE_STR",
    "DATUM_TYPE_FZ",
    "DATUM_TYPE_BROKEN_HEART",
};

static void
prdesc(const char *msg, size_t desc)
{
    if (desc &1) {
        int type = DATUM_TYPE(desc);
        fprintf(stderr, "%s %s (%d) len %u\n",
                msg, datum_types[type >> 1], type, DATUM_LEN(desc));
    } else {
        fprintf(stderr, "%s <not a chunk>\n", msg);
    }
}
#endif

static inline void
relocate(datum refloc)
{
    datum p, *i, *j;
    size_t len;

    p =  *((datum *) refloc);

    /* don't try to relocate an unboxed int */
    if (((size_t) p) & 1) return;

    if (!in_chunk_range(p)) return;

    --p;

    for (;;) {
        len = DATUM_LEN(*p);
#if GC_DEBUG
        prdesc("Relocating", *p);
        fprintf(stderr, "at %p\n", p);
#endif

        switch (DATUM_TYPE(*p)) {
            /*
            case DATUM_TYPE_BACKPTR:
                p -= len;
                continue;
            */

            case DATUM_TYPE_STR:
            case DATUM_TYPE_BYTES:
                len = (len + 3) / 4;

                /* fall through */
            case DATUM_TYPE_PAIR:
            case DATUM_TYPE_CLOSURE:
            case DATUM_TYPE_ARRAY:
            case DATUM_TYPE_FZ:
                i = (datum *) p;
                j = (datum *) free_chunks + free_index;

                /*
                while (j + len >= &free_chunks[free_index]) {
                    to_lim = gmore();
                }
                */

                *j++ = *i++; /* copy the descriptor */
                *j++ = *i++; /* copy the method table pointer */
                while (len--) *j++ = *i++; /* copy the body */

                ((datum *) p)[1] = 2 + (datum) &free_chunks[free_index];
                free_index = ((datum) j) - free_chunks;
#if GC_DEBUG
                fprintf(stderr, "Relocated\n");
#endif

                *p = DATUM_TYPE_BROKEN_HEART;

                /* fall through */
            case DATUM_TYPE_BROKEN_HEART:
                *refloc += p[1] - ((size_t) (p + 2));
                return;

            /*
            case DATUM_TYPE_EMBEDDED:
            */
            default:
                p--;
                continue;
        }
    }
}

static void
gc(int c, ...)
{
    int i, live = 0;
    datum np, fzp, *fz_prev;
    datum old_become_a, old_become_b, dp;
    va_list ap;

    if (gc_in_progress) die("ran out of memory during GC");
    gc_in_progress = 1;

#if GC_DEBUG
    fprintf(stderr, "Start GC\n");
#endif

    free_chunks = malloc(sizeof(datum) * HEAP_SIZE);
    if (!free_chunks) die("gc -- out of memory");
    free_index = 0;

#if GC_DEBUG
    fprintf(stderr, "free_chunks is %p\n", free_chunks);
#endif

    if (become_a && become_b) {
        old_become_a = become_a;
        old_become_b = become_b;
        relocate((datum) &become_a);
        relocate((datum) &become_b);
        if (old_become_a != become_a) {
            ((datum *) old_become_a)[-1] = become_b;
        }
        if (old_become_b != become_b && !become_keep_b) {
            ((datum *) old_become_b)[-1] = become_a;
        }
    }

    relocate((datum) &stack);
    relocate((datum) &genv);
    relocate((datum) &int_surrogate);
    relocate((datum) &bytes_surrogate);
    relocate((datum) &pair_surrogate);
    relocate((datum) &array_surrogate);
    relocate((datum) &nil_surrogate);
    relocate((datum) &symbol_surrogate);
    relocate((datum) &fz_list);
    va_start(ap, c);
    for (; c; --c) {
        dp = va_arg(ap, datum);
        relocate(dp);
    }
    va_end(ap);
    for (i = 0; i < REG_COUNT; ++i) {
        relocate((datum) &regs[i]);
    }
    for (i = 0; i < static_datums_fill; ++i) {
        relocate((datum) &static_datums[i]);
    }

    scan_index = 0;

    np = free_chunks;
    while (np < &free_chunks[free_index]) {
        ++live;
        datum p = np + 2;
        size_t descr = *np, len = DATUM_LEN(descr);
        relocate(np + 1); /* relocate the method table pointer */
        switch (DATUM_TYPE(descr)) {
            case DATUM_TYPE_STR:
            case DATUM_TYPE_BYTES:
                np += (len + 3) / 4 + 2;
                break;
            case DATUM_TYPE_PAIR:
            case DATUM_TYPE_CLOSURE:
            case DATUM_TYPE_FZ:
            case DATUM_TYPE_ARRAY:
            default:
#if GC_DEBUG
                prdesc("scanning", descr);
#endif
                np += len + 2;
                assert(p < np);
                while (p < np) relocate(p++);
        }
    }

    old_chunks = busy_chunks;
    busy_chunks = free_chunks;
    if (free_index >= HEAP_SIZE) die("gc -- no progress");

    /* Now process the finalizer list. For each entry:
     *  - if the object survived, update the finalizer's pointer
     *  - else, call the free function and remove the finalizer
     */
    fz_prev = &fz_list;
    for (fzp = fz_list; fzp != nil; fzp = ((datum *)fzp)[1]) {
        datum p = ((datum *) fzp)[2];
        if (IS_BROKEN_HEART(*(p - 2))) {
            ((datum *) fzp)[2] = (datum) *p;
            fz_prev = (datum *) (fzp + 2); /* update the prev pointer */
        } else {
            ((na_fn_free) fzp[0])((datum) (DATUM_LEN(*(p - 2)) > 2 ? p[2] : 0));
            *fz_prev = (datum) fzp[1]; /* remove fzp from the list */
        }
    }

    free(old_chunks);
    old_chunks = 0;

#if GC_DEBUG
    fprintf(stderr, "Finish GC\n");
#endif

    gc_in_progress = 0;
    become_a = become_b = nil;
}

static datum
dalloc(unsigned char type, uint len, datum mtab, datum x, datum y)
{
    datum p;
    size_t wlen = len;

    if (type == DATUM_TYPE_STR || type == DATUM_TYPE_BYTES) {
        wlen = len + 3 / 4;
    }

    if ((free_index + (wlen + 2)) >= HEAP_SIZE) gc(2, &x, &y);
    if ((free_index + (wlen + 2)) >= HEAP_SIZE) die("dalloc -- OOM after gc");
    p = busy_chunks + free_index++;
    *p = DATUM_INFO(type, len);
    p[1] = (size_t) mtab;
    free_index++;
    if (wlen > 0) p[2] = (size_t) x;
    if (wlen > 1) p[3] = (size_t) y;
    free_index += wlen;
#if GC_DEBUG
    prdesc("Allocated", *p);
#endif
    return p + 2;
}

size_t
datum_size(datum d)
{
    if (((size_t) d) & 1) return 4;
    return DATUM_LEN(*(d - 2));
}

datum
cons(datum x, datum y)
{
    return dalloc(DATUM_TYPE_PAIR, 2, pair_mtab, x, y);
}

datum
make_array(uint len)
{
    datum p;

    if (len < 1) return nil;
    if (len != CLIP_LEN(len)) die("make_array -- too big");
    p = dalloc(DATUM_TYPE_ARRAY, len, array_mtab, nil, nil);
    for (;--len;) p[len] = nil;
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
grow_closure(datum *op, uint grow_len, na_fn_free fn, void *data)
{
    datum d, fz;
    int new_len, ex = fn ? 2 + FZ_LEN : 0;

    new_len = DATUM_LEN(*(*op - 2)) + grow_len;
    regs[R_GC0] = *op;
    d = dalloc(DATUM_TYPE_CLOSURE, new_len + ex,
            (datum) (*op)[-1], (datum) **op, (datum) (*op)[1]);
    *op = regs[R_GC0];
    regs[R_GC0] = nil;
    if (grow_len) d[2] = (size_t) data;
    if (fn) {
        *(d - 2) = DATUM_INFO(DATUM_TYPE_CLOSURE, new_len);
        fz = d + new_len + 2;
        fz[-2] = DATUM_INFO(DATUM_TYPE_FZ, FZ_LEN);
        fz[-1] = nil; /* finalizer_mtab */
        fz[0] = (size_t) fn;
        fz[1] = (size_t) fz_list;
        fz[2] = (size_t) d; fz_list = fz;
    }

    become(op, &d, 1);
    return d;
}

datum
make_closure(datum env, uint *table)
{
    return dalloc(DATUM_TYPE_CLOSURE, 1, (datum) table, env, nil);
}

datum
make_bytes(uint size)
{
    return dalloc(DATUM_TYPE_BYTES, size, bytes_mtab, nil, nil);
}

datum
make_str(size_t size)
{
    return dalloc(DATUM_TYPE_STR, size, str_mtab, nil, nil);
}

char *
bytes_contents(datum bytes)
{
    if (!bytesp(bytes)) die1("bytes_contents -- not an instance of bytes", bytes);
    return (char *) bytes;
}

datum
array_get(datum arr, uint index)
{
    if (!arrayp(arr)) die1("array_get -- not an array", arr);
    if (index >= DATUM_LEN(*(arr - 2))) die("array_get -- index out of bounds");
    return (datum) arr[index];
}

datum
array_put(datum arr, uint index, datum val)
{
    if (!arrayp(arr)) die1("array_put -- not an array", arr);
    if (index >= DATUM_LEN(*(arr - 2))) die("array_put -- index out of bounds");
    return (datum) (arr[index] = (size_t) val);
}

uint
array_len(datum arr)
{
    return DATUM_LEN(*(arr - 2));
}

int
pairp(datum x)
{
    return in_chunk_range(x) &&
        (((datum) x[-1]) == pair_mtab) && (x != pair_surrogate);
}

int
arrayp(datum x)
{
    return in_chunk_range(x) &&
        (((datum) x[-1]) == array_mtab) && (x != array_surrogate);
}

int
closurep(datum x)
{
    return !(((size_t) x) & 1) && in_chunk_range(x);
}

int
bytesp(datum x)
{
    return in_chunk_range(x) &&
        (((datum) x[-1]) == bytes_mtab) && (x != bytes_surrogate);
}

int
strp(datum x)
{
    return in_chunk_range(x) && (((datum) x[-1]) == str_mtab);
}

int
broken_heartp(datum x)
{
    return in_old_chunk_range(x) &&
        (DATUM_TYPE(x[-2]) == DATUM_TYPE_BROKEN_HEART);
}

inline pair
datum2pair(datum d)
{
    if (!pairp(d)) die1("not a pair", d);
    return (pair) d;
}
