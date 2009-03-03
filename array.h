/* array.h - array type header */

#ifndef array_h
#define array_h

extern datum array_mtab;

void array_init();

datum make_array(uint len);

#endif /*array_h*/
