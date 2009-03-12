from typ import S, Integer, String, Decimal, InlineMethEntry
#from util import traced

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
    S('void'),
)

all_regs = all_readonly_regs + all_writable_regs
all_regs_dict = dict([(str(k),i) for i,k in enumerate(all_regs)])

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

def suitable_register_list(x):
    return isinstance(x, tuple) or isinstance(x, frozenset)

def make_desc(format, len):
    if not (format & 1): raise 'bad format'
    return pad(32, (28, len), (4, format))

def encode_str(s):
    head = (
            BACKPTR(),
            ENCODED(make_desc(7, len(s)), comment='descriptor'),
            ENCODED(0, comment='str mtab'),
           )
    padded = s + '\0' * (4 - len(s) % 4)
    groups = [''.join(xs) for xs in zip(*[iter(padded)]*4)]
    body = ((pad(32, *[(8, ord(c)) for c in g]), repr(g)) for g in groups)
    return (head + tuple((ENCODED(x, comment=c) for x,c in body)), 3)

def encode_ime(ime):
    return ((
            BACKPTR(),
            ENCODED(make_desc(7, 4), comment='descriptor'),
            ENCODED(0, comment='ime mtab'),
            PACKED('((uint) %s)' % (ime.name,)),
           ), 3)

def encode_datum(datum):
    if isinstance(datum, String): return encode_str(datum)
    if isinstance(datum, InlineMethEntry): return encode_ime(datum)
    return ((), 0)

