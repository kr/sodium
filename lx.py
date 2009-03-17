
import sys
import re
from string import maketrans

from typ import *
from pair import cons, nil, pairp
from pair import list as plist
from env import *
from ir import *
from util import traced, report_compile_error

import lexer
import reader

# Indicates a problem with the code to be compiled, not the compiler itself.
class CompileError(Exception):
    def __init__(self, exp, message):
        Exception.__init__(self, message)
        self.annotate_or_report(exp)

    def annotate_or_report(self, exp):
        self.context = (exp,) + getattr(self, 'context', ())
        if self.context[0:1] == self.context[1:2]:
          self.context = self.context[1:]
        while exp not in reader.current_pos_info:
          if not pairp(exp): raise self
          exp = exp.car()
        info = reader.current_pos_info[exp]
        report_compile_error(self, file=info[0], line=info[1], char=info[2])

quote_s = S('quote')
set__s = S('set!')
def_s = S('def')
load_module_s = S('module')
export_s = S('export')
import_s = S('import')
return_s = S('return')
qmark_s = S('?')
if_s = S('if')
fn_s = S('fn')
shfn_s = S(':shorthand-fn:')
obj_s = S('obj')
sobj_s = S('sobj')
do_s = S('do')
inline_s = S('inline')
assign_s = S('=')
def compile(exp, target, linkage, cenv, pop_all_symbol, **kwargs):
  try:
    if self_evaluatingp(exp):
        return compile_self_evaluating(exp, target, linkage, cenv, pop_all_symbol)
    if tagged_list(exp, quote_s): return compile_quoted(exp, target, linkage, cenv, pop_all_symbol)
    if variablep(exp): return compile_variable(exp, target, linkage, cenv, pop_all_symbol)
    if tagged_list(exp, set__s): return compile_assignment(exp, target, linkage, cenv, pop_all_symbol)
    if tagged_list(exp, def_s): return compile_definition(exp, target, linkage, cenv, pop_all_symbol)
    if tagged_list(exp, qmark_s): return compile_qmark(exp, target, linkage, cenv, pop_all_symbol)
    if tagged_list(exp, fn_s): return compile_obj(fn2obj(exp), target, linkage, cenv, pop_all_symbol)
    if tagged_list(exp, shfn_s): return compile_shfn(exp, target, linkage, cenv, pop_all_symbol)
    if tagged_list(exp, obj_s): return compile_obj(exp, target, linkage, cenv, pop_all_symbol)
    if tagged_list(exp, sobj_s): return compile_sobj(exp, target, linkage, cenv, pop_all_symbol, **kwargs)
    if tagged_list(exp, do_s): return compile_do(exp, target, linkage, cenv, pop_all_symbol)
    if tagged_list(exp, inline_s): return compile_inline(exp)
    if tagged_list(exp, return_s): return compile_return(exp, target, linkage, cenv, pop_all_symbol)
    if tagged_list2(exp, assign_s): return compile(assign2setbang(exp), target, linkage, cenv, pop_all_symbol)
    if simple_macrop(exp): return compile(expand_simple_macros(exp),
            target, linkage, cenv, pop_all_symbol)
    if pairp(exp): return compile_application(exp, target, linkage, cenv, pop_all_symbol)
    raise CompileError(exp, 'Unknown expression type')
  except CompileError, err:
    err.annotate_or_report(exp)

val_r = S('val')
def compile_return(exp, target, linkage, cenv, pop_all_symbol):
  return compile(exp.cadr(), val_r, return_s, cenv, pop_all_symbol);

def assign2setbang(exp):
  return plist(set__s, exp.car(), exp.caddr())

def compile_shfn(exp, target, linkage, cenv, pop_all_symbol):
  return compile(shfn2fn(exp), target, linkage, cenv, pop_all_symbol)

def simple_macrop(exp):
  tag = str(exp.car())
  return tag in simple_macros

def expand_simple_macro(exp):
  if not pairp(exp): return exp
  tag = str(exp.car())
  if tag not in simple_macros: return exp
  return simple_macros[tag](exp)

def expand_simple_macros(exp):
  while True:
    nexp = expand_simple_macro(exp)
    if nexp == exp: break
    exp = nexp
  return exp

def expand_seq_macros(seq):
  if seq.nullp(): return seq
  if not pairp(seq.car()): return seq
  if seq.car().nullp(): return seq
  tag = str(seq.caar())
  if tag not in seq_macros: return seq
  return seq_macros[tag](seq)

