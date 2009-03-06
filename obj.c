#include "gen.h"
#include "obj.h"
#include "mem.h"
#include "int.h"
#include "prim.h"

#include "vm.h"

datum
closure_env(datum d)
{
    return (datum) *d;
}

int
closure_has_method(datum d, datum name)
{
    int i, n;
    method_table table;

    table = (method_table) datum_mtab(d);

    n = datum2int(table->size);
    for (i = 0; i < n; ++i) {
        if (table->items[i].name == name) return 1;
    }
    return 0;
}

datum
closure_methods(datum d)
{
    int i, n;
    method_table table;
    datum methods = nil;

    /* This pointer is stable. A garbage collection will not invalidate it */
    table = (method_table) datum_mtab(d);

    n = datum2int(table->size);
    for (i = 0; i < n; ++i) methods = cons(table->items[i].name, methods);
    return methods;
}
