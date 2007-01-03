from typ import S, Integer, String, Decimal
#from util import traced

all_writable_regs = (
    S('nil'),
    S('env'),
    S('proc'),
    S('val'),
    S('argl'),
    S('continue'),
    S('tmp'),
)

all_readonly_regs = (
    S('global'),
)

all_regs = all_writable_regs + all_readonly_regs
all_regs_dict = dict([(str(k),i) for i,k in enumerate(all_regs)])

import_names = []

# snarfed from lx.py
def self_evaluatingp(exp):
    return isinstance(exp, Integer) or isinstance(exp, String) or isinstance(exp, Decimal)
def nullp(l): return len(l) == 0
def car(t):
    if isinstance(t, (tuple, list)): return t[0]
    return t.car()
def cdr(t):
    if isinstance(t, (tuple, list)): return t[1:]
    return t.cdr()

def symbolp(exp):
    return isinstance(exp, S)

def byte0(i): return (i >> 24) & 0xff
def byte1(i): return (i >> 16) & 0xff
def byte2(i): return (i >> 8) & 0xff
def byte3(i): return i & 0xff

def encode_int(i):
    return chr(byte0(i)) + chr(byte1(i)) + chr(byte2(i)) + chr(byte3(i))

def pseudo_box(n):
    INT_TAG = 0x1
    return (n << 1) | INT_TAG

def fmt_int(i, l=4, c='\n'):
    return str(i).ljust(l, c)

class AssemblingError(Exception):
    pass

def make_datum_c_name(n):
    return 'datum_%d' % (n,)

unique_c_name_counter = 0
def gen_unique_c_name():
    global unique_c_name_counter
    unique_c_name_counter += 1
    return 'datum_pair_%d' % (unique_c_name_counter,)