def expand_sequence(seq, cenv):
  try:
    oseq = seq
    while True:
      nseq = expand_seq_macros(seq)
      if nseq == seq: break
      seq = nseq
    #if oseq != seq:
    #  print 'old:', oseq
    #  print 'new:', seq
    if cenv is not nil:
      return scan_out_defines(seq)
    return seq

  except CompileError, err:
    err.annotate_or_report(seq)

def compile_do(exp, target, linkage, cenv, pop_all_symbol):
  return compile_sequence(exp.cdr(), target, linkage, cenv, pop_all_symbol)

env_r = S('env')
global_r = S('global')
nil_r = S('nil')
not_found_s = S('not-found')
percent_s = S('%')
def compile_self_evaluating(exp, target, linkage, cenv, pop_all_symbol):
    return end_with_linkage(linkage,
            make_ir_seq((), (target,), LOAD_IMM(target, exp)), pop_all_symbol)

def compile_literal(exp, target, linkage, cenv, pop_all_symbol):
  if exp is nil:
    return end_with_linkage(linkage,
            make_ir_seq((), (target,), MOV(target, nil_r)), pop_all_symbol)
    return compile_self_evaluating(exp, target, linkage, cenv, pop_all_symbol)
  if self_evaluatingp(exp) or symbolp(exp):
    return compile_self_evaluating(exp, target, linkage, cenv, pop_all_symbol)
  if pairp(exp):
    reg = tmp_r
    if reg is target: reg = val_r # this feels like a hack
    d = compile_literal(exp.cdr(), target, next_s, cenv, pop_all_symbol)
    a = compile_literal(exp.car(), reg, next_s, cenv, pop_all_symbol)
    return end_with_linkage(linkage,
            append_ir_seqs(
                d,
                preserving((target,),
                    a,
                    make_ir_seq((reg, target), (target,),
                        CONS(target, reg, target)),
                    pop_all_symbol)), pop_all_symbol)
  raise CompileError(exp, "Can't compile literal")

def compile_quoted(exp, target, linkage, cenv, pop_all_symbol):
    return compile_literal(exp.cadr(), target, linkage, cenv, pop_all_symbol)

def compile_variable(exp, target, linkage, cenv, pop_all_symbol):
    addr = find_variable(exp, cenv)
    if addr is not_found_s:
        binop_pat = '^' + lexer.non_name_pat + '$'
        if re.match(binop_pat, str(exp)):
            raise CompileError(exp, 'lookup of binop')
        if str(exp).startswith('.') or str(exp).startswith(':'):
            raise CompileError(exp, 'lookup of non-name')
        return end_with_linkage(linkage,
            make_ir_seq((), (target,),
                LOOKUP(target, global_r, exp)), pop_all_symbol)
    return end_with_linkage(linkage,
            make_ir_seq((env_r,), (target,),
                LEXICAL_LOOKUP(target, addr)), pop_all_symbol)

next_s = S('next')
ok_s = S('ok')
def compile_assignment(exp, target, linkage, cenv, pop_all_symbol):
    var = exp.cadr()
    get_value_code = compile(exp.caddr(), val_r, next_s, cenv, pop_all_symbol,
            tag=var)
    addr = find_variable(var, cenv)
    if var is self_s:
        raise CompileError(exp, 'cannot assign to pseudo-variable %s' % var)
    if addr is not_found_s:
        return end_with_linkage(linkage,
                preserving((), get_value_code,
                    make_ir_seq((val_r,), (target,),
                        SET_(global_r, val_r, var),
                        LOAD_IMM(target, ok_s)), pop_all_symbol), pop_all_symbol)
    return end_with_linkage(linkage,
            preserving((env_r,), get_value_code,
                make_ir_seq((env_r, val_r), (target,),
                    LEXICAL_SETBANG(val_r, addr),
                    LOAD_IMM(target, ok_s)), pop_all_symbol), pop_all_symbol)

def compile_definition(exp, target, linkage, cenv, pop_all_symbol):
    var = definition_variable(exp)
    get_value_code = compile(definition_value(exp), val_r, next_s, cenv, pop_all_symbol)
    if cenv is not nil:
        raise RuntimeError('internal error, cannot have define here: %r' % exp)
    return end_with_linkage(linkage,
            preserving((env_r,), get_value_code,
                make_ir_seq((env_r, val_r), (target,),
                    DEFINE(env_r, val_r, var),
                    LOAD_IMM(target, ok_s)), pop_all_symbol), pop_all_symbol)

