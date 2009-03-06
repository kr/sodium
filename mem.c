
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include "mem.h"
#include "vm.h"
#include "obj.h"
#include "prim.h"
#include "pair.h"
#include "symbol.h"
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
datum perm_busy_chunks, busy_chunks, old_chunks, fz_list = nil;
static datum free_chunks;
static size_t perm_free_index = 0, free_index = 0, scan_index = 0;
static datum become_a = nil, become_b = nil;

void
init_mem(void)
{
    perm_busy_chunks = malloc(sizeof(datum) * HEAP_SIZE);
    if (!perm_busy_chunks) die("init_mem -- out of memory");
    perm_free_index = 0;

    busy_chunks = malloc(sizeof(datum) * HEAP_SIZE);
    if (!busy_chunks) die("init_mem -- out of memory");
    free_index = 0;
}

#define DATUM_INFO(t,l) (((l) << 4) | ((t) & 0xf))
#define DATUM_TYPE(i) ((i) & 0xf)
#define DATUM_LEN(i) ((i) >> 4)

#define DATUM_TYPE_unused1 1
#define DATUM_TYPE_unused3 3
#define DATUM_TYPE_CLOSURE 5
#define DATUM_TYPE_ARRAY 7
#define DATUM_TYPE_unused9 9
#define DATUM_TYPE_STR 11
#define DATUM_TYPE_FZ 13
#define DATUM_TYPE_BROKEN_HEART 15

#define IS_BROKEN_HEART(o) (((o) & 0xf) == DATUM_TYPE_BROKEN_HEART)

#define FZ_LEN 3

#if GC_DEBUG
static const char *datum_types[] = {
    "<unused 1>",
    "<unused 3>",
    "DATUM_TYPE_CLOSURE",
    "DATUM_TYPE_ARRAY",
    "<unused 9>",
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
                len = (len + 3) / 4;

                /* fall through */
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
    relocate((datum) &symbols);
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
                np += (len + 3) / 4 + 2;
                break;
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
            ((na_fn_free) fzp[0])(p);
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
dalloc(size_t *base, size_t *free,
       unsigned char type, uint len, datum mtab, datum x, datum y)
{
    datum p;
    size_t delta = len + 2;

    if (type == DATUM_TYPE_STR) {
        delta = (len + 3 / 4) + 2;
    }

    if ((*free + delta) >= HEAP_SIZE) gc(2, &x, &y);
    if ((*free + delta) >= HEAP_SIZE) die("dalloc -- OOM after gc");
    p = base + *free;
    *p = DATUM_INFO(type, len);
    p[1] = (size_t) mtab;
    if (delta > 2) p[2] = (size_t) x;
    if (delta > 3) p[3] = (size_t) y;
    *free += delta;
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

void
become(datum *a, datum *b, int keep_b)
{
    become_a = *a;
    become_b = *b;
    become_keep_b = keep_b;
    gc(2, a, b);
}

/* Note: *x must be the only pointer to x */
void
install_fz(datum *x, na_fn_free fn)
{
    datum fz;

    regs[R_GC0] = *x;
    fz = dalloc(busy_chunks, &free_index,
                DATUM_TYPE_FZ, 3, nil, (datum) fn, fz_list);
    fz[2] = ((size_t) (*x = regs[R_GC0]));
    regs[R_GC0] = nil;
    fz_list = fz;
}

datum
make_opaque(size_t size, datum mtab)
{
    return dalloc(busy_chunks, &free_index,
                  DATUM_TYPE_STR, size, mtab, nil, nil);
}

datum
make_record(size_t len, datum mtab, datum a, datum b)
{
    return dalloc(busy_chunks, &free_index,
                  DATUM_TYPE_ARRAY, len, mtab, a, b);
}

datum
make_opaque_permanent(size_t size, datum mtab)
{
    return dalloc(perm_busy_chunks, &perm_free_index,
                  DATUM_TYPE_STR, size, mtab, nil, nil);
}

datum
make_record_permanent(size_t len, datum mtab, datum a, datum b)
{
    return dalloc(perm_busy_chunks, &perm_free_index,
                  DATUM_TYPE_ARRAY, len, mtab, a, b);
}

int
broken_heartp(datum x)
{
    return in_old_chunk_range(x) &&
        (DATUM_TYPE(x[-2]) == DATUM_TYPE_BROKEN_HEART);
}
