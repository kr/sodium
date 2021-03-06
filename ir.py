from typ import S, Integer, String, Decimal, InlineMethEntry
#from util import traced

py_list = list

all_readonly_regs = (
    S('nil'),
    S('global'),
)

all_writable_regs = (
    S('proc'),
    S('val'),
    S('argl'),
    S('continue'),
    S('addr'),
    S('env'),
    S('tmp'),
    S('vm0'),
    S('void'),
    S('free'),
)

all_regs = all_readonly_regs + all_writable_regs
all_regs_dict = dict([(str(k),i) for i,k in enumerate(all_regs)])

ops = {}

def reg_instr(callable):
    callable.name = S(callable.__name__.replace('_', '-'))
    ops[callable.name] = callable
    return callable

def desc_datum(x):
    return '%s: %s' % (type(x).__name__, x)

def quote(s):
    r = ''
    for c in s:
        if c in '\\"':
            c = '\\' + c
        if c == '\n':
            c = '\\n'
        r += c
    return '"' + r + '"'

def referencable_from_code(x):
    return (isinstance(x, S) or
            isinstance(x, Integer) or
            isinstance(x, String) or
            isinstance(x, InlineMethEntry) or
            isinstance(x, Decimal))

def tr(s, old, new):
    for a,b in zip(old, new):
        s = s.replace(a, b)
    return s

# snarfed from lx.py
def self_evaluatingp(exp):
    return (isinstance(exp, Integer) or
            isinstance(exp, String) or
            isinstance(exp, Decimal) or
            isinstance(exp, InlineMethEntry))
def nullp(l): return len(l) == 0
def car(t):
    if isinstance(t, (tuple, py_list)): return t[0]
    return t.car()
def cdr(t):
    if isinstance(t, (tuple, py_list)): return t[1:]
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

def suitable_register_list(x):
    return isinstance(x, tuple) or isinstance(x, frozenset)

DATUM_FORMAT_RECORD = 1
DATUM_FORMAT_BACKPTR = 5
DATUM_FORMAT_EMB_OPAQUE = 7
DATUM_FORMAT_OPAQUE = 15

def make_desc(format, len):
    if not (format & 1): raise 'bad format'
    return pad(32, (28, len), (4, format))

def encode_str(s):
    head = (
            backptr(),
            encoded(make_desc(DATUM_FORMAT_EMB_OPAQUE, len(s)), comment='descriptor'),
            PACKED('(size_t) str_mtab'),
           )
    padded = s + '\0' * (4 - len(s) % 4)
    groups = [''.join(reversed(xs)) for xs in zip(*[iter(padded)]*4)]
    body = ((pad(32, *[(8, ord(c)) for c in g]), repr(g)) for g in groups)
    return (head + tuple((encoded(x, comment=c) for x,c in body)), 3)

def encode_ime(ime):
    return ((
            backptr(),
            encoded(make_desc(DATUM_FORMAT_EMB_OPAQUE, 4), comment='descriptor'),
            PACKED('(size_t) ime_mtab'),
            PACKED('((uint) %s)' % (ime.name,)),
           ), 3)

def encode_datum(datum):
    if isinstance(datum, String): return encode_str(datum)
    if isinstance(datum, InlineMethEntry): return encode_ime(datum)
    return ((), 0)

def flatten(x, types=(py_list, tuple)):
    t = type(x)
    x = py_list(x)
    i = 0
    while i < len(x):
        if isinstance(x[i], types):
            x[i:i + 1] = x[i]
        else:
            i += 1
    return t(x)