def import_name(exp):
    return exp.cadr()

continue_r = S('continue')
def compile_qmark(exp, target, linkage, cenv, pop_all_symbol):
    t_branch = make_label('true-branch')
    f_branch = make_label('false-branch')
    after_if = make_label('after-if')
    #consequent_linkage = (linkage is next_s) if after_if else linkage
    consequent_linkage = linkage
    if linkage is next_s: consequent_linkage = after_if
    p_code = compile(exp.cadr(), val_r, next_s, cenv, pop_all_symbol)
    c_code = compile(exp.caddr(), target, consequent_linkage, cenv, pop_all_symbol)
    a_code = compile(if_alternative(exp), target, linkage, cenv, pop_all_symbol)
    return preserving((env_r, continue_r), p_code,
            append_ir_seqs(
                make_ir_seq((val_r,), (),
                    BF(val_r, f_branch)),
                parallel_ir_seqs(
                    append_ir_seqs(t_branch, c_code),
                    append_ir_seqs(f_branch, a_code)),
                after_if), pop_all_symbol)

def compile_sequence(seq, target, linkage, cenv, pop_all_symbol):
    if seq.nullp(): raise CompileError(seq, 'value of null sequence is undefined')
    seq = expand_sequence(seq, cenv)
    if seq.cdr().nullp(): return compile(seq.car(), target, linkage, cenv, pop_all_symbol)
    first_code = compile(seq.car(), target, next_s, cenv, pop_all_symbol)
    rest_code = compile_sequence(seq.cdr(), target, linkage, cenv, pop_all_symbol)
    return preserving((env_r, continue_r, proc_r),
        first_code,
        rest_code, pop_all_symbol)

def compile_obj(exp, target, linkage, cenv, pop_all_symbol):
    obj_table = make_label('obj-table')
    after_obj = make_label('after-obj')
    m_tabl_code = make_ir_seq((), (),
        BACKPTR(),
        obj_table,
        DATUM(exp_methods(exp).len()))
    m_body_code = empty_instruction_seq()
    for meth in exp_methods(exp):
        name = S(meth_name(meth))
        #entry = make_label('method-entry')
        entry = make_meth_entry(meth)
        m_tabl_code = tack_on_ir_seq(m_tabl_code,
                                     make_ir_seq((), (),
                                        DATUM(name),
                                        ADDR(entry)))
        m_body_code = tack_on_ir_seq(m_body_code,
                                     compile_meth_body(meth, entry, cenv))
    #meth_linkage = (linkage is next_s) if after_obj else linkage
    meth_linkage = linkage
    if linkage is next_s: meth_linkage = after_obj
    return append_ir_seqs(
        tack_on_ir_seq(
            end_with_linkage(meth_linkage,
                make_ir_seq((env_r,), (target, val_r),
                    LOAD_ADDR(val_r, obj_table),
                    MAKE_CLOSURE(target, env_r, val_r)), pop_all_symbol),
            tack_on_ir_seq(m_tabl_code, m_body_code)),
        after_obj)

self_s = S('self')
def compile_sobj(exp, target, linkage, cenv, pop_all_symbol, tag=None):
    obj_table = make_label('obj-table')
    after_obj = make_label('after-obj')
    m_tabl_code = make_ir_seq((), (),
        BACKPTR(),
        obj_table,
        DATUM(sobj_methods(exp).len(), tag=tag))
    m_body_code = empty_instruction_seq()
    for meth in sobj_methods(exp):
        name = S(meth_name(meth))
        #entry = make_label('method-entry')
        entry = make_meth_entry(meth)
        m_tabl_code = tack_on_ir_seq(m_tabl_code,
                                     make_ir_seq((), (),
                                        DATUM(name),
                                        ADDR(entry)))
        m_body_code = tack_on_ir_seq(m_body_code,
                                     compile_smeth_body(meth, entry))
    #meth_linkage = (linkage is next_s) if after_obj else linkage
    meth_linkage = linkage
    if linkage is next_s: meth_linkage = after_obj
    return append_ir_seqs(
        tack_on_ir_seq(
            end_with_linkage(meth_linkage,
                make_ir_seq((env_r,), (target, val_r),
                    LOAD_ADDR(val_r, obj_table),
                    MAKE_SELFOBJ(target, val_r)), pop_all_symbol),
            tack_on_ir_seq(m_tabl_code, m_body_code)),
        after_obj)