the_labels = None
class make_ir_seq:
    def __init__(self, needs, modifies, *statements):
        self.needs = frozenset(needs)
        self.modifies = frozenset(modifies)
        self.statements = statements

    def __repr__(self):
        return '<IR %r %r %r>' % (self.needs, self.modifies, self.statements)

    def format(self):
        s = 'needs:%r modifies:%r\n' % (list(self.needs), list(self.modifies))
        for stmt in self.statements:
            if not symbolp(stmt): s += '  '
            s += str(stmt) + '\n'
        return s

    def extract(self):
        global the_labels
        datums = self.get_se_datums() + self.get_symbols()
        datums = self.get_lists(datums)
        labels, self.statements = self.get_labels()
        the_labels = labels
        return datums, labels

    def gen_c(self, name, fd, hfd):
        datums, labels = self.extract()

        h_tpl = '''
#ifndef %(name)s_h
#define %(name)s_h

/* #include <stdio.h> */

/* typedef void * datum; */

extern struct lxc_module lxc_module_%(name)s;

#endif /*%(name)s_h*/
        '''

        print >>hfd, h_tpl % { 'name':name }

        print >>fd, '#include "lxc.h"'
        print >>fd, '#include "module-index.h"'
        print >>fd, '#include "%s.lxc.h"' % (name,)

        # import names
        import_names_id = '((const char **) 0)'
        if import_names:
            import_names_id = 'import_names'
            print >>fd
            print >>fd, 'static const char **import_names = {'
            for import_name in import_names:
                print >>fd, '    "%s",' % (import_name,)
            print >>fd, '};'

        # datums
        print >>fd
        sdts = ''
        datum_names = []
        for i, d in enumerate(datums):
            c_name = make_datum_c_name(i)
            datum_names.append(c_name)

            if symbolp(d):
                sdts += '$'
                print >>fd, '#define %s "%s"' % (c_name, str(d))
            elif isinstance(d, String):
                sdts += '@'
                print >>fd, '#define %s "%s"' % (c_name, str(d))
            elif self_evaluatingp(d):
                sdts += '#'
                print >>fd, '#define %s %d' % (c_name,
                        pseudo_box(int(d)))
            else: # it is a list
                sdts += '('
                self.emit_c_pair(fd, tuple(reversed(d)), c_name)

        print >>fd, 'static uint static_datum_entries[] = {'
        for datum_name in datum_names:
            print >>fd, '    (uint) %s,' % (datum_name,)
        print >>fd, '};'

        # labels
        print >>fd, 'static uint label_offsets[] = {'
        for s, i in labels:
            print >>fd, '    %d,' % (i,)
            #fd.write(fmt_int(i, 4, ' '))
        print >>fd, '};'

        # instrs
        print >>fd, 'static uint instrs[] = {'
        for s in self.statements:
            if symbolp(s): continue
            i = s.encode(fd, labels, datums)
            print >>fd, '    0x%x,' % (i,)
        print >>fd, '};'

        print >>fd
        print >>fd, 'struct lxc_module lxc_module_%s = {' % (name,)
        print >>fd, '    "%s",' % (name,)
        print >>fd, '    %s,' % (import_names_id,)
        print >>fd, '    %d,' % (len(import_names),)
        print >>fd, '    {'
        print >>fd, '        "%s",' % (sdts,)
        print >>fd, '        static_datum_entries,'
        print >>fd, '    },'
        print >>fd, '    %d,' % (len(datums),)
        print >>fd, '    instrs,'
        print >>fd, '    %d,' % (len(self.statements),)
        print >>fd, '    label_offsets,'
        print >>fd, '};'

    def assemble(self, fd):
        datums, labels = self.extract()

        self.emit_magic(fd)
        self.emit_import_names(fd)
        self.emit_datums(fd, datums)
        self.emit_labels(fd, labels)
        self.emit_instructions(fd, labels, datums)
        #self.list_instructions() # for debugging

    @staticmethod
    def emit_magic(fd):
        fd.write("\x89LX1\x0d\n\x1a\n")

    @staticmethod
    def emit_import_names(fd):
        fd.write(encode_int(len(import_names)))
        for d in import_names:
            fd.write(str(d))
            fd.write('\0')

    def emit_c_pair(self, fd, p, c_name):
        if len(p):
            next_c_name = gen_unique_c_name()
            self.emit_c_pair(fd, p[1:], next_c_name)
            item_name = make_datum_c_name(p[0])
            print >>fd, 'static struct spair %s_s = { (uint) %s, (uint) %s };' % (c_name,
                    item_name, next_c_name)
            print >>fd, '#define %s &%s_s' % (c_name, c_name)
        else:
            print >>fd, '#define %s 0' % (c_name,)

    @staticmethod
    def emit_datums(fd, datums):
        fd.write(encode_int(len(datums)))
        #for i, d in enumerate(datums):
        for d in datums:
            if symbolp(d):
                fd.write('$')
                fd.write(str(d))
                fd.write('\0')
            elif isinstance(d, String):
                fd.write('@')
                fd.write(encode_int(len(d)))
                fd.write(d)
            elif self_evaluatingp(d):
                fd.write('#')
                d = int(d) # hack to get floats to "work" for now
                fd.write(encode_int(pseudo_box(d)))
            else: # it is a list
                fd.write('(')
                if len(d):
                    fd.write(' ')
                    fd.write(encode_int(d[-1]))
                    for x in reversed(d[:-1]):
                        fd.write(' ')
                        fd.write(encode_int(x))
                fd.write(')')

    @staticmethod
    def emit_labels(fd, labels):
        fd.write(encode_int(len(labels)))
        for s, i in labels:
            fd.write(encode_int(i))
            #fd.write(fmt_int(i, 4, ' '))

    def get_se_datums(self):
        datums = frozenset()
        for s in self.statements:
            if symbolp(s): continue
            datums |= s.se_datums()
        return tuple(datums)

    def get_symbols(self):
        symbols = frozenset()
        for s in self.statements:
            if symbolp(s): continue
            symbols |= s.symbols()
        return tuple(symbols)

    def get_lists(self, datums):
        for s in self.statements:
            if symbolp(s): continue
            ex = s.lists(datums)
            datums += ex
        return datums

    def get_labels(self):
        labels, statements = [], []
        i = 0
        for s in self.statements:
            if symbolp(s):
                labels.append((s, i))
            else:
                statements.append(s)
                i += 1 # only count real statements
                if s.op in (load_addr_s, bf_s, bprim_s, goto_label_s):
                    statements.append(ADDR(s.l))
                    i += 1
        statements.append(QUIT())
        return labels, statements

    def list_instructions(self):
        for i, s in enumerate(self.statements):
            for l,k in the_labels:
                if i == k: print l
            print '    %r' % (s,)

    def emit_instructions(self, fd, labels, datums):
        fd.write(encode_int(len(self.statements)))
        for s in self.statements:
            if symbolp(s): continue
            s.emit(fd, labels, datums)

