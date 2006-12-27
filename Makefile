#modules
sources := vm.c pair.c obj.c gen.c prim.c st.c

export CFLAGS := -g -pg -Wall -Werror

all: vm

#ifneq ($(MAKECMDGOALS),clean)
#ifneq ($(MAKECMDGOALS),distclean)
-include $(sources:.c=.d)
#endif
#endif


vm: $(sources:.c=.o)

clean:
	rm -f vm *.o

distclean: clean
	rm -f *.d *.pyc *.lxc

# .DELETE_ON_ERROR:
.PHONY: all clean distclean

# This tells make how to generate dependency files
%.d: %.c
	@$(SHELL) -ec '$(CC) -MM $(CPPFLAGS) $< \
	              | sed '\''s/\($*\)\.o[ :]*/\1.o $@ : /g'\'' > $@; \
				  [ -s $@ ] || rm -f $@'