the_labels = None
class make_ir_seq:
    def __init__(self, needs, modifies, *statements):
        if not suitable_register_list(needs):
            raise RuntimeError('unsuitable needs: %r' % needs)
        if not suitable_register_list(modifies):
            raise RuntimeError('unsuitable modifies: %r' % modifies)
        self.needs = frozenset(needs)
        self.modifies = frozenset(modifies)
        self.statements = flatten(statements)
        self.c_defs = ()

    def __repr__(self):
        return '<IR %r %r %r>' % (self.needs, self.modifies, self.statements)

    def add_c_defs(self, *c_defs):
        self.c_defs += c_defs

    def format(self):
        s = 'needs:%r modifies:%r\n' % (py_list(self.needs), py_list(self.modifies))
        for stmt in self.statements:
            if not symbolp(stmt): s += '  '
            s += str(stmt) + '\n'
        return s

    def extract(self):
        global the_labels
        datums = self.get_se_datums_and_symbols()
        labels, real_instrs, symbol_offsets = self.get_labels(datums)
        the_labels = labels
        return datums, labels, real_instrs, symbol_offsets

    def find_mtab_offset(self, name, insts):
        for i, inst in enumerate(insts):
            tag = getattr(inst, 'tag', None)
            if tag is name: return i
        return None

    def gen_h(self, name, fd):
        datums, labels, real_instrs, symbol_offsets = self.extract()
        cname = tr(name, '/-.', '___')

        print >>fd, '/* This file is automatically generated. */'
        print >>fd, '#ifndef %s_na_h' % cname
        print >>fd, '#define %s_na_h' % cname

        print >>fd, '#include "lxc.h"'
        print >>fd, 'extern struct lxc_module lxc_module_%s;' % (cname,)
        print >>fd, 'extern uint %s_instr_array[];' % (cname,)
        print >>fd, '#define %s_instrs (%s_instr_array + 2)' % (cname, cname)

        mtab_offset = self.find_mtab_offset(S(cname), real_instrs)
        if mtab_offset:
            print >>fd, ('#define %s_mtab (%s_instrs + %s)' %
                    (cname, cname, mtab_offset))

        print >>fd, '#endif /* %s_na_h */' % cname

    def gen_c(self, name, fd):
        datums, labels, real_instrs, symbol_offsets = self.extract()
        cname = tr(name, '/-.', '___')

        print >>fd, '/* This file is automatically generated. */'
        print >>fd
        print >>fd, '#define _GNU_SOURCE'
        print >>fd, '#include "symbol.h"'
        print >>fd, '#include "vm.h"'
        print >>fd, '#include "module.na.h"'
        print >>fd, '#include "str.na.h"'
        print >>fd, '#include "%s.na.h"' % name
        print >>fd

        # inline code
        for c_def in self.c_defs:
            print >>fd, c_def

        # instrs
        instrs_desc = make_desc(DATUM_FORMAT_OPAQUE, len(real_instrs) * 4)
        print >>fd, 'uint %s_instr_array[] = {' % (cname,)
        print >>fd, '    0x%x, /* %r */' % (instrs_desc, 'desc')
        print >>fd, '    (size_t) module_mtab,'
        for c, s in enumerate(real_instrs):
            if symbolp(s): continue
            for l,k in the_labels:
                if c == k: print >>fd, '    /* %s */' % (l,)
            ver = getattr(s, 'verify', None)
            if ver: ver(real_instrs, labels)
            packed = s.pack(c, labels, datums)
            print >>fd, '    %s, /* %r */' % (packed, s)
        print >>fd, '};'

        # sym offsets
        print >>fd, 'static uint sym_offsets[] = {'
        for i in symbol_offsets:
            print >>fd, '    %d,' % (i,)
        print >>fd, '    0,' # indicate the end of the list
        print >>fd, '};'

        print >>fd
        print >>fd, 'struct lxc_module lxc_module_%s = {' % (cname,)
        print >>fd, '    "%s",' % (name,)
        print >>fd, '    %s_instrs,' % (cname,)
        print >>fd, '    sym_offsets,'
        print >>fd, '};'

    def assemble(self, fd):
        datums, labels, real_instrs, symbol_offsets = self.extract()

        self.emit_magic(fd)
        self.emit_instructions(fd, labels, datums, real_instrs)
        self.emit_str_offsets(fd, datums)
        self.emit_intarray(fd, symbol_offsets)
        #self.list_instructions() # for debugging

    @staticmethod
    def emit_magic(fd):
        fd.write("\x89LX1\x0d\n\x1a\n")

    @staticmethod
    def emit_str_offsets(fd, datums):
        str_offsets = [x for x in datums if isinstance(x, String)]
        fd.write(encode_int(len(str_offsets)))
        for d in str_offsets:
            fd.write(encode_int(d.off))

    @staticmethod
    def emit_intarray(fd, ints):
        fd.write(encode_int(len(ints)))
        for x in ints:
            fd.write(encode_int(x))

    def get_se_datums_and_symbols(self):
        datums = ()
        for s in self.statements:
            if symbolp(s): continue
            datums += s.se_datums_and_symbols()
        return sorted(tuple(set(datums)), key=desc_datum)

    def get_labels(self, datums):
        labels, real_instrs, symbol_offsets = [], [], []
        i = 0
        for s in self.statements:
            if symbolp(s):
                #real_instrs.append(backptr())
                #i += 1
                labels.append((s, i))
            else:
                real_instrs.append(s)
                if isinstance(s, OP_DATUM_OFFSET):
                    if symbolp(s.d):
                        symbol_offsets.append(i)
                i += 1 # only count real statements
                if s.op in (la.name, bf.name, bprim.name, j.name,):
                    real_instrs.append(OP_LABEL_OFFSET(s.l))
                    i += 1
                if s.op in (closure_method.name, set_.name, define.name, lookup.name):
                    d = s.d
                    if symbolp(d): d = String(d.s)
                    real_instrs.append(OP_DATUM_OFFSET(d))
                    symbol_offsets.append(i)
                    i += 1
                if s.op in (closure_method2.name,):
                    d1, d2 = s.d1, s.d2
                    if symbolp(d1): d1 = String(d1.s)
                    if symbolp(d2): d2 = String(d2.s)
                    real_instrs.append(OP_DATUM_OFFSET(d1))
                    symbol_offsets.append(i)
                    i += 1
                    real_instrs.append(OP_DATUM_OFFSET(d2))
                    symbol_offsets.append(i)
                    i += 1
                if s.op in (load_imm.name,):
                    if isinstance(s.d, Decimal):
                        real_instrs.append(DATUM(Integer(int(s.d))))
                    elif isinstance(s.d, Integer):
                        real_instrs.append(DATUM(s.d))
                    elif symbolp(s.d):
                        real_instrs.append(OP_DATUM_OFFSET(String(s.d.s)))
                        symbol_offsets.append(i)
                    elif isinstance(s.d, str):
                        real_instrs.append(PACKED(s.d))
                    else:
                        raise "unsupported data type: %s %r" % (type(s.d), s.d)
                    i += 1

        for x in datums:
            more, off = encode_datum(x)
            if more:
              x.off = i + off
              real_instrs.extend(more)
              i += len(more)
        return labels, real_instrs, symbol_offsets

    def list_instructions(self):
        for i, s in enumerate(self.statements):
            for l,k in the_labels:
                if i == k: print l
            print '    %r' % (s,)

    def emit_instructions(self, fd, labels, datums, real_instrs):
        fd.write(encode_int(len(real_instrs)))
        for i, s in enumerate(real_instrs):
            if symbolp(s): continue
            s.emit(fd, i, labels, datums)