def empty_instruction_seq():
    return make_ir_seq((), ())

def append_ir_seqs(*seqs):
    def append_2_sequences(s1, s2):
        return make_ir_seq(
            registers_needed(s1) | (registers_needed(s2) -
                                    registers_modified(s1)),
            registers_modified(s1) | registers_modified(s2),
            *(statements(s1) + statements(s2)))
    def append_seq_list(seqs):
        if nullp(seqs): return empty_instruction_seq()
        return append_2_sequences(car(seqs),
                                  append_seq_list(cdr(seqs)))
    return append_seq_list(seqs)

def preserving(regs, s1, s2):
    for reg in regs:
        if needs_registerp(s2, reg) and modifies_registerp(s1, reg):
            s1 = make_ir_seq(frozenset([reg]) | registers_needed(s1),
                             registers_modified(s1) - frozenset([reg]),
                             *((PUSH(reg),) + statements(s1) + (POP(reg),)))
    return append_ir_seqs(s1, s2)

def tack_on_ir_seq(seq, body):
    return make_ir_seq(registers_needed(seq), registers_modified(seq),
                       *(statements(seq) + statements(body)))

def parallel_ir_seqs(s1, s2):
    return make_ir_seq(registers_needed(s1) | registers_needed(s2),
                       registers_modified(s1) | registers_modified(s2),
                       *(statements(s1) + statements(s2)))

def registers_needed(s):
    if symbolp(s): return frozenset()
    return s.needs

def registers_modified(s):
    if symbolp(s): return frozenset()
    return s.modifies

def statements(s):
    if symbolp(s): return (s,)
    return s.statements

def needs_registerp(seq, reg):
    return reg in registers_needed(seq)

def modifies_registerp(seq, reg):
    return reg in registers_modified(seq)

#label_counter = 0
label_counters = {}
def make_label(sym):
    #global label_counter
    if sym not in label_counters:
        label_counters[sym] = 1
        return S(sym)
    #label_counter += 1
    #return S(sym + str(label_counter))
    label_counters[sym] += 1
    return S(sym + str(label_counters[sym]))

# The noop instruction
nop_s = S('NOP')
def NOP(): return OP_NOP()

# The datum pseudo-instruction
datum_op_s = S('DATUM')
def DATUM(d): return OP_DATUM(d)

# The addr pseudo-instruction
addr_op_s = S('ADDR')
def ADDR(l): return OP_L(addr_op_s, l)

# No-payload instructions

quit_s = S('QUIT')
def QUIT(): return OP_Z(quit_s)

# One register instructions

goto_reg_s = S('GOTO_REG')
push_s = S('PUSH')
pop_s = S('POP')
def GOTO_REG(target_reg): return OP_R(goto_reg_s, target_reg)
def PUSH(reg): return OP_R(push_s, reg)
def POP(reg): return OP_R(pop_s, reg)

# One label instructions

goto_label_s = S('GOTO_LABEL')
def GOTO_LABEL(target_label): return OP_L(goto_label_s, target_label)

# Two register instructions

