
#include <stdlib.h>
#include <string.h>
#include "pair.h"
#include "vm.h"
#include "obj.h"
#include "prim.h"
#include "config.h"

int gc_in_progress = 0;
struct pair *busy_pairs, *old_pairs;
static struct pair *free_pairs;
size_t free_index = 0, scan_index = 0;

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
#define DATUM_TYPE_STRING 0x02
#define DATUM_TYPE_OBJ 0x03
#define DATUM_TYPE_BROKEN_HEART 0xff
#define DATUM_LEN(i) ((i) & 0x00ffffff)

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
    if (DATUM_TYPE(p->info) == DATUM_TYPE_STRING) {
        printf("copying string at %p\n", p);
    }
#endif

    //switch (DATUM_TYPE(p->info)) {
    //    case DATUM_TYPE_ARRAY:
            np = &free_pairs[free_index++];
            np->info = p->info;
            for (len = DATUM_LEN(p->info); len--;) {
                np->datums[len] = p->datums[len];
                ++free_index;
            }
    //        break;
    //    case DATUM_TYPE_STRING:
    //        np = &free_pairs[free_index];
    //        strcpy((char *) np, (const char *) p);
    //        len = strlen((const char *) np);
    //        free_index += max(len / 4 + ((len % 4) ? 1 : 0), 2);
    //        break;
    //    default:
    //        die("relocate -- unknown object type");
    //}

#if GC_DEBUG
    printf("...copied\n");
#endif

#if GC_DEBUG_STR
    if (DATUM_TYPE(p->info) == DATUM_TYPE_STRING) {
        printf("copied ``%s'' to ``%s''\n",
                (char *) p->datums, (char *) np->datums);
    }
#endif

    p->info = DATUM_INFO(DATUM_TYPE_BROKEN_HEART, DATUM_LEN(p->info));
    return car(p) = np;
}

static datum
gc(int alen, datum x, datum y, int slen, int blen)
{
    int i, live = 0;
    pair np;
    free_index = 0;
    datum new;

    if (gc_in_progress) die("ran out of memory during GC");
    gc_in_progress = 1;

#if GC_DEBUG
    printf("BEGIN GC\n");
#endif

    free_pairs = malloc(sizeof(struct pair) * HEAP_SIZE);
    if (!free_pairs) die("gc -- out of memory");

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
    x = relocate(x);
    y = relocate(y);
    for (i = 0; i < REG_COUNT; ++i) {
        regs[i] = relocate(regs[i]);
    }
    for (i = 0; i < static_datums_fill; ++i) {
        static_datums[i] = relocate(static_datums[i]);
    }

#if GC_DEBUG
    printf("free_index is %d\n", free_index);
#endif
    for (scan_index = 0; scan_index < free_index;) {
        np = &free_pairs[scan_index++];
        ++live;
        switch (DATUM_TYPE(np->info)) {
            case DATUM_TYPE_ARRAY:
            case DATUM_TYPE_OBJ:
                for (i = DATUM_LEN(np->info); i--;) {
                    np->datums[i] = relocate(np->datums[i]);
                    ++scan_index;
                }
                break;
            case DATUM_TYPE_STRING:
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

    for (scan_index = 0; scan_index < HEAP_SIZE;) {
        np = &old_pairs[scan_index++];
        switch (DATUM_TYPE(np->info)) {
            case DATUM_TYPE_OBJ:
                if (compiled_obj_has_method(np, destroy_sym)) {
                    call(np, destroy_sym, nil);
                }
                /*(((prim) car(np))(np, destroy_sym))(np, nil);*/
                break;
        }
        scan_index += DATUM_LEN(np->info);
    }

    free(old_pairs);
    old_pairs = 0;

#if GC_DIE
    die("GC_DIE");
#endif

    if (alen > -1) new = make_array(alen);
    else if (slen > -1) new = make_string(slen);
    else if (blen > -1) new = make_obj(blen);
    else new = cons(x, y);
    gc_in_progress = 0;
    return new;
}

datum
cons(datum x, datum y)
{
    pair p;

    if ((free_index + 3) >= HEAP_SIZE) return gc(-1, x, y, -1, -1);
    p = &busy_pairs[free_index++];
    p->info = DATUM_INFO(DATUM_TYPE_ARRAY, 2);
    car(p) = x;
    cdr(p) = y;
    free_index += 2;
    return p;
}

datum
make_array(uint len)
{
    pair p;

    if (len < 1) return nil;
    if (len != CLIP_LEN(len)) die("make_array -- too big");
    if ((free_index + (len + 1)) >= HEAP_SIZE) return gc(len, nil, nil, -1, -1);
    p = &busy_pairs[free_index++];
    p->info = DATUM_INFO(DATUM_TYPE_ARRAY, len);
    free_index += len;
    for (;len--;) {
        p->datums[len] = nil;
    }
    return p;
}

datum
make_string(uint len)
{
    pair p;
    uint words;

    words = max(len / 4 + ((len % 4) ? 1 : 0), 1);

    if ((free_index + (words + 1)) >= HEAP_SIZE) return gc(-1, nil, nil, len, -1);

    p = &busy_pairs[free_index++];
    p->info = DATUM_INFO(DATUM_TYPE_STRING, words);
    free_index += words;
    return p;
}

datum
make_obj(uint len)
{
    pair p;

    if ((free_index + (len + 1)) >= HEAP_SIZE) return gc(-1, nil, nil, -1, len);

    p = &busy_pairs[free_index++];
    p->info = DATUM_INFO(DATUM_TYPE_OBJ, len);
    free_index += len;
    return p;
}

datum
make_compiled_obj(datum env, uint *table)
{
    pair p = cons(env, (datum) table);
    p->info = DATUM_INFO(DATUM_TYPE_OBJ, 2);
    return p;
}

datum
make_string_init_len(const char *s, int len)
{
    datum d = make_string(len + 1);
    strcpy(string_contents(d), s);
    string_contents(d)[len] = '\0';
    return d;
}

datum
make_string_init(const char *s)
{
    return make_string_init_len(s, strlen(s));
}

char *
string_contents(datum str)
{
    pair p;
    if (!stringp(str)) die1("string_contents -- not a string", str);
    p = (pair) str;
    return (char *) p->datums;
}

/* caller must free the string returned by this function */
char *
copy_string_contents(datum str)
{
    uint n;
    char *s, *x = string_contents(str);
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
string_tag_matches(datum arr)
{
    pair p = (pair) arr;
    return DATUM_TYPE(p->info) == DATUM_TYPE_STRING;
}

int
obj_tag_matches(datum o)
{
    pair p = (pair) o;
    return DATUM_TYPE(p->info) == DATUM_TYPE_OBJ;
}

int
broken_heart_tag_matches(datum bh)
{
    pair p = (pair) bh;
    return DATUM_TYPE(p->info) == DATUM_TYPE_BROKEN_HEART;
}