def make_ir_seq_with_c_defs(needs, modifies, statements, c_defs):
    ir = make_ir_seq(needs, modifies, *statements)
    ir.add_c_defs(*c_defs)
    return ir

def empty_instruction_seq():
    return make_ir_seq((), ())

def append_ir_seqs(*seqs):
    def append_2_sequences(s1, s2):
        return make_ir_seq_with_c_defs(
            registers_needed(s1) | (registers_needed(s2) -
                                    registers_modified(s1)),
            registers_modified(s1) | registers_modified(s2),
            statements(s1) + statements(s2),
            c_defs(s1) + c_defs(s2))
    def append_seq_list(seqs):
        if nullp(seqs): return empty_instruction_seq()
        return append_2_sequences(car(seqs),
                                  append_seq_list(cdr(seqs)))
    return append_seq_list(seqs)

void_s = S('void')
def preserving(regs, s1, s2, pop_all_symbol):
    def add_pops(ss):
        nss = []
        for stmt in ss:
            nss.append(stmt)
            if stmt is pop_all_symbol:
                nss.append(pop(void_s))
        return tuple(nss)
    for reg in regs:
        if needs_registerp(s2, reg) and modifies_registerp(s1, reg):
            s1 = make_ir_seq_with_c_defs(
                    frozenset([reg]) | registers_needed(s1),
                    registers_modified(s1) - frozenset([reg]),
                    (push(reg),) + add_pops(statements(s1)) + (pop(reg),),
                    c_defs(s1))
    return append_ir_seqs(s1, s2)

