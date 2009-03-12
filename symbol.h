/* symbol.h - symbol type header */

#ifndef symbol_h
#define symbol_h

#include "gen.h"

extern datum symbols;

datum intern(const char *name);
datum intern_str(datum name);

int symbolp(datum d);
size_t symbol_copy0(char *dest, size_t n, datum sym);

void pr_symbol(datum sym);


#endif /*symbol_h*/