the_labels = None
class make_ir_seq:
    def __init__(self, needs, modifies, *statements):
        if not suitable_register_list(needs):
            raise RuntimeError('unsuitable needs: %r' % needs)
        if not suitable_register_list(modifies):
            raise RuntimeError('unsuitable modifies: %r' % modifies)
        self.needs = frozenset(needs)
        self.modifies = frozenset(modifies)
        self.statements = statements
        self.c_defs = ()

    def __repr__(self):
        return '<IR %r %r %r>' % (self.needs, self.modifies, self.statements)

    def add_c_defs(self, *c_defs):
        self.c_defs += c_defs

    def format(self):
        s = 'needs:%r modifies:%r\n' % (list(self.needs), list(self.modifies))
        for stmt in self.statements:
            if not symbolp(stmt): s += '  '
            s += str(stmt) + '\n'
        return s

    def extract(self):
        global the_labels
        datums = self.get_se_datums_and_symbols()
        labels, real_instrs = self.get_labels(datums)
        the_labels = labels
        return datums, labels, real_instrs

    def find_mtab_offset(self, name, insts):
        for i, inst in enumerate(insts):
            tag = getattr(inst, 'tag', None)
            if tag is name: return i
        return None

    def gen_c(self, name, fd, mtab=None):
        datums, labels, real_instrs = self.extract()
        cname = tr(name, '/-.', '___')

        print >>fd, '/* This file is automatically generated. */'
        print >>fd
        print >>fd, '#include "symbol.h"'
        print >>fd, '#include "vm.h"'
        print >>fd, '#include "lxc.h"'
        print >>fd
        print >>fd, 'extern struct lxc_module lxc_module_%s;' % (cname,)
        print >>fd, 'static uint instr_array[];'
        print >>fd, '#define instrs (instr_array + 2)'

        mtab_offset = None
        if mtab: mtab_offset = self.find_mtab_offset(S(mtab), real_instrs)
        if mtab_offset:
            print >>fd, '#define mtab (instrs + %s)' % mtab_offset

        # inline code
        for c_def in self.c_defs:
            print >>fd, c_def

        # datums
        print >>fd
        sdts = ''
        datum_names = []
        for i, d in enumerate(datums):
            c_name = make_datum_c_name(i)
            datum_names.append(c_name)

            if symbolp(d):
                sdts += '$'
                print >>fd, '#define %s "%s" /* %s */' % (c_name, str(d), desc_datum(d))
            elif isinstance(d, String):
                sdts += '@'
                print >>fd, '#define %s %s /* %s */' % (c_name, quote(str(d)), desc_datum(d))
            elif isinstance(d, InlineMethEntry):
                sdts += '>'
                print >>fd, '#define %s %s /* %s */' % (c_name, str(d), desc_datum(d))
            elif self_evaluatingp(d):
                sdts += '#'
                print >>fd, '#define %s %d /* %s */' % (c_name,
                        pseudo_box(int(d)), desc_datum(d))
            else:
                raise RuntimeError('wtf %r' % d)

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
        instrs_desc = make_desc(15, len(real_instrs) * 4)
        print >>fd, 'static uint instr_array[] = {'
        print >>fd, '    0x%x, /* %r */' % (instrs_desc, 'desc')
        print >>fd, '    0x%x, /* %r */' % (0, 'mtab')
        for c, s in enumerate(real_instrs):
            if symbolp(s): continue
            for l,k in the_labels:
                if c == k: print >>fd, '    /* %s */' % (l,)
            packed = s.pack(c, labels, datums)
            print >>fd, '    %s, /* %r */' % (packed, s)
        print >>fd, '};'

        # str offsets
        print >>fd, 'static uint str_offsets[] = {'
        for d in datums:
            if isinstance(d, String):
                print >>fd, '    %d,' % (d.off,)
        print >>fd, '    0,' # indicate the end of the list
        print >>fd, '};'

        # ime offsets
        print >>fd, 'static uint ime_offsets[] = {'
        for d in datums:
            if isinstance(d, InlineMethEntry):
                print >>fd, '    %d,' % (d.off,)
        print >>fd, '    0,' # indicate the end of the list
        print >>fd, '};'

        print >>fd
        print >>fd, 'struct lxc_module lxc_module_%s = {' % (cname,)
        print >>fd, '    "%s",' % (name,)
        print >>fd, '    {'
        print >>fd, '        "%s",' % (sdts,)
        print >>fd, '        static_datum_entries,'
        print >>fd, '    },'
        print >>fd, '    %d,' % (len(datums),)
        print >>fd, '    instrs,'
        print >>fd, '    %d,' % (len(real_instrs),)
        print >>fd, '    label_offsets,'
        print >>fd, '    str_offsets,'
        print >>fd, '    ime_offsets,'
        print >>fd, '};'

    def assemble(self, fd):
        datums, labels, real_instrs = self.extract()

        self.emit_magic(fd)
        self.emit_datums(fd, datums)
        self.emit_labels(fd, labels)
        self.emit_instructions(fd, labels, datums, real_instrs)
        self.emit_str_offsets(fd, datums)
        self.emit_ime_offsets(fd, datums)
        #self.list_instructions() # for debugging

    @staticmethod
    def emit_magic(fd):
        fd.write("\x89LX1\x0d\n\x1a\n")

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
            else:
                raise RuntimeError('wtf %r' % d)

    @staticmethod
    def emit_labels(fd, labels):
        fd.write(encode_int(len(labels)))
        for s, i in labels:
            fd.write(encode_int(i))
            #fd.write(fmt_int(i, 4, ' '))

    @staticmethod
    def emit_str_offsets(fd, datums):
        fd.write(encode_int(len(datums)))
        for d in datums:
            if isinstance(d, String):
                fd.write(encode_int(d.off))

    @staticmethod
    def emit_ime_offsets(fd, datums):
        fd.write(encode_int(len(datums)))
        for d in datums:
            if isinstance(d, InlineMethEntry):
                fd.write(encode_int(d.off))

    def get_se_datums_and_symbols(self):
        datums = ()
        for s in self.statements:
            if symbolp(s): continue
            datums += s.se_datums_and_symbols()
        return sorted(tuple(set(datums)), key=desc_datum)

    def get_labels(self, datums):
        labels, real_instrs = [], []
        i = 0
        for s in self.statements:
            if symbolp(s):
                labels.append((s, i))
            else:
                real_instrs.append(s)
                i += 1 # only count real statements
                if s.op in (load_addr_s, bf_s, bprim_s, goto_label_s,):
                    real_instrs.append(OP_OFFSET(s.l))
                    i += 1
        real_instrs.append(QUIT())
        for x in datums:
            more, off = encode_datum(x)
            x.off = i + off
            real_instrs.extend(more)
            i += len(more)
        return labels, real_instrs

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
                nss.append(POP(void_s))
        return tuple(nss)
    for reg in regs:
        if needs_registerp(s2, reg) and modifies_registerp(s1, reg):
            s1 = make_ir_seq_with_c_defs(
                    frozenset([reg]) | registers_needed(s1),
                    registers_modified(s1) - frozenset([reg]),
                    (PUSH(reg),) + add_pops(statements(s1)) + (POP(reg),),
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
nop_s = S('NOP')
def NOP(): return OP_NOP()

# The datum pseudo-instruction
datum_op_s = S('DATUM')
def DATUM(d, tag=None):
    if isinstance(d, Integer):
      return ENCODED(pad(32, (5, 0), (26, d), (1, 1)), tag=tag)
    return OP_DATUM(d, tag=tag)

# The addr pseudo-instruction
addr_op_s = S('ADDR')
def ADDR(l):
    if isinstance(l, InlineMethEntry): return OP_DATUM(l)
    return OP_L(addr_op_s, l)

# The backptr pseudo-instruction
backptr_op_s = S('BACKPTR')
def BACKPTR(): return OP_BACKPTR()

# The encoded pseudo-instruction
encoded_op_s = S('ENCODED')
def ENCODED(*args, **kwargs): return OP_ENCODED(*args, **kwargs)

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
closure_env_s = S('CLOSURE_ENV')
list_s = S('LIST')
make_selfobj_s = S('MAKE_SELFOBJ')
def MOV(target_reg, src_reg): return OP_RR(mov_s, target_reg, src_reg)
def CLOSURE_ENV(target_reg, proc_reg):
    return OP_RR(closure_env_s, target_reg, proc_reg)
def LIST(target_reg, val_reg): return OP_RR(list_s, target_reg, val_reg)
def MAKE_SELFOBJ(target_reg, label_reg):
    return OP_RR(make_selfobj_s, target_reg, label_reg)


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
def LOAD_IMM(target_reg, val): return OP_RD(load_imm_s, target_reg, val)
def MAKE_ARRAY(target_reg, len): return OP_RD(make_array_s, target_reg, len)

# Three register instructions

cons_s = S('CONS')
make_closure_s = S('MAKE_CLOSURE')
def CONS(target_reg, car_reg, cdr_reg):
    return OP_RRR(cons_s, target_reg, car_reg, cdr_reg)
def MAKE_CLOSURE(target_reg, env_reg, label_reg):
    return OP_RRR(make_closure_s, target_reg, env_reg, label_reg)


# Two register, one symbol instructions

closure_method_s = S('CLOSURE_METHOD')
set__s = S('SET_')
define_s = S('DEFINE')
lookup_s = S('LOOKUP')
def CLOSURE_METHOD(target_reg, obj_reg, name):
    return OP_RRD(closure_method_s, target_reg, obj_reg, name)
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
apply_prim_meth_s = S('APPLY_PRIM_METH')
def EXTEND_ENVIRONMENT(target_reg, env_reg, argl_reg, formals_r):
    return OP_RRRR(extend_environment_s, target_reg, env_reg, argl_reg, formals_r)
def APPLY_PRIM_METH(target_reg, proc_reg, mess_reg, argl_reg):
    return OP_RRRR(apply_prim_meth_s, target_reg, proc_reg, mess_reg, argl_reg)

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
    closure_env_s,
    list_s,
    load_addr_s,
    bf_s,
    bprim_s,
    load_imm_s,
    cons_s,
    apply_prim_meth_s,
    make_closure_s,
    closure_method_s,
    set__s,
    make_array_s,
    define_s,
    lookup_s,
    lexical_lookup_s,
    lexical_setbang_s,
    extend_environment_s,
    make_selfobj_s,
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
        OP.__init__(self, nop_s)

    def get_body(self, index, labels, datums):
        return pack((0, 0))

class OP_DATUM(OP):
    def __init__(self, d, tag=None):
        OP.__init__(self, datum_op_s, tag=tag)
        if not referencable_from_code(d):
            raise AssemblingError('d cannot be referenced from code: %r' % d)
        self.d = d

    def get_body(self, index, labels, datums):
        i = lookup_dat(self.d, datums)
        return pack((27, i))

    def se_datums_and_symbols(self):
        if symbolp(self.d): return self.d, String(self.d.s)
        return (self.d,)

    def __repr__(self):
        tag = ''
        if self.tag: tag = ' tag=%s' % self.tag
        return '%s %s%s' % (self.op, self.d, tag)

class OP_BACKPTR(OP):
    def __init__(self):
        OP.__init__(self, backptr_op_s)

    def encode(self, index, labels, datums):
        return make_desc(5, index + 1)

    def __repr__(self):
        return '<Backpointer>'

class OP_ENCODED(OP):
    def __init__(self, x, comment=None, tag=None):
        OP.__init__(self, encoded_op_s, tag=tag)
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

class OP_OFFSET(OP):
    def __init__(self, label):
        OP.__init__(self, nop_s)
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
        if not referencable_from_code(d):
            raise AssemblingError('d cannot be referenced from code: %r' % d)
        self.d = d

    def get_body(self, index, labels, datums):
        i = lookup_dat(self.d, datums)
        return pack((5, self.r), (22, i))

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