def tack_on_ir_seq(seq, body):
    return make_ir_seq_with_c_defs(
            registers_needed(seq),
            registers_modified(seq),
            statements(seq) + statements(body),
            c_defs(seq) + c_defs(body))

def parallel_ir_seqs(s1, s2):
    return make_ir_seq_with_c_defs(
            registers_needed(s1) | registers_needed(s2),
            registers_modified(s1) | registers_modified(s2),
            statements(s1) + statements(s2),
            c_defs(s1) + c_defs(s2))

def registers_needed(s):
    if symbolp(s): return frozenset()
    return s.needs

def registers_modified(s):
    if symbolp(s): return frozenset()
    return s.modifies

def statements(s):
    if symbolp(s): return (s,)
    return s.statements

def c_defs(s):
    if symbolp(s): return ()
    return s.c_defs

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
@reg_instr
def nop(): return OP_NOP()

# The datum pseudo-instruction
def DATUM(d, tag=None):
    if isinstance(d, Integer):
      op = 0
      if d < 0:
          op = 0x1f
          d = (1 << 26) + d
      return encoded(pad(32, (5, op), (26, d), (1, 1)), tag=tag)
    if not symbolp(d): raise 'oops'
    return OP_DATUM_OFFSET(d, tag=tag)

# The addr pseudo-instruction
def ADDR(l):
    if isinstance(l, InlineMethEntry): return OP_DATUM_OFFSET(l)
    return OP_LABEL_OFFSET(l)

# The backptr pseudo-instruction
@reg_instr
def backptr(): return OP_BACKPTR()

# The encoded pseudo-instruction
@reg_instr
def encoded(*args, **kwargs): return OP_ENCODED(*args, **kwargs)

# No-payload instructions

@reg_instr
def quit(): return OP_Z(quit.name)

# One register instructions

@reg_instr
def jr(target_reg): return OP_R(jr.name, target_reg)

@reg_instr
def push(reg): return OP_R(push.name, reg)

@reg_instr
def pop(reg): return OP_R(pop.name, reg)

# One label instructions

@reg_instr
def j(target_label): return OP_L(j.name, target_label)

# Two register instructions

@reg_instr
def mov(target_reg, src_reg): return OP_RR(mov.name, target_reg, src_reg)

@reg_instr
def closure_env(target_reg, proc_reg):
    return lw(target_reg, proc_reg, 0)

@reg_instr
def list(target_reg, val_reg):
    return cons(target_reg, val_reg, S('nil'))

@reg_instr
def make_selfobj(target_reg, label_reg):
    return OP_RR(make_selfobj.name, target_reg, label_reg)

@reg_instr
def load_off(target_reg, val):
    if not isinstance(val, String):
        raise CompileError(val, 'not a string')
    return OP_RO(load_off.name, target_reg, val)

# One register, one label instructions

@reg_instr
def la(target_reg, label): return OP_RL(la.name, target_reg, label)

@reg_instr
def bf(reg, label): return OP_RL(bf.name, reg, label)

@reg_instr
def bprim(reg, label): return OP_RL(bprim.name, reg, label)

# One register, one value instructions

@reg_instr
def load_imm(target_reg, val):
    if isinstance(val, String): return load_off(target_reg, val)
    if not referencable_from_code(val):
      raise AssemblingError('val cannot be referenced from code: %r' % val)
    return OP_RD(load_imm.name, target_reg, val)

@reg_instr
def load_cint(target_reg, name):
    return OP_RD(load_imm.name, target_reg, '((%s) << 1) | 1' % name)

# Three register instructions

