/* st.c - Symbol Table */

#include <stdlib.h>
#include <string.h>
#include "st.h"
#include "gen.h"
#include "obj.h"

#define CHAR_STAR_TAG 0x2
#define symbol2datum(x) ((datum) (((unsigned int)(x)) | CHAR_STAR_TAG))
#define datum2symbol(x) ((symbol) (((unsigned int)(x)) & ~CHAR_STAR_TAG))

typedef struct symbol *symbol;
typedef struct st_ent *st_ent;

struct symbol {
    st_ent ent;
};
struct st_ent {
    symbol sym;
    char chars[];
};

static datum intern_pres(char *s, symbol sym);

static size_t cap = 0, fill = 0, threshold = 0;
static st_ent *symbols = 0;

static void
init(size_t size)
{
    cap = size;
    fill = 0;
    threshold = cap >> 1;
    symbols = malloc(sizeof(datum) * cap);
}

/* Yikes, this is expensive. There has got to be a better way. */
static uint
next_prime(uint n)
{
    uint i;

    if (n < 2) return 2;
    if (n % 2 == 0) ++n;
    for (;; n += 2) {
        for (i = 3; i * i <= n && n % i != 0; i += 2) {}
        if (i * i > n) return n;
    }
}

static void
grow()
{
    size_t i, ocap = cap;
    st_ent *osymbols = symbols;

    init(next_prime(cap << 1));
    for (i = 0; i < ocap; i++) {
        if (osymbols[i]) intern_pres(osymbols[i]->chars, osymbols[i]->sym);
        free(osymbols[i]);
    }
    free(osymbols);
}

static uint
hash(char *s)
{
    return 0;
}

datum
intern(char *s)
{
    return intern_pres(s, 0);
}

static datum
intern_pres(char *s, symbol sym)
{
    size_t i;
    if (fill >= threshold) grow();
    i = hash(s) % cap;
    for (;; i = (i + 1) % cap) {
        if (!symbols[i]) {
            if (!sym) sym = malloc(sizeof(struct symbol));
            symbols[i] = malloc(sizeof(struct st_ent) +
                                sizeof(char) * (strlen(s) + 1));
            symbols[i]->sym = sym;
            sym->ent = symbols[i];
            strcpy(symbols[i]->chars, s);
            fill++;
            return symbol2datum(sym);
        }
        if (strcmp(s, symbols[i]->chars) == 0) {
            return symbol2datum(symbols[i]->sym);
        }
    }
}

void
dump_symbol(void *s)
{
    symbol sym;
    if (!symbolp(s)) {
        printf("<bad-symbol>");
        return;
    }
    sym = datum2symbol(s);
    printf("symbol\n  %s", sym->ent->chars);
}

void
pr_symbol(void *s)
{
    symbol sym;
    if (!symbolp(s)) {
        printf("<bad-symbol>");
        return;
    }
    sym = datum2symbol(s);
    printf("%s", sym->ent->chars);
}

int
symbolp(datum d)
{
    return ((((uint) d) & BOX_MASK) == CHAR_STAR_TAG) && !compiled_objp(d);
}