mov_s = S('MOV')
compiled_obj_env_s = S('COMPILED_OBJ_ENV')
list_s = S('LIST')
def MOV(target_reg, src_reg): return OP_RR(mov_s, target_reg, src_reg)
def COMPILED_OBJ_ENV(target_reg, proc_reg):
    return OP_RR(compiled_obj_env_s, target_reg, proc_reg)
def LIST(target_reg, val_reg): return OP_RR(list_s, target_reg, val_reg)


# One register, one label instructions

load_addr_s = S('LOAD_ADDR')
bf_s = S('BF')
bprim_s = S('BPRIM')
def LOAD_ADDR(target_reg, label): return OP_RL(load_addr_s, target_reg, label)
def BF(reg, label): return OP_RL(bf_s, reg, label)
def BPRIM(reg, label): return OP_RL(bprim_s, reg, label)

# One register, one value instructions

load_imm_s = S('LOAD_IMM')
make_array_s = S('MAKE_ARRAY')
import_s = S('LOOKUP_MODULE')
def LOAD_IMM(target_reg, val): return OP_RD(load_imm_s, target_reg, val)
def MAKE_ARRAY(target_reg, len): return OP_RD(make_array_s, target_reg, len)
def LOOKUP_MODULE(target_reg, name):
    import_names.append(name)
    return OP_RD(import_s, target_reg, name)

# Three register instructions

cons_s = S('CONS')
make_compiled_obj_s = S('MAKE_COMPILED_OBJ')
def CONS(target_reg, car_reg, cdr_reg):
    return OP_RRR(cons_s, target_reg, car_reg, cdr_reg)
def MAKE_COMPILED_OBJ(target_reg, env_reg, label_reg):
    return OP_RRR(make_compiled_obj_s, target_reg, env_reg, label_reg)


# Two register, one symbol instructions

compiled_object_method_s = S('COMPILED_OBJECT_METHOD')
set__s = S('SET_')
define_s = S('DEFINE')
lookup_s = S('LOOKUP')
def COMPILED_OBJECT_METHOD(target_reg, obj_reg, name):
    return OP_RRD(compiled_object_method_s, target_reg, obj_reg, name)
def SET_(env_reg, val_reg, name):
    return OP_RRD(set__s, env_reg, val_reg, name)
def DEFINE(env_reg, val_reg, name):
    return OP_RRD(define_s, env_reg, val_reg, name)
def LOOKUP(target_reg, env_reg, name):
    return OP_RRD(lookup_s, target_reg, env_reg, name)

# Two register, two integer instructions

lexical_lookup_s = S('LEXICAL_LOOKUP')
lexical_setbang_s = S('LEXICAL_SETBANG')
def LEXICAL_LOOKUP(target_reg, addr):
    # env_r is implied
    return OP_RII(lexical_lookup_s, target_reg, addr[0], addr[1])
def LEXICAL_SETBANG(val_reg, addr):
    # env_r is implied
    return OP_RII(lexical_setbang_s, val_reg, addr[0], addr[1])

# Three register, one list instructions

extend_environment_s = S('EXTEND_ENVIRONMENT')
apply_primitive_procedure_s = S('APPLY_PRIMITIVE_PROC')
def EXTEND_ENVIRONMENT(target_reg, env_reg, argl_reg, formals_r):
    return OP_RRRR(extend_environment_s, target_reg, env_reg, argl_reg, formals_r)
def APPLY_PRIMITIVE_PROC(target_reg, proc_reg, mess_reg, argl_reg):
    return OP_RRRR(apply_primitive_procedure_s, target_reg, proc_reg, mess_reg, argl_reg)