@reg_instr
def cons(target_reg, car_reg, cdr_reg):
    return OP_RRR(cons.name, target_reg, car_reg, cdr_reg)
@reg_instr
def make_closure(target_reg, env_reg, label_reg):
    return OP_RRR(make_closure.name, target_reg, env_reg, label_reg)


# Two register, one immediate instructions
@reg_instr
def lw(target_reg, address_reg, imm):
    return OP_RRI(lw.name, target_reg, address_reg, imm)
@reg_instr
def sw(value_reg, address_reg, imm):
    return OP_RRI(sw.name, value_reg, address_reg, imm)
@reg_instr
def addi(target_reg, imm):
    return OP_RRI(addi.name, target_reg, S('nil'), imm)
@reg_instr
def si(address_reg, imm):
    return OP_RRI(si.name, S('nil'), address_reg, imm)


# Two register, one symbol instructions

@reg_instr
def closure_method(target_reg, obj_reg, name):
    if not symbolp(name): return closure_method2(target_reg, obj_reg, *name)
    return OP_RRD(closure_method.name, target_reg, obj_reg, name)
@reg_instr
def set_(env_reg, val_reg, name):
    return OP_RRD(set_.name, env_reg, val_reg, name)
@reg_instr
def define(env_reg, val_reg, name):
    return OP_RRD(define.name, env_reg, val_reg, name)
@reg_instr
def lookup(target_reg, env_reg, name):
    return OP_RRD(lookup.name, target_reg, env_reg, name)


@reg_instr
def closure_method2(target_reg, obj_reg, name1, name2):
    return OP_RRDD(closure_method2.name, target_reg, obj_reg, name1, name2)

# Two register, two integer instructions

@reg_instr
def lexical_lookup(target_reg, addr):
    # env_r is implied
    return OP_RII(lexical_lookup.name, target_reg, addr[0], addr[1])
@reg_instr
def lexical_setbang(val_reg, addr):
    # env_r is implied
    return OP_RII(lexical_setbang.name, val_reg, addr[0], addr[1])
@reg_instr
def lexical_lookup_tail(target_reg, addr):
    # env_r is implied
    return OP_RII(lexical_lookup_tail.name, target_reg, addr[0], addr[1])
@reg_instr
def lexical_setbang_tail(val_reg, addr):
    # env_r is implied
    return OP_RII(lexical_setbang_tail.name, val_reg, addr[0], addr[1])

# Three register, one list instructions

@reg_instr
def extend_environment(target_reg, env_reg, argl_reg, formals_r):
    return OP_RRRR(extend_environment.name, target_reg, env_reg, argl_reg, formals_r)
@reg_instr
def apply_prim_meth(target_reg, proc_reg, mess_reg, argl_reg):
    return OP_RRRR(apply_prim_meth.name, target_reg, proc_reg, mess_reg, argl_reg)

hardware_op_order = (
    nop.name,
    lw.name,
    sw.name,
    jr.name,
    push.name,
    pop.name,
    quit.name,
    j.name,
    mov.name,
    'unused1',
    addi.name,
    la.name,
    bf.name,
    bprim.name,
    load_imm.name,
    cons.name,
    apply_prim_meth.name,
    make_closure.name,
    closure_method.name,
    set_.name,
    load_off.name,
    define.name,
    lookup.name,
    lexical_lookup.name,
    lexical_setbang.name,
    extend_environment.name,
    make_selfobj.name,
    closure_method2.name,
    lexical_lookup_tail.name,
    lexical_setbang_tail.name,
    si.name,
)
op_codes = dict([(k,i) for i,k in enumerate(hardware_op_order)])

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
    def __init__(self, op, tag=None):
        self.op = op
        self.tag = tag

    def __repr__(self):
        return repr(self.op)

    def pack(self, index, labels, datums):
        return '0x%x' % self.encode(index, labels, datums)

    def encode(self, index, labels, datums):
        body = self.get_body(index, labels, datums)
        inst = pad(32, (5, lookup_op(self.op)), body)
        return inst

    def emit(self, fd, index, labels, datums):
        inst = self.encode(index, labels, datums)
        fd.write(encode_int(inst))

    def se_datums_and_symbols(self):
        return ()

