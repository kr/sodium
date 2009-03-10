/* array.h - array type header */

#ifndef array_h
#define array_h

datum make_array(uint len);

int arrayp(datum x);
datum array_get(datum arr, uint index);
datum array_put(datum arr, uint index, datum val);
uint array_len(datum arr);

#endif /*array_h*/
