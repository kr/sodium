lxmodules := int.lx bytes.lx str.lx pair.lx array.lx nil.lx symbol.lx re.lx \
			 file-io.lx fin.lx module.lx prelude.lx
cmodules := vm.c mem.c obj.c gen.c prim.c st.c
sources := $(cmodules) module-index.c $(lxmodules:.lx=.lxc.c)

export CFLAGS := -g -pg -Wall -Werror
#export CFLAGS := -O2 -Wall -Werror

export LDFLAGS := -pg -lpcre

all: vm

#ifneq ($(MAKECMDGOALS),clean)
#ifneq ($(MAKECMDGOALS),distclean)
#ifneq ($(MAKECMDGOALS),reallyclean)
-include $(sources:.c=.d)
#endif
#endif
#endif

vm: $(sources:.c=.o)

module-index.c: $(lxmodules)
	./gen-mod-index --output=$@ $(lxmodules:.lx=)

# This one is special.
prelude.lxc.c: prelude.lx
	./lx1c --generate-c --output=$@ $<

%.lxc.c: %.lx
	./lx1c --module --generate-c --output=$@ $<

check:
	./check.sh sh-tests/*.na

clean:
	rm -f vm *.o core core.* gmon.out

distclean: clean
	rm -f *.d *.pyc *.lxc ad-hoc-tests/*.lxc lib/*.lxc tests/*.lxc

reallyclean: distclean
	rm -f *.lxc.c module-index.c

# .DELETE_ON_ERROR:
.PHONY: all clean distclean reallyclean check

# This tells make how to generate dependency files
%.d: %.c
	@$(SHELL) -ec '$(CC) -MM $(CPPFLAGS) $< \
	              | sed '\''s/\($*\)\.o[ :]*/\1.o $@ : /g'\'' > $@; \
				  [ -s $@ ] || rm -f $@'