def lookup_op(op):
    return op_codes[op]

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

class OP_NOP(OP):
    def __init__(self):
        OP.__init__(self, nop.name)

    def get_body(self, index, labels, datums):
        return pack((0, 0))

class OP_BACKPTR(OP):
    def __init__(self):
        OP.__init__(self, backptr.name)

    def encode(self, index, labels, datums):
        return make_desc(DATUM_FORMAT_BACKPTR, index + 1)

    def __repr__(self):
        return '<Backpointer>'

class OP_ENCODED(OP):
    def __init__(self, x, comment=None, tag=None):
        OP.__init__(self, encoded.name, tag=tag)
        if x < 0: raise 'woah'
        self.encoded = x
        self.comment = comment

    def encode(self, index, labels, datums):
        return self.encoded

    def __repr__(self):
        comment = ''
        if self.comment: comment = ' ' + self.comment
        return '<Encoded 0x%x>%s' % (self.encoded, comment)

packed_op_s = S('PACKED')
class PACKED(OP):
    def __init__(self, x, comment=None):
        OP.__init__(self, packed_op_s)
        self.packed = x
        self.comment = comment

    def encode(self, index, labels, datums):
        return pad(32)

    def pack(self, index, labels, datums):
        return self.packed

    def __repr__(self):
        comment = ''
        if self.comment: comment = ' ' + self.comment
        return '<Packed %s>%s' % (self.packed, comment)

class OP_Z(OP):
    def __init__(self, op):
        OP.__init__(self, op)

    def get_body(self, index, labels, datums):
        return pack((0, 0))

class OP_R(OP):
    def __init__(self, op, r):
        OP.__init__(self, op)
        self.reg = r
        self.r = lookup_reg(r)

    def get_body(self, index, labels, datums):
        return pack((5, self.r))

    def __repr__(self):
        return '%s %s' % (self.op, self.reg)

class OP_L(OP):
    def __init__(self, op, l):
        OP.__init__(self, op)
        self.l = l

    def get_body(self, index, labels, datums):
        i = lookup_lab(self.l, labels)
        return pack((27, i))

    def __repr__(self):
        return '%s %s' % (self.op, lab_repr(self.l))

class OP_LABEL_OFFSET(OP):
    def __init__(self, label):
        OP.__init__(self, nop.name)
        self.l = label

    def get_body(self, index, labels, datums):
        for s, i in labels:
          if s is self.l:
            addr = i
            break
        else:
          raise KeyError, self.l
        if (addr - index >= 0x08048000): raise 'bleh'
        return pack((26, addr - index), (1, 1))

    def __repr__(self):
        return 'OFFSET %s' % (lab_repr(self.l),)

class OP_DATUM_OFFSET(OP):
    def __init__(self, datum, tag=None):
        OP.__init__(self, nop.name, tag=tag)
        self.d = datum

    def get_body(self, index, labels, datums):
        def find(x):
          for d in datums:
            if d == x:
              return d.off
          raise KeyError, self.d
        d = self.d
        if symbolp(d): d = String(d.s)
        addr = find(d)
        return pack((26, addr - index), (1, 1))

    def se_datums_and_symbols(self):
        if symbolp(self.d): return self.d, String(self.d.s)
        return (self.d,)

    def __repr__(self):
        return 'OFFSET for %s' % (self.d,)

class OP_RR(OP):
    def __init__(self, op, r1, r2):
        OP.__init__(self, op)
        self.r1n = r1
        self.r2n = r2
        self.r1 = lookup_reg(r1)
        self.r2 = lookup_reg(r2)

    def get_body(self, index, labels, datums):
        return pack((5, self.r1), (5, self.r2))

    def __repr__(self):
        return '%s %s %s' % (self.op, self.r1n, self.r2n)

