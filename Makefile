lxmodules := int.lx bytes.lx str.lx pair.lx array.lx nil.lx symbol.lx \
			 pbox.lx re.lx \
			 file-io.lx fin.lx module.lx prelude.lx
cmodules := vm.c mem.c gen.c prim.c
sources := $(cmodules) module-index.c $(lxmodules:.lx=.lx.c)

export CFLAGS := -g -pg -Wall -Werror
#export CFLAGS := -O2 -Wall -Werror

export LDFLAGS := -pg -lpcre

all: vm

deps := $(shell bin/create-if-missing $(sources:.c=.d))
ifneq ($(MAKECMDGOALS),clean)
include $(deps)
endif

vm: $(sources:.c=.o)

module-index.c: $(lxmodules)
	./gen-mod-index --output=$@ $(lxmodules:.lx=)

%.lx: ;

# This one is special.
prelude.lx.c: prelude.lx *.py
	./lx1c --generate-c $<

%.lx.c: %.lx *.py
	./lx1c --module --generate-c $<

check: vm
	./check.sh sh-tests/*.na

clean:
	rm -f vm *.o core core.* gmon.out sh-tests/*.out
	rm -f *.d *.pyc *.lxc ad-hoc-tests/*.lxc lib/*.lxc tests/*.lxc
	rm -f *.nac ad-hoc-tests/*.nac lib/*.nac tests/*.nac
	rm -f *.lx.c module-index.c

# .DELETE_ON_ERROR:
.PHONY: all clean distclean reallyclean check

.SECONDARY: $(lxmodules:.lx=.lx.c)

# This tells make how to generate dependency files
%.d: %.c
	@$(CC) -MM -MG -MT '$(<:.c=.o) $@' -MF $@ $<