all_ops = (
    nop_s,
    datum_op_s,
    addr_op_s,
    goto_reg_s,
    push_s,
    pop_s,
    quit_s,
    goto_label_s,
    mov_s,
    compiled_obj_env_s,
    list_s,
    load_addr_s,
    bf_s,
    bprim_s,
    load_imm_s,
    cons_s,
    apply_primitive_procedure_s,
    make_compiled_obj_s,
    compiled_object_method_s,
    set__s,
    make_array_s,
    define_s,
    lookup_s,
    lexical_lookup_s,
    lexical_setbang_s,
    extend_environment_s,
    import_s,
)
all_ops_dict = dict([(k,i) for i,k in enumerate(all_ops)])

def pack(*chunks):
    val = 0L
    tlen = 0
    for len, data in chunks:
        if data >= (1L << len):
            raise ValueError, (len, data)
        val <<= len
        val |= data
        tlen += len
    return tlen, val

def pad(tlen, *chunks):
    len, data = pack(*chunks)
    if tlen < len:
        raise ValueError, (len, data)
    return data << (tlen - len)

class OP(object):
    def __init__(self, op):
        self.op = op
        self.opcode = lookup_op(op)

    def __repr__(self):
        return self.op

    def encode(self, fd, labels, datums):
        body = self.get_body(labels, datums)
        inst = pad(32, (5, self.opcode), body)
        return inst

    def emit(self, fd, labels, datums):
        inst = self.encode(fd, labels, datums)
        fd.write(encode_int(inst))

    def se_datums(self):
        return frozenset()

    def symbols(self):
        return frozenset()

    def lists(self, datums):
        return ()

def lookup_op(op):
    return all_ops_dict[op]

def lookup_reg(r):
    return all_regs_dict[str(r)]

def lookup_lab(l, labels):
    for i,e in enumerate(labels):
        if l is e[0]: return i
    raise KeyError, l

def lab_repr(l):
    if the_labels is None: return l
    for i,e in enumerate(the_labels):
        if l is e[0]: return (i, l, e[1])
    raise KeyError, l

def lookup_dat(d, datums):
    i, ex = find_datum(d, datums)
    if ex: raise AssemblingError, ('datum not found: %r' % (d,))
    return i

def extract_se_datums(x):
    if self_evaluatingp(x): return frozenset((x,))
    if symbolp(x) or x.nullp(): return frozenset()
    return extract_se_datums(x.car()) | extract_se_datums(x.cdr())

def extract_symbols(x):
    if symbolp(x): return frozenset((x,))
    if self_evaluatingp(x) or x.nullp(): return frozenset()
    return extract_symbols(x.car()) | extract_symbols(x.cdr())

def find_datum(x, datums):
    # base case: x is not a list
    if symbolp(x) or self_evaluatingp(x):
        for i, d in enumerate(datums):
            if d == x: return i, ()
        raise AssemblingError, ('datum not found: %r' % (x,))

    # flatten elements
    flat = ()
    ex = ()
    for e in x:
        ne, nex = find_datum(e, datums)
        flat += (ne,)
        ex += nex
        datums += nex

    # if flat list is in datums, return index
    for i, d in enumerate(datums):
        if d == flat: return i, ex

    # else, add list to datums, return new index
    return len(datums), ex + (flat,)

def extract_lists(x, datums):
    i, extra = find_datum(x, datums)
    return extra

class OP_NOP(OP):
    def __init__(self):
        OP.__init__(self, nop_s)

    def get_body(self, labels, datums):
        return pack((0, 0))

class OP_DATUM(OP):
    def __init__(self, d):
        OP.__init__(self, datum_op_s)
        self.d = d

    def get_body(self, labels, datums):
        i = lookup_dat(self.d, datums)
        return pack((27, i))

    def se_datums(self):
        return extract_se_datums(self.d)

    def symbols(self):
        return extract_symbols(self.d)

    def lists(self, datums):
        x = self.d
        if symbolp(x) or self_evaluatingp(x): return ()
        return extract_lists(x, datums)

    def __repr__(self):
        return '%s %s' % (self.op, self.d)

class OP_Z(OP):
    def __init__(self, op):
        OP.__init__(self, op)

    def get_body(self, labels, datums):
        return pack((0, 0))