def exp_methods(exp):
    return exp.cdr()

def sobj_methods(exp):
    return exp.cdr()

def is_inline_meth(meth):
    return tagged_list(meth, inline_s)

def make_meth_entry(meth):
    if is_inline_meth(meth):
        return InlineMethEntry(make_label('inline_meth_entry'))
    return make_label('method-entry')

def inline_meth_name(meth):
    x = meth.caddr()
    if pairp(x): return x.car()
    return x

def inline_meth_params(meth):
    x = meth.caddr()
    if pairp(x): return x.cdr()
    return nil

def meth_name(meth):
    if is_inline_meth(meth): return inline_meth_name(meth)
    if not pairp(meth):
        raise CompileError(meth, 'method must be a list')
    if not pairp(meth.car()):
        raise CompileError(meth.car(), 'method signature must be a list')
    return meth.caar()

def meth_params(meth):
    return meth.cdar()

def meth_body(meth):
    return meth.cdr()

proc_r = S('proc')
argl_r = S('argl')
tmp_r = S('tmp')
def compile_meth_body(meth, meth_entry, cenv):
    pop_all_symbol = make_label('pop-all')
    if is_inline_meth(meth):
        return compile_inline_meth_body(meth, meth_entry, cenv)
    formals = meth_params(meth)
    body = meth_body(meth)
    cenv = cons(formals, cenv)
    return append_ir_seqs(
        make_ir_seq((proc_r,), (env_r,),
            BACKPTR(),
            meth_entry,
            CLOSURE_ENV(env_r, proc_r)),
        compile_literal(formals, tmp_r, next_s, cenv, pop_all_symbol),
        make_ir_seq((env_r, tmp_r, argl_r), (env_r,),
            EXTEND_ENVIRONMENT(env_r, env_r, argl_r, tmp_r)),
        compile_sequence(body, val_r, return_s, cenv, pop_all_symbol))

def compile_smeth_body(meth, meth_entry):
    pop_all_symbol = make_label('pop-all')
    if is_inline_meth(meth):
        return compile_inline_meth_body(meth, meth_entry, nil)
    formals = meth_params(meth)
    body = meth_body(meth)
    cenv = cons(formals, plist(plist(self_s)))
    return append_ir_seqs(
        make_ir_seq((proc_r,), (env_r,),
            BACKPTR(),
            meth_entry,
            LIST(env_r, proc_r),
            LIST(env_r, env_r)),
        compile_literal(formals, tmp_r, next_s, cenv, pop_all_symbol),
        make_ir_seq((env_r, argl_r, tmp_r), (env_r,),
            EXTEND_ENVIRONMENT(env_r, env_r, argl_r, tmp_r)),
        compile_sequence(body, val_r, return_s, cenv, pop_all_symbol))

def munge_sym_to_c(name):
    return 'n_' + str(name).translate(maketrans('-*', '__'))

def make_c_undefine(name):
    return '#undef %s /* %s */\n' % (munge_sym_to_c(name), name)

def make_lexical_c_defines(cenv):
    def make_c_define(name, i, j):
        p = munge_sym_to_c(name), i, j, name
        return (make_c_undefine(name) +
                '#define %s (lexical_lookup(closure_env(rcv), %d, %d)) /* %s */\n' % p)
    def help(i, env):
        def hhelp(j, names):
            if names.nullp(): return ''
            name = names.car()
            return make_c_define(name, i, j) + hhelp(j + 1, names.cdr())
        if env.nullp(): return ''
        return hhelp(0, env.car()) + help(i + 1, env.cdr())
    return help(0, cenv)

def make_lexical_c_undefines(cenv):
    def help(env):
        def hhelp(names):
            if names.nullp(): return ''
            name = names.car()
            return make_c_undefine(name) + hhelp(names.cdr())
        if env.nullp(): return ''
        return hhelp(env.car()) + help(env.cdr())
    return help(cenv)

