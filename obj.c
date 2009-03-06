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
