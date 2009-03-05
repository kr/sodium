/* symbol.h - symbol type header */

#ifndef symbol_h
#define symbol_h

#include "gen.h"

void symbol_init();

datum intern(const char *s);
int symbolp(datum d);
size_t symbol_copy0(char *dest, size_t n, datum sym);

void dump_symbol(void *s);
void pr_symbol(void *s);


#endif /*symbol_h*/