class OP_RL(OP):
    def __init__(self, op, r, l):
        OP.__init__(self, op)
        self.reg = r
        self.r = lookup_reg(r)
        self.l = l
        if l is S('quit'): raise 'aaa'

    def verify(self, instrs, labels):
        if self.op is la.name:
          for name, offs in labels:
            if name is self.l: break
          if instrs[offs - 1].op is not backptr.name:
            raise RuntimeError('error: continue label at %d not preceded by backptr: %s' % (offs, self.l))

    def get_body(self, index, labels, datums):
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

    def get_body(self, index, labels, datums):
        return pack((5, self.r), (22, 0))

    def se_datums_and_symbols(self):
        if symbolp(self.d): return self.d, String(self.d.s)
        if isinstance(self.d, str): return ()
        return (self.d,)

    def __repr__(self):
        return '%s %s %r' % (self.op, self.reg, self.d)

class OP_RO(OP):
    def __init__(self, op, r, d):
        OP.__init__(self, op)
        self.reg = r
        self.r = lookup_reg(r)
        if not isinstance(d, String):
            raise AssemblingError('d cannot be referenced by an offset: %r' % d)
        self.d = d

    def get_body(self, index, labels, datums):
        def find():
          for d in datums:
            if d == self.d:
              return d.off
          raise KeyError, self.d
        addr = find()
        return pack((5, self.r), (22, addr - index))

    def se_datums_and_symbols(self):
        if symbolp(self.d): return self.d, String(self.d.s)
        return (self.d,)

    def __repr__(self):
        return '%s %s %r' % (self.op, self.reg, self.d)

class OP_RRR(OP):
    def __init__(self, op, r1, r2, r3):
        OP.__init__(self, op)
        self.r1n = r1
        self.r2n = r2
        self.r3n = r3
        self.r1 = lookup_reg(r1)
        self.r2 = lookup_reg(r2)
        self.r3 = lookup_reg(r3)

    def get_body(self, index, labels, datums):
        return pack((5, self.r1), (5, self.r2), (5, self.r3))

    def __repr__(self):
        return '%s %s %s %s' % (self.op, self.r1n, self.r2n, self.r3n)

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

    def get_body(self, index, labels, datums):
        i = lookup_dat(self.d, datums)
        return pack((5, self.r1), (5, self.r2), (17, i))

    def se_datums_and_symbols(self):
        if symbolp(self.d): return self.d, String(self.d.s)
        return (self.d,)

    def __repr__(self):
        return '%s %s %s %r' % (self.op, self.reg1, self.reg2, self.d)

class OP_RRI(OP):
    def __init__(self, op, r1, r2, imm):
        OP.__init__(self, op)
        self.reg1 = r1
        self.reg2 = r2
        self.r1 = lookup_reg(r1)
        self.r2 = lookup_reg(r2)
        self.imm = imm

    def get_body(self, index, labels, datums):
        return pack((5, self.r1), (5, self.r2), (17, self.imm))

    def __repr__(self):
        return '%s %s %s %d' % (self.op, self.reg1, self.reg2, self.imm)

class OP_RRDD(OP):
    def __init__(self, op, r1, r2, d1, d2):
        OP.__init__(self, op)
        self.reg1 = r1
        self.reg2 = r2
        self.r1 = lookup_reg(r1)
        self.r2 = lookup_reg(r2)
        if not isinstance(d1, S):
            raise AssemblingError, 'd1 must be a symbol: %r' % d1
        if not isinstance(d2, S):
            raise AssemblingError, 'd2 must be a symbol: %r' % d2
        self.d1 = d1
        self.d2 = d2

    def get_body(self, index, labels, datums):
        return pack((5, self.r1), (5, self.r2), (17, 0))

    def se_datums_and_symbols(self):
        return self.d1, self.d2, String(self.d1.s), String(self.d2.s)

    def __repr__(self):
        return '%s %s %s %r %r' % (self.op, self.reg1, self.reg2,
                self.d1, self.d2)

class OP_RII(OP):
    def __init__(self, op, r, levs, offs):
        OP.__init__(self, op)
        self.reg = r
        self.r = lookup_reg(r)
        self.levs = levs
        self.offs = offs

    def get_body(self, index, labels, datums):
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

    def get_body(self, index, labels, datums):
        return pack((5, self.r1), (5, self.r2), (5, self.r3), (5, self.r4))
