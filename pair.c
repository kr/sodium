
#include <stdlib.h>
#include <string.h>
#include "pair.h"
#include "vm.h"
#include "obj.h"
#include "prim.h"
#include "config.h"

struct pair *busy_pairs, *free_pairs;
size_t free_index = 0, scan_index = 0;

// int /*bool*/
// pairp(datum x) {
//     return objp(x) &&
//         (((pair)x) >= busy_pairs) &&
//         (((pair)x) < &busy_pairs[MAX_PAIRS]);
// }

void
init_mem(void)
{
    busy_pairs = malloc(sizeof(struct pair) * MAX_PAIRS);
#if GC_DEBUG
    printf("busy_pairs = %p\n", busy_pairs);
#endif
    if (!busy_pairs) die("init_mem -- out of memory");
    free_index = 0;
}

#define CLIP_LEN(l) ((l)&0x00ffffff)
#define OBJ_INFO(t,l) (((t)<<24)|CLIP_LEN(l))
#define OBJ_TYPE(i) ((i) >> 24)
#define OBJ_TYPE_ARRAY 0x01
#define OBJ_TYPE_STRING 0x02
#define OBJ_TYPE_BLANK 0x03
#define OBJ_TYPE_BROKEN_HEART 0xff
#define OBJ_LEN(i) ((i) & 0x00ffffff)

inline pair
relocate(pair p)
{
    pair np;
    int /*len,*/ is_compiled_obj;

    if (!in_pair_range(p)) return p;

    is_compiled_obj = compiled_objp(p);
    if (is_compiled_obj) p = obj2pair(p);

#if GC_DEBUG_BH
    if (OBJ_TYPE(p->info) == OBJ_TYPE_BROKEN_HEART) {
        printf("found broken heart %p -> %p\n", p, car(p));
    }
#endif

    if (OBJ_TYPE(p->info) == OBJ_TYPE_BROKEN_HEART) return car(p);

#if GC_DEBUG
    printf("relocating pair at %p\n", p);
#endif

#if GC_DEBUG_STR
    if (OBJ_TYPE(p->info) == OBJ_TYPE_STRING) {
        printf("copying string at %p\n", p);
    }
#endif

    //switch (OBJ_TYPE(p->info)) {
    //    case OBJ_TYPE_ARRAY:
            np = &free_pairs[free_index++];
            np->info = p->info;
            for (p->info = OBJ_LEN(p->info); p->info--;) {
                np->datums[p->info] = p->datums[p->info];
                ++free_index;
            }
    //        break;
    //    case OBJ_TYPE_STRING:
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
    if (OBJ_TYPE(p->info) == OBJ_TYPE_STRING) {
        printf("copied ``%s'' to ``%s''\n",
                (char *) p->datums, (char *) np->datums);
    }
#endif

    p->info = OBJ_INFO(OBJ_TYPE_BROKEN_HEART, OBJ_LEN(p->info));
    car(p) = is_compiled_obj ? pair2obj(np) : np;
    return car(p);
}

static datum
gc(int alen, datum x, datum y, int slen, int blen)
{
    int i, live = 0;
    pair np;
    free_index = 0;

    free_pairs = malloc(sizeof(struct pair) * MAX_PAIRS);
    if (!free_pairs) die("gc -- out of memory");

    stack = relocate(stack);
    tasks = relocate(tasks);
    genv = relocate(genv);
    x = relocate(x);
    y = relocate(y);
    for (i = 0; i < REG_COUNT; ++i) {
        regs[i] = relocate(regs[i]);
    }
    for (i = 0; i < static_datums_fill; ++i) {
        static_datums[i] = relocate(static_datums[i]);
    }

    for (scan_index = 0; scan_index < free_index;) {
        np = &free_pairs[scan_index++];
        ++live;
        switch (OBJ_TYPE(np->info)) {
            case OBJ_TYPE_ARRAY:
                for (i = OBJ_LEN(np->info); i--;) {
                    np->datums[i] = relocate(np->datums[i]);
                    ++scan_index;
                }
                break;
            case OBJ_TYPE_STRING:
                scan_index += OBJ_LEN(np->info);
                break;
        }
    }

    for (scan_index = 0; scan_index < MAX_PAIRS;) {
        np = &busy_pairs[scan_index++];
        switch (OBJ_TYPE(np->info)) {
            case OBJ_TYPE_BLANK:
                ((prim) car(np))(np, destroy_sym, nil);
                break;
        }
        scan_index += OBJ_LEN(np->info);
    }

    free(busy_pairs);
    busy_pairs = free_pairs;
#if GC_DEBUG
    printf("busy_pairs = %p\n", busy_pairs);
#endif
    if (free_index >= MAX_PAIRS) die("gc -- no progress");
#if GC_STATS
    printf("gc done (%d live)\n", live);
#endif /*GC_STATS*/
    if (alen > -1) return make_array(alen);
    if (slen > -1) return make_string(slen);
    if (blen > -1) return make_blank(blen);
    return cons(x, y);
}

datum
cons(datum x, datum y)
{
    pair p;

    if ((free_index + 3) >= MAX_PAIRS) return gc(-1, x, y, -1, -1);
    p = &busy_pairs[free_index++];
    p->info = OBJ_INFO(OBJ_TYPE_ARRAY, 2);
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
    if ((free_index + (len + 1)) >= MAX_PAIRS) return gc(len, nil, nil, -1, -1);
    p = &busy_pairs[free_index++];
    p->info = OBJ_INFO(OBJ_TYPE_ARRAY, len);
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

    if ((free_index + (words + 1)) >= MAX_PAIRS) return gc(-1, nil, nil, len, -1);

    p = &busy_pairs[free_index++];
    p->info = OBJ_INFO(OBJ_TYPE_STRING, words);
    free_index += words;
    return p;
}

datum
make_blank(uint len)
{
    pair p;

    if ((free_index + (len + 1)) >= MAX_PAIRS) return gc(-1, nil, nil, -1, len);

    p = &busy_pairs[free_index++];
    p->info = OBJ_INFO(OBJ_TYPE_BLANK, len);
    free_index += len;
    return p;
}

datum
make_string_init(const char *s)
{
    datum d = make_string(strlen(s) + 1);
    strcpy(string_contents(d), s);
    return d;
}

char *
string_contents(datum str)
{
    pair p;
    if (!stringp(str)) die1("string_contents -- not a string", str);
    p = (pair) str;
    return (char *) p->datums;
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
    pair p = datum2pair(arr);;
    if (index >= OBJ_LEN(p->info)) die("array_get -- index out of bounds");
    return acc(arr, index);
}

void
array_put(datum arr, uint index, datum val)
{
    pair p = datum2pair(arr);;
    if (index >= OBJ_LEN(p->info)) die("array_put -- index out of bounds");
    acc(arr, index) = val;
}

uint
array_len(datum arr)
{
    pair p = datum2pair(arr);;
    return OBJ_LEN(p->info);
}

int
array_tag_matches(datum arr)
{
    pair p = (pair) arr;;
    return OBJ_TYPE(p->info) == OBJ_TYPE_ARRAY;
}

int
string_tag_matches(datum arr)
{
    pair p = (pair) arr;;
    return OBJ_TYPE(p->info) == OBJ_TYPE_STRING;
}

int
blank_tag_matches(datum arr)
{
    pair p = (pair) arr;;
    return OBJ_TYPE(p->info) == OBJ_TYPE_BLANK;
}
