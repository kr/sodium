namodules := \
    array.na \
    bytes.na \
    file-io.na \
    int.na \
    module.na \
    nil.na \
    pair.na \
    pbox.na \
    prelude.na \
    re.na \
    reaper.na \
    str.na \
    symbol.na \
    syscall.na \
    tuple.na \

cmodules := vm.c mem.c gen.c prim.c
sources := $(cmodules) index.c $(namodules:.na=.na.c)

NAC := ./lx1c

export CFLAGS := -g -pg -Wall -Werror
#export CFLAGS := -O2 -Wall -Werror

export LDFLAGS := -pg -lpcre

all: vm

deps := $(shell bin/create-if-missing $(sources:.c=.d))
ifneq ($(MAKECMDGOALS),clean)
include $(deps)
endif

vm: $(sources:.c=.o)

index.c: $(namodules) bin/gen-mod-index
	bin/gen-mod-index --output=$@ $(namodules:.na=)

%.na: ;

%.na.c: %.na *.py lx1c
	$(NAC) $(NAFLAGS) --generate-c $<

%.na.h: %.na *.py lx1c
	$(NAC) $(NAFLAGS) --generate-h $<

# This one is special.
prelude.na.c: export NAFLAGS += --bare

check: vm
	bin/check sh-tests/*.na

clean:
	rm -f vm *.o core core.* gmon.out sh-tests/*.out *.d *.pyc
	rm -f *.lxc ad-hoc-tests/*.lxc lib/*.lxc tests/*.lxc
	rm -f *.nac ad-hoc-tests/*.nac lib/*.nac tests/*.nac
	rm -f *.na.h *.na.c index.c

# .DELETE_ON_ERROR:
.PHONY: all clean distclean reallyclean check

.SECONDARY: $(namodules:.na=.na.c)

# This tells make how to generate dependency files
%.d: %.c
	@$(CC) -MM -MG -MT '$(<:.c=.o) $@' -MF $@ $<

