
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include "mem.h"
#include "vm.h"
#include "prim.h"
#include "int.na.h"
#include "reaper.na.h"
#include "symbol.h"
#include "config.h"

#define HEAP_SIZE (2 * 1024 * 1024)

int gc_in_progress = 0, fin_in_progress = 0, become_keep_b = 0;

struct fz_head_struct {
    size_t desc;
    datum mtab;
    datum object;
    datum cdr;
    datum state;
};

struct fz_head_struct fz_head = {
    make_desc(DATUM_FORMAT_FZ, 3),
    reaper_mtab,
    nil, /* object */
    nil, /* cdr */
    nil, /* state */
};

static datum perm_base, perm_top, perm_ptr;
static datum busy_base;
#define busy_ptr (regs[R_FREE])
datum busy_top;
static datum to_base, to_top, to_ptr;

static datum become_a = nil, become_b = nil;
static datum fz_list = (datum) &fz_head.object;

void
init_mem(void)
{
    perm_base = malloc(sizeof(datum) * HEAP_SIZE);
    if (!perm_base) die("init_mem -- out of memory");
    perm_ptr = perm_base;
    perm_top = perm_base + HEAP_SIZE;

    busy_base = malloc(sizeof(datum) * HEAP_SIZE);
    if (!busy_base) die("init_mem -- out of memory");
    busy_ptr = busy_base;
    busy_top = busy_base + HEAP_SIZE;
}

static inline char
datum_desc_format(size_t desc)
{
    return desc & 0xf;
}

static inline size_t
datum_desc_len(size_t desc)
{
    return desc >> 4;
}

#if GC_DEBUG
static const char *datum_types[] = {
    "DATUM_FORMAT_RECORD",
    "DATUM_FORMAT_BROKEN_HEART",
    "DATUM_FORMAT_BACKPTR",
    "DATUM_FORMAT_EMB_OPAQUE",
    "<unused 9>",
    "<unused 11>",
    "DATUM_FORMAT_FZ",
    "DATUM_FORMAT_OPAQUE",
};

static void
prdesc(const char *msg, size_t desc)
{
    if (desc &1) {
        int type = datum_desc_format(desc);
        prfmt(1, "%s %s (%d) len %u\n",
                msg, datum_types[type >> 1], type, datum_desc_len(desc));
    } else {
        prfmt(1, "%s <not a chunk 0x%x>\n", msg, desc);
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

    if (p < busy_base) return;
    if (p >= busy_top) return;

    --p; /* make p point at the mtab or descriptor */

    for (;;) {
        len = datum_desc_len(*p);
        switch (datum_desc_format(*p)) {
            case DATUM_FORMAT_BACKPTR:
                p -= len;
                continue;

            case DATUM_FORMAT_OPAQUE:
                len = (len + 3) / 4;

                /* fall through */
            case DATUM_FORMAT_RECORD:
            case DATUM_FORMAT_FZ:
#if GC_DEBUG
                prdesc("Relocating", *p);
                prfmt(1, "at %p\n", p);
#endif
                i = (datum *) p;
                j = (datum *) to_ptr;

                /*
                while (j + len >= to_ptr) {
                    to_lim = gmore();
                }
                */

                *j++ = *i++; /* copy the descriptor */
                *j++ = *i++; /* copy the method table pointer */
                while (len--) *j++ = *i++; /* copy the body */

                ((datum *) p)[1] = 2 + (datum) to_ptr;
                to_ptr = (datum) j;
#if GC_DEBUG
                prfmt(1, "Relocated\n");
#endif

                *p = DATUM_FORMAT_BROKEN_HEART;

                /* fall through */
            case DATUM_FORMAT_BROKEN_HEART:
                *refloc += p[1] - ((size_t) (p + 2));
                return;

            case DATUM_FORMAT_EMB_OPAQUE:
            default:
                p--;
                continue;
        }
    }
}

static datum saved_stack = 0, saved_x1 = 0, saved_x2 = 0,
             saved_regs[REG_COUNT] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};

static void
save_regs(datum x1, datum x2)
{
    int i;

    saved_stack = stack;
    for (i = 0; i < REG_COUNT; i++) {
        if (i == R_FREE) continue; /* this register doesn't hold a datum */
        saved_regs[i] = regs[i];
    }
    saved_x1 = x1;
    saved_x2 = x2;
}

static void
restore_regs(datum *x1, datum *x2)
{
    int i;

    stack = saved_stack;
    saved_stack = 0;
    for (i = 0; i < REG_COUNT; i++) {
        if (i == R_FREE) continue; /* this register doesn't hold a datum */
        regs[i] = saved_regs[i];
        saved_regs[i] = 0;
    }
    *x1 = saved_x1;
    *x2 = saved_x2;
}

