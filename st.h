/* st.h - Symbol Table header */

#ifndef st_h
#define st_h

#include "gen.h"

datum intern(char *s);
int symbolp(datum d);
const char * symbol2charstar(datum s);

void dump_symbol(void *s);
void pr_symbol(void *s);

#endif /*st_h*/