def make_param_c_defines(params):
    def carcode(c): return 'car(%s)' % (c,)
    def cdrcode(c): return 'cdr(%s)' % (c,)
    def make(code, name):
        return (make_c_undefine(name) +
                '#define %s (%s) /* %s */\n' % (munge_sym_to_c(name), code, name))
    def help(code, params):
        if params.nullp(): return ''
        return (make(carcode(code), params.car()) +
                help(cdrcode(code), params.cdr()))
    return help('args', params)

def make_param_c_undefines(params):
    if params.nullp(): return ''
    return make_c_undefine(params.car()) + make_param_c_undefines(params.cdr())

def compile_inline_meth_body(meth, entry, cenv):
    c_def = '''static datum
%(name)s(datum rcv, datum args)
{
%(defines)s
#line %(line)d "%(file)s"
%(body)s
%(undefines)s
}
'''
    body = meth.cadddr()
    params = inline_meth_params(meth)
    defines = make_lexical_c_defines(cenv) + make_param_c_defines(params)
    undefines = make_lexical_c_undefines(cenv) + make_param_c_undefines(params)
    c_def %= { 'name':str(entry), 'body':body, 'file':body.pos[0],
               'line':body.pos[1], 'defines':defines, 'undefines':undefines }
    seq = empty_instruction_seq()
    seq.add_c_defs(c_def)
    return seq

def compile_inline(exp):
    seq = empty_instruction_seq()
    seq.add_c_defs(exp.caddr())
    return seq

def compile_application(exp, target, linkage, cenv, pop_all_symbol):
    proc_code = compile(exp_object(exp), proc_r, next_s, cenv, pop_all_symbol)
    message = exp_message(exp)
    operand_codes = exp_operands(exp).map(lambda operand: compile(operand, val_r, next_s, cenv, pop_all_symbol))
    return preserving((env_r, continue_r),
                      proc_code,
                      preserving((proc_r, continue_r),
                                 construct_arglist(operand_codes, pop_all_symbol),
                                 compile_procedure_call(target, linkage, cenv, message, pop_all_symbol), pop_all_symbol), pop_all_symbol)

def message_send(exp):
    return call_app(exp) or send_app(exp)

def messp(x):
    if not symbolp(x): return False
    return ((str(x)[0] in ('.', ':')) or
            re.match(lexer.non_name_pat + '$', str(x)))

def call_app(exp):
    if exp.len() < 2: return False
    m = exp.cadr()
    return messp(m) and (str(m)[0] != ':')

def send_app(exp):
    if exp.len() < 2: return False
    m = exp.cadr()
    return messp(m) and (str(m)[0] == ':')

send_s = S('send')
def exp_object(exp):
    if not message_send(exp): return exp.car()
    if call_app(exp): return exp.car()
    if send_app(exp): return send_s

def extract_message(token):
    if token[0] in (':', '.'): return S(token[1:])
    return token

run_s = S('run')
def exp_message(exp):
    if not message_send(exp): return run_s
    if call_app(exp): return extract_message(exp.cadr())
    if send_app(exp): return run_s

def exp_operands(exp):
    if not message_send(exp): return exp.cdr()
    if call_app(exp): return exp.cddr()
    msg = plist(quote_s, extract_message(exp.cadr()))
    argl = cons(S('list'), exp.cddr())
    if send_app(exp): return plist(exp.car(), msg, argl)

def construct_arglist(operand_codes, pop_all_symbol):
    operand_codes = operand_codes.reverse()
    if operand_codes.nullp():
        return make_ir_seq((), (argl_r,), MOV(argl_r, nil_r))
    code_to_get_last_arg = append_ir_seqs(operand_codes.car(),
                                          make_ir_seq((val_r,), (argl_r,),
                                              LIST(argl_r, val_r)))
    if operand_codes.cdr().nullp(): return code_to_get_last_arg
    return preserving((env_r,), code_to_get_last_arg,
            code_to_get_rest_args(operand_codes.cdr(), pop_all_symbol), pop_all_symbol)

cons_s = S('cons')
def code_to_get_rest_args(operand_codes, pop_all_symbol):
    code_for_next_arg = preserving((argl_r,), operand_codes.car(),
                                   make_ir_seq((val_r, argl_r), (argl_r,),
                                       CONS(argl_r, val_r, argl_r)), pop_all_symbol)
    if operand_codes.cdr().nullp(): return code_for_next_arg
    return preserving((env_r,), code_for_next_arg,
            code_to_get_rest_args(operand_codes.cdr(), pop_all_symbol), pop_all_symbol)