static void
gc(datum *x1, datum *x2)
{
    int i, live = 0;
    int scanned_finalizers = 0;
    datum np, fzp;
    datum old_become_a, old_become_b;

    if (gc_in_progress) die("ran out of memory during GC");
    gc_in_progress = 1;

#if GC_DEBUG
    prfmt(1, "Start GC\n");
#endif

    to_base = malloc(sizeof(datum) * HEAP_SIZE);
    if (!to_base) die("gc -- out of memory");
    to_ptr = to_base;
    to_top = to_base + HEAP_SIZE;

#if GC_DEBUG
    prfmt(1, "to_base is %p\n", to_base);
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
    relocate((datum) &symbols);
    relocate((datum) (fz_list + 1));
    relocate((datum) x1);
    relocate((datum) x2);
    for (i = 0; i < REG_COUNT; ++i) {
        if (i == R_FREE) continue; /* this register doesn't hold a datum */
        relocate((datum) &regs[i]);
    }

    relocate((datum) &saved_stack);
    for (i = 0; i < REG_COUNT; ++i) {
        if (i == R_FREE) continue; /* this register doesn't hold a datum */
        relocate((datum) saved_regs + i);
    }
    relocate((datum) &saved_x1);
    relocate((datum) &saved_x2);

    np = to_base;
    while (np < to_ptr) {
        while (np < to_ptr) {
            ++live;
            datum p = np + 2;
            size_t descr = *np, len = datum_desc_len(descr);
            relocate(np + 1); /* relocate the method table pointer */
            switch (datum_desc_format(descr)) {
                case DATUM_FORMAT_OPAQUE:
                    np += (len + 3) / 4 + 2;
                    break;
                case DATUM_FORMAT_FZ:
                    np += len + 2;
                    //relocate(p);
                    relocate(p + 1);
                    break;
                case DATUM_FORMAT_RECORD:
                default:
#if GC_DEBUG
                    prdesc("scanning", descr);
#endif
                    np += len + 2;
                    while (p < np) relocate(p++);
            }
        }

        if (!scanned_finalizers) {
            /* Now process the finalizer list. For each entry:
             *  - if the object died, set fzp's state to 'dead
             *  - relocate the object
             */
            for (fzp = fz_list; fzp != nil; fzp = (datum) fzp[1]) {
                datum p = (datum) fzp[0];
                if (datum_desc_format(p[-2]) != DATUM_FORMAT_BROKEN_HEART) {
                    if (fzp[2] == (size_t) live_sym) fzp[2] = (size_t) dead_sym;
                }
                relocate(fzp);
            }

            scanned_finalizers = 1;
        }
    }

    free(busy_base);
    busy_base = to_base;
    busy_ptr = to_ptr;
    busy_top = to_top;
    if (to_ptr >= to_top) die("gc -- no progress");

#if GC_DEBUG
    prfmt(1, "Finish GC\n");
#endif

    gc_in_progress = 0;
    become_a = become_b = nil;

    /* Call x.finalize for each dead x. */
    if (!fin_in_progress) {
        fin_in_progress = 1;
        save_regs(*x1, *x2);
        call(fz_list, reap0_sym, nil);
        restore_regs(x1, x2);
        fin_in_progress = 0;
    }
}

void
fault()
{
    gc(regs + R_NIL, regs + R_NIL);
}

static datum
dalloc(size_t **ptr, size_t **top,
       unsigned char type, uint len, datum mtab, datum x, datum y)
{
    datum p;
    size_t delta = len + 2;

    if (type == DATUM_FORMAT_OPAQUE) {
        delta = (len + 3 / 4) + 2;
    }

    if ((*ptr + delta) >= *top) gc(&x, &y);
    if ((*ptr + delta) >= *top) die("dalloc -- OOM after gc");
    p = *ptr;
    *p = make_desc(type, len);
    p[1] = (size_t) mtab;
    if (delta > 2) p[2] = (size_t) x;
    if (delta > 3) p[3] = (size_t) y;
    *ptr += delta;
#if GC_DEBUG
    //prdesc("Allocated", *p);
#endif
    return p + 2;
}

size_t
datum_size(datum d)
{
    if (((size_t) d) & 1) return 4;
    while (!(1 & *--d));
    return datum_desc_len(*d);
}

int
opaquep(datum d)
{
    if (((size_t) d) & 1) return 1;
    while (!(1 & *--d));
    return datum_desc_format(*d) == DATUM_FORMAT_OPAQUE;
}

int
broken_heartp(datum x)
{
    if (((size_t) x) & 1) return 1;
    while (!(1 & *--x));
    return datum_desc_format(*x) == DATUM_FORMAT_BROKEN_HEART;
}

datum
datum_mtab(datum d)
{
    if (intp(d)) return int_mtab;
    return (datum) d[-1];
}

void
become(datum *a, datum *b, int keep_b)
{
    become_a = *a;
    become_b = *b;
    become_keep_b = keep_b;
    gc(a, b);
}

/* Note: *x must be the only pointer to x */
void
install_fz(datum *x)
{
    datum fz;

    if (intp(*x)) return;

    fz = dalloc(&busy_ptr,  &busy_top, DATUM_FORMAT_FZ, 3,
            reaper_mtab, (datum) *x, (datum) fz_list[1]);
    fz[2] = (size_t) live_sym;
    fz_list[1] = (size_t) fz;
}

datum
first_reaper()
{
    return fz_list;
}

datum
make_opaque(size_t size, datum mtab)
{
    return dalloc(&busy_ptr,  &busy_top,
                  DATUM_FORMAT_OPAQUE, size, mtab, nil, nil);
}

datum
make_record(size_t len, datum mtab, datum a, datum b)
{
    return dalloc(&busy_ptr,  &busy_top,
                  DATUM_FORMAT_RECORD, len, mtab, a, b);
}

datum
make_opaque_permanent(size_t size, datum mtab)
{
    return dalloc(&perm_ptr, &perm_top,
                  DATUM_FORMAT_OPAQUE, size, mtab, nil, nil);
}

datum
make_record_permanent(size_t len, datum mtab, datum a, datum b)
{
    return dalloc(&perm_ptr, &perm_top,
                  DATUM_FORMAT_RECORD, len, mtab, a, b);
}