class OP_R(OP):
    def __init__(self, op, r):
        OP.__init__(self, op)
        self.reg = r
        self.r = lookup_reg(r)

    def get_body(self, labels, datums):
        return pack((5, self.r))

    def __repr__(self):
        return '%s %s' % (self.op, self.reg)

class OP_L(OP):
    def __init__(self, op, l):
        OP.__init__(self, op)
        self.l = l

    def get_body(self, labels, datums):
        i = lookup_lab(self.l, labels)
        return pack((27, i))

    def __repr__(self):
        return '%s %s' % (self.op, lab_repr(self.l))

class OP_RR(OP):
    def __init__(self, op, r1, r2):
        OP.__init__(self, op)
        self.r1 = lookup_reg(r1)
        self.r2 = lookup_reg(r2)

    def get_body(self, labels, datums):
        return pack((5, self.r1), (5, self.r2))

class OP_RL(OP):
    def __init__(self, op, r, l):
        OP.__init__(self, op)
        self.reg = r
        self.r = lookup_reg(r)
        self.l = l
        if l is S('quit'): raise 'aaa'

    def get_body(self, labels, datums):
        i = lookup_lab(self.l, labels)
        return pack((5, self.r), (22, i))

    def __repr__(self):
        return '%s %r %s' % (self.op, self.reg, lab_repr(self.l))

class OP_RD(OP):
    def __init__(self, op, r, d):
        OP.__init__(self, op)
        self.reg = r
        self.r = lookup_reg(r)
        self.d = d

    def get_body(self, labels, datums):
        i = lookup_dat(self.d, datums)
        return pack((5, self.r), (22, i))

    def se_datums(self):
        return extract_se_datums(self.d)

    def symbols(self):
        return extract_symbols(self.d)

    def lists(self, datums):
        x = self.d
        if symbolp(x) or self_evaluatingp(x): return ()
        return extract_lists(x, datums)

    def __repr__(self):
        return '%s %s %r' % (self.op, self.reg, self.d)

class OP_RRR(OP):
    def __init__(self, op, r1, r2, r3):
        OP.__init__(self, op)
        self.r1 = lookup_reg(r1)
        self.r2 = lookup_reg(r2)
        self.r3 = lookup_reg(r3)

    def get_body(self, labels, datums):
        return pack((5, self.r1), (5, self.r2), (5, self.r3))

class OP_RRD(OP):
    def __init__(self, op, r1, r2, d):
        OP.__init__(self, op)
        self.reg1 = r1
        self.reg2 = r2
        self.r1 = lookup_reg(r1)
        self.r2 = lookup_reg(r2)
        if not isinstance(d, S):
            raise AssemblingError, 'd must be a symbol: %r' % d
        self.d = d

    def get_body(self, labels, datums):
        i = lookup_dat(self.d, datums)
        return pack((5, self.r1), (5, self.r2), (17, i))

    def symbols(self):
        return frozenset((self.d,))

    def __repr__(self):
        return '%s %s %s %r' % (self.op, self.reg1, self.reg2, self.d)

class OP_RII(OP):
    def __init__(self, op, r, levs, offs):
        OP.__init__(self, op)
        self.reg = r
        self.r = lookup_reg(r)
        self.levs = levs
        self.offs = offs

    def get_body(self, labels, datums):
        return pack((5, self.r), (10, self.levs), (12, self.offs))

    def __repr__(self):
        return '%s %s %d:%d' % (self.op, self.reg, self.levs, self.offs)

class OP_RRRR(OP):
    def __init__(self, op, r1, r2, r3, r4):
        OP.__init__(self, op)
        self.r1 = lookup_reg(r1)
        self.r2 = lookup_reg(r2)
        self.r3 = lookup_reg(r3)
        self.r4 = lookup_reg(r4)

    def get_body(self, labels, datums):
        return pack((5, self.r1), (5, self.r2), (5, self.r3), (5, self.r4))