addr_r = S('addr')
primitive_procedure_s = S('primitive-procedure')
def compile_procedure_call(target, linkage, cenv, message, pop_all_symbol):
  primitive_branch = make_label('primitive-branch')
  compiled_branch = make_label('compiled-branch')
  after_call = make_label('after-call')

  compiled_linkage = linkage
  if linkage is next_s: compiled_linkage = after_call
  return \
  append_ir_seqs(make_ir_seq((proc_r,), (addr_r,),
                      CLOSURE_METHOD(addr_r, proc_r, message),
                      BPRIM(addr_r, primitive_branch)),
                 parallel_ir_seqs(
                     append_ir_seqs(
                         compiled_branch,
                         compile_meth_invoc(target, compiled_linkage, cenv, message, pop_all_symbol)),
                     append_ir_seqs(
                         primitive_branch,
                         end_with_linkage(linkage,
                             make_ir_seq((addr_r, proc_r, argl_r), (target,),
                               APPLY_PRIM_METH(target, addr_r, proc_r, argl_r)), pop_all_symbol))),
                 after_call)

def compile_meth_invoc(target, linkage, cenv, message, pop_all_symbol):
    if target is val_r:
        if linkage is return_s:
            return make_ir_seq((addr_r, continue_r), all_writable_regs,
                    pop_all_symbol,
                    GOTO_REG(addr_r))
        else:
            return make_ir_seq((addr_r,), all_writable_regs,
                    LOAD_ADDR(continue_r, linkage),
                    GOTO_REG(addr_r))
    else:
        if linkage is return_s:
            raise CompileError(target, 'return linkage, target not val')
        else:
            proc_return = make_label('proc_return')
            return make_ir_seq((addr_r,), all_writable_regs,
                LOAD_ADDR(continue_r, proc_return),
                GOTO_REG(addr_r),
                proc_return,
                MOV(target, val_r),
                GOTO_LABEL(linkage))

def compile_linkage(linkage, pop_all_symbol):
    if linkage is S('return'):
        return make_ir_seq((S('continue'),), (), pop_all_symbol, GOTO_REG(S('continue')))
    if linkage is next_s:
        return empty_instruction_seq()
    return make_ir_seq((), (), GOTO_LABEL(linkage))

def end_with_linkage(linkage, ir_seq, pop_all_symbol):
    return preserving((continue_r,),
            ir_seq,
            compile_linkage(linkage, pop_all_symbol),
            pop_all_symbol)

def if_alternative(exp):
    if exp.cdddr().nullp(): return Integer(0)
    return exp.cadddr()

# (fn (param1 param2) body1 body2)
def fn_params(exp):
    return exp.cadr()

# (fn (param1 param2) body1 body2)
def fn_body(exp):
    return exp.cddr()

def fn2obj(exp):
    sig = cons(run_s, fn_params(exp))
    body = fn_body(exp)
    return make_obj(plist(cons(sig, body)))

def shfn2fn(exp):
    seq = exp.cdr()
    return make_fn(scan_out_xyz_sequence(seq), seq)

def variablep(exp):
    return symbolp(exp)

def definition_variable(exp):
    if symbolp(exp.cadr()): return exp.cadr()
    return exp.caadr()

def definition_value(exp):
    if symbolp(exp.cadr()): return exp.caddr()
    return make_fn(exp.cdadr(), exp.cddr())

def make_obj(tos):
    return cons(obj_s, tos)

def make_fn(parameters, body):
    return cons(fn_s, cons(parameters, body))

def self_evaluatingp(exp):
    return isinstance(exp, Integer) or isinstance(exp, String) or isinstance(exp, Decimal)

def tagged_list(exp, tag):
    return pairp(exp) and (not exp.nullp()) and exp.car() is tag

def tagged_list2(exp, tag):
    return pairp(exp) and (not exp.nullp()) and tagged_list(exp.cdr(), tag)

def lexical_address_lookup(addr, env):
    return env.lexical_lookup(*addr)

def lexical_address_set(addr, env, val):
    return env.lexical_lookup(addr[0], addr[1], val)

def find_variable(var, cenv):
    def find(cframe, var):
        for i, x in enumerate(cframe):
            if var is x: return i
        return -1
    def help(n, cenv):
        if cenv.nullp(): return not_found_s
        cframe = cenv.car()
        if var in cframe:
            return n, find(cframe, var)
        return help(n + 1, cenv.cdr())
    return help(0, cenv)

