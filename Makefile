namodules := \
    array.na \
    bytes.na \
    file-io.na \
    fin.na \
    int.na \
    module.na \
    nil.na \
    pair.na \
    pbox.na \
    prelude.na \
    re.na \
    str.na \
    symbol.na \

cmodules := vm.c mem.c gen.c prim.c
sources := $(cmodules) module-index.c $(namodules:.na=.na.c)

export CFLAGS := -g -pg -Wall -Werror
#export CFLAGS := -O2 -Wall -Werror

export LDFLAGS := -pg -lpcre

all: vm

deps := $(shell bin/create-if-missing $(sources:.c=.d))
ifneq ($(MAKECMDGOALS),clean)
include $(deps)
endif

vm: $(sources:.c=.o)

module-index.c: $(namodules)
	./gen-mod-index --output=$@ $(namodules:.na=)

%.na: ;

%.na.c: %.na *.py
	./lx1c --module --generate-c $<

# This one is special.
prelude.na.c: prelude.na *.py
	./lx1c --generate-c $<

check: vm
	./check.sh sh-tests/*.na

clean:
	rm -f vm *.o core core.* gmon.out sh-tests/*.out *.d *.pyc
	rm -f *.lxc ad-hoc-tests/*.lxc lib/*.lxc tests/*.lxc
	rm -f *.nac ad-hoc-tests/*.nac lib/*.nac tests/*.nac
	rm -f *.na.c module-index.c

# .DELETE_ON_ERROR:
.PHONY: all clean distclean reallyclean check

.SECONDARY: $(namodules:.na=.na.c)

# This tells make how to generate dependency files
%.d: %.c
	@$(CC) -MM -MG -MT '$(<:.c=.o) $@' -MF $@ $<

