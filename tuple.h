/* tuple.h - tuple type header */

#ifndef tuple_h
#define tuple_h

datum make_tuple(uint len);

int tuplep(datum x);
datum tuple_get(datum arr, uint index);
uint tuple_len(datum arr);

#endif /*tuple_h*/