def make_quote(name):
  return plist(quote_s, name)

def make_load_module(name):
  return plist(load_module_s, make_quote(name))

def make_def(name, exp):
  return plist(def_s, name, exp)

def make_call(rcv, msg, *args):
  return plist(rcv, S('.' + str(msg)), *args)

def make_send(rcv, msg, *args):
  return plist(rcv, S(':' + str(msg)), *args)

def make_if(test, consequent, alternative=None):
  if alternative is None: return plist(if_s, test, consequent)
  return plist(if_s, test, consequent, alternative)

def make_do(*stmts):
  return plist(do_s, *stmts)

def expand_imports(seq):
  def expand_import(stmt):
    def old_name(term):
      if symbolp(term): return term
      return term.car()
    def new_name(term):
      if symbolp(term): return term
      return term.caddr()
    def import_term2def(term):
      return make_def(new_name(term),
                      make_call(new_name(stmt.cadr()), old_name(term)))
    def module2def(name):
      return make_def(new_name(name), make_load_module(old_name(name)))
    terms = stmt.cddr()
    return cons(module2def(stmt.cadr()), terms.map(import_term2def))

  if seq.nullp(): return seq
  return expand_import(seq.car()).append(seq.cdr())

def expand_export(seq):
    def export2obj(exp):
      def export_term2to(term):
        if symbolp(term): return cons(plist(term), plist(term))
        return cons(plist(term.caddr()), plist(term.car()))
      tos = exp.cdr().map(export_term2to)
      return make_obj(tos)

    export_stmt = seq.car()
    rest = seq.cdr()
    return rest.append(plist(export2obj(export_stmt)))

else_s = S('else')
false_s = S('false')
def if_macro_alternative(seq):
  if seq.nullp(): return false_s
  next = seq.car()
  if tagged_list(next, else_s): return cons(do_s, next.cdr())
  return false_s

def if_macro_tail(seq):
  if seq.nullp(): return seq
  next = seq.car()
  if tagged_list(next, else_s): return seq.cdr()
  return seq

elif_s = S('elif')
def collect_elifs(seq):
  if seq.nullp(): return false_s, seq
  next = seq.car()
  if tagged_list(next, else_s):
    return cons(do_s, next.cdr()), seq.cdr()
  if not tagged_list(next, elif_s): return false_s, seq
  alt, tail = collect_elifs(seq.cdr())
  return plist(do_s, cons(if_s, next.cdr()), plist(else_s, alt)), tail

def expand_if(seq):
  def help(stmt, alt):
    test = stmt.cadr()
    consequent = cons(do_s, stmt.cddr())
    return plist(qmark_s, test, consequent, alt)
  if seq.nullp(): return seq
  alternative, tail = collect_elifs(seq.cdr())
  return cons(help(seq.car(), alternative), tail)

def report_else_error(seq):
  raise CompileError(seq.car(), 'else with no preceding if')

def report_elif_error(seq):
  raise CompileError(seq.car(), 'elif with no preceding if')

in_s = S('in')
map_s = S('map')
def expand_for(exp):
  param = exp.cadr()
  in_word = exp.caddr()
  seq_exp = exp.cadddr()
  body_seq = exp.cddddr()
  if in_word is not in_s:
    raise CompileError(exp, 'should be "for <expr> in <expr>"')
  f = cons(fn_s, cons(plist(param), body_seq))
  return plist(map_s, f, seq_exp)

seq_macros = {
  'import': expand_imports,
  'export': expand_export,
  'if': expand_if,
  'else': report_else_error,
  'elif': report_elif_error,
}

simple_macros = {
  'for': expand_for,
}

unassigned_s = S(':unassigned:')
q_unassigned_s = plist(quote_s, unassigned_s)
def scan_out_defines(body):
    def scan(body):
        if body.nullp(): return nil, nil, nil
        exp = body.car()
        vars, exps, voids = scan(body.cdr())
        if tagged_list(exp, def_s):
            var = definition_variable(exp)
            val = definition_value(exp)
            set = plist(set__s, var, val)
            return cons(var, vars), cons(set, exps), cons(q_unassigned_s, voids)
        else:
            return vars, cons(exp, exps), voids
    vars, exps, voids = scan(body)
    if vars.nullp(): return exps
    binding = make_fn(vars, exps)
    return plist(cons(binding, voids)) # apply the fn to the void values

