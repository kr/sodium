/* pbox.h - pbox type header */

#ifndef pbox_h
#define pbox_h

typedef void(*pbox_fn_free)(void *);

/* fn may be NULL */
datum make_pbox(void *p, pbox_fn_free fn);

#endif /*pbox_h*/