def set_equal(l1, l2):
  if l1.nullp() and l2.nullp(): return True
  if l1.nullp() or l2.nullp(): return False
  if l1.car() is not l2.car(): return False
  return set_equal(l1.cdr(), l2.cdr())

def set_minus1(l, x):
  if l.nullp(): return nil
  a = l.car()
  d = set_minus1(l.cdr(), x)
  return d if a is x else cons(a, d)

def set_minus(a, b):
  if b.nullp(): return a
  return set_minus1(set_minus(a, b.cdr()), b.car())

# only works with sorted lists
def set_merge(l1, l2):
  if l1.nullp(): return l2
  if l2.nullp(): return l1
  l1a = l1.car()
  l2a = l2.car()
  if l1a is l2a: return cons(l1a, set_merge(l1.cdr(), l2.cdr()))
  if l1a < l2a: return cons(l1a, set_merge(l1.cdr(), l2))
  if l1a > l2a: return cons(l2a, set_merge(l1, l2.cdr()))
  raise RuntimeError("can't happen")

def scan_out_xyz(exp):
    if self_evaluatingp(exp): return nil
    if tagged_list(exp, quote_s): return nil
    if variablep(exp): return scan_out_xyz_variable(exp)
    if tagged_list(exp, set__s): return scan_out_xyz_assignment(exp)
    if tagged_list(exp, def_s): return scan_out_xyz_definition(exp)
    #if tagged_list(exp, load_module_s): return nil
    if tagged_list(exp, if_s): return scan_out_xyz_if(exp)
    if tagged_list(exp, fn_s): return scan_out_xyz_obj(fn2obj(exp))
    if tagged_list(exp, shfn_s): return nil # impossible
    if tagged_list(exp, obj_s): return scan_out_xyz_obj(exp)
    if tagged_list(exp, sobj_s): return scan_out_xyz_sobj(exp)
    if tagged_list(exp, do_s): return scan_out_xyz_do(exp)
    if tagged_list(exp, inline_s): return nil
    if pairp(exp): return scan_out_xyz_application(exp)
    raise CompileError(exp, 'Unknown expression type in scan_out_xyz')

def scan_out_xyz_obj(exp):
    return foldl(set_merge, nil, exp_methods(exp).map(scan_out_xyz_method))

def scan_out_xyz_sobj(exp):
    return foldl(set_merge, nil, sobj_methods(exp).map(scan_out_xyz_method))

def memq(x, l):
    if l.nullp(): return False
    if l.car() is x: return l
    return memq(x, l.cdr())

def meth_xyz_params(meth):
    params = meth_params(meth)
    x_l = plist(x_s) if memq(x_s, params) else nil
    y_l = plist(y_s) if memq(y_s, params) else nil
    z_l = plist(z_s) if memq(z_s, params) else nil
    return set_merge(x_l, set_merge(y_l, z_l))

def scan_out_xyz_method(meth):
    if set_equal(meth_xyz_params(meth), plist(x_s, y_s, z_s)): return nil
    return set_minus(scan_out_xyz_sequence(meth_body(meth)),
                                           meth_xyz_params(meth))

x_s = S('x')
y_s = S('y')
z_s = S('z')
def scan_out_xyz_variable(exp):
  if exp in (x_s, y_s, z_s): return plist(exp)
  return nil

def foldl(f, i, l):
    if l.nullp(): return i
    return foldl(f, f(i, l.car()), l.cdr())

def scan_out_xyz_assignment(exp): return scan_out_xyz(exp.caddr())
def scan_out_xyz_definition(exp): return scan_out_xyz(definition_value(exp))
def scan_out_xyz_if(exp):
    return set_merge(scan_out_xyz(exp.cadr()),
                 set_merge(scan_out_xyz(exp.caddr()),
                       scan_out_xyz(if_alternative(exp))))
def scan_out_xyz_do(exp): return scan_out_xyz_sequence(exp.cdr())
def scan_out_xyz_application(exp):
    return foldl(set_merge, nil, exp.map(scan_out_xyz))

def scan_out_xyz_sequence(seq):
    if seq.nullp(): return nil
    exp = seq.car()
    return set_merge(scan_out_xyz(exp), scan_out_xyz_sequence(seq.cdr()))

