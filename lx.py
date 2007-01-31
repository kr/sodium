
from typ import *
from pair import cons, nil, pairp
from pair import list as plist
from env import *
from ir import *
from util import traced

class Object(object):
    count = 0
    def __init__(self, tos, env):
        self.oid = Object.count
        Object.count += 1
        self.name = ':::'
        self.env = env
        self.methods = {}
        for name, params, bproc in tos:
            self.methods[name] = make_method(params, bproc)
    def __repr__(self): return '<%s object %d>' % (self.name, self.oid)
    def __str__(self): return repr(self)
    def __getattr__(self, name):
        try:
            return super(Object, self).__getattr__(name)
        except AttributeError, e:
            return lambda *args: call(self, S(name), args)

method_s = S('method')
def make_method(parameters, body):
    return plist(method_s, parameters, body)

quote_s = S('quote')
set__s = S('set!')
def_s = S('def')
import_s = S('import')
if_s = S('if')
fn_s = S('fn')
obj_s = S('obj')
begin_s = S('begin')
inline_s = S('inline')
def compile(exp, target, linkage, cenv):
    if self_evaluatingp(exp):
        return compile_self_evaluating(exp, target, linkage, cenv)
    if tagged_list(exp, quote_s): return compile_quoted(exp, target, linkage, cenv)
    if variablep(exp): return compile_variable(exp, target, linkage, cenv)
    if tagged_list(exp, set__s): return compile_assignment(exp, target, linkage, cenv)
    if tagged_list(exp, def_s): return compile_definition(exp, target, linkage, cenv)
    if tagged_list(exp, import_s): return compile_import(exp, target, linkage, cenv)
    if tagged_list(exp, if_s): return compile_if(exp, target, linkage, cenv)
    if tagged_list(exp, fn_s): return compile_obj(fn2obj(exp), target, linkage, cenv)
    if tagged_list(exp, obj_s): return compile_obj(exp, target, linkage, cenv)
    if tagged_list(exp, begin_s): return compile_sequence(exp.cdr(), target, linkage, cenv)
    if tagged_list(exp, inline_s): return compile_inline(exp)
    if pairp(exp): return compile_application(exp, target, linkage, cenv)
    raise Exception, 'Unknown expression type %s' % exp

env_r = S('env')
global_r = S('global')
not_found_s = S('not-found')
def compile_self_evaluating(exp, target, linkage, cenv):
    return end_with_linkage(linkage,
            make_ir_seq((), (target,), LOAD_IMM(target, exp)))
def compile_quoted(exp, target, linkage, cenv):
    return end_with_linkage(linkage,
            make_ir_seq((), (target,), LOAD_IMM(target, exp.cadr())))
def compile_variable(exp, target, linkage, cenv):
    addr = find_variable(exp, cenv)
    if addr is not_found_s:
        if exp == '%':
            raise 'lookup of %'
        return end_with_linkage(linkage,
            make_ir_seq((), (target),
                LOOKUP(target, global_r, exp)))
    return end_with_linkage(linkage,
            make_ir_seq((env_r,), (target,),
                LEXICAL_LOOKUP(target, addr)))

val_r = S('val')
next_s = S('next')
ok_s = S('ok')
def compile_assignment(exp, target, linkage, cenv):
    var = exp.cadr()
    get_value_code = compile(exp.caddr(), val_r, next_s, cenv)
    addr = find_variable(var, cenv)
    if addr is not_found_s:
        return end_with_linkage(linkage,
                preserving((), get_value_code,
                    make_ir_seq((val_r,), (target,),
                        SET_(global_r, val_r, var),
                        LOAD_IMM(target, ok_s))))
    return end_with_linkage(linkage,
            preserving((env_r,), get_value_code,
                make_ir_seq((env_r, val_r), (target,),
                    LEXICAL_SETBANG(val_r, addr),
                    LOAD_IMM(target, ok_s))))

def compile_definition(exp, target, linkage, cenv):
    var = definition_variable(exp)
    get_value_code = compile(definition_value(exp), val_r, next_s, cenv)
    if cenv is not nil: raise 'error, cannot have define here', exp
    return end_with_linkage(linkage,
            preserving((env_r,), get_value_code,
                make_ir_seq((env_r, val_r), (target,),
                    DEFINE(env_r, val_r, var),
                    LOAD_IMM(target, ok_s))))

def compile_import(exp, target, linkage, cenv):
    name = import_name(exp)
    return end_with_linkage(linkage,
            make_ir_seq((), (target),
                LOOKUP_MODULE(target, name)))

def import_name(exp):
    return exp.cadr()

continue_r = S('continue')
def compile_if(exp, target, linkage, cenv):
    t_branch = make_label('true-branch')
    f_branch = make_label('false-branch')
    after_if = make_label('after-if')
    #consequent_linkage = (linkage is next_s) if after_if else linkage
    consequent_linkage = linkage
    if linkage is next_s: consequent_linkage = after_if
    p_code = compile(exp.cadr(), val_r, next_s, cenv)
    c_code = compile(exp.caddr(), target, consequent_linkage, cenv)
    a_code = compile(if_alternative(exp), target, linkage, cenv)
    return preserving((env_r, continue_r), p_code,
            append_ir_seqs(
                make_ir_seq((val_r,), (),
                    BF(val_r, f_branch)),
                parallel_ir_seqs(
                    append_ir_seqs(t_branch, c_code),
                    append_ir_seqs(f_branch, a_code)),
                after_if))

def compile_sequence(seq, target, linkage, cenv):
    if seq.nullp(): raise "value of null sequence is undefined"
    if seq.cdr().nullp(): return compile(seq.car(), target, linkage, cenv)
    return preserving((env_r, continue_r),
        compile(seq.car(), target, next_s, cenv),
        compile_sequence(seq.cdr(), target, linkage, cenv))

def compile_obj(exp, target, linkage, cenv):
    obj_table = make_label('obj-table')
    after_obj = make_label('after-obj')
    m_tabl_code = make_ir_seq((), (),
        obj_table,
        DATUM(exp_methods(exp).len()))
    m_body_code = empty_instruction_seq()
    for meth in exp_methods(exp):
        name = meth_name(meth)
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
                    MAKE_COMPILED_OBJ(target, env_r, val_r))),
            tack_on_ir_seq(m_tabl_code, m_body_code)),
        after_obj)

def exp_methods(exp):
    return exp.cddr()

def is_inline_meth(meth):
    return tagged_list(meth, inline_s)

def make_meth_entry(meth):
    if is_inline_meth(meth):
        return InlineMethEntry(make_label('inline_meth_entry'))
    return make_label('method-entry')

def meth_name(meth):
    if is_inline_meth(meth): return meth.caddr()
    return meth.caar()

def meth_params(meth):
    return meth.cdar()

def meth_body(meth):
    return meth.cdr()

proc_r = S('proc')
argl_r = S('argl')
return_s = S('return')
tmp_r = S('tmp')
def compile_meth_body(meth, meth_entry, cenv):
    if is_inline_meth(meth):
        return compile_inline_meth_body(meth, meth_entry, cenv)
    formals = meth_params(meth)
    body = scan_out_defines(meth_body(meth))
    cenv = cons(formals, cenv)
    return append_ir_seqs(
        make_ir_seq((env_r, proc_r, argl_r), (env_r, tmp_r),
            meth_entry,
            COMPILED_OBJ_ENV(env_r, proc_r),
            LOAD_IMM(tmp_r, formals),
            EXTEND_ENVIRONMENT(env_r, env_r, argl_r, tmp_r)),
        compile_sequence(body, val_r, return_s, cenv))

def compile_inline_meth_body(meth, entry, cenv):
    class lexical_scanner(object):
        def __init__(self, cenv):
            self.cenv = cenv
        def __getitem__(self, k):
            addr = find_variable(S(k), self.cenv)
            return 'lexical_lookup(compiled_obj_env(rcv), %d, %d)' % addr

    c_def = '''static datum
%(name)s(datum rcv, datum args)
{
%(body)s
}
'''
    body = meth.cadddr() % lexical_scanner(cenv)
    c_def %= { 'name':str(entry), 'body':body }
    seq = empty_instruction_seq()
    seq.add_c_defs(c_def)
    return seq

def compile_inline(exp):
    seq = empty_instruction_seq()
    seq.add_c_defs(exp.caddr())
    return seq

def compile_application(exp, target, linkage, cenv):
    proc_code = compile(exp_object(exp), proc_r, next_s, cenv)
    message = exp_message(exp)
    operand_codes = exp_operands(exp).map(lambda operand: compile(operand, val_r, next_s, cenv))
    return preserving((env_r, continue_r),
                      proc_code,
                      preserving((proc_r, continue_r),
                                 construct_arglist(operand_codes),
                                 compile_procedure_call(target, linkage, cenv, message)))

def message_send(exp):
    return call_app(exp) or send_app(exp)
    #if exp.len() < 2: return False
    #m = exp.cadr()
    #return symbolp(m) and (m[0] in (':', '.') or str(m) in PUNC)

def call_app(exp):
    if exp.len() < 2: return False
    m = exp.cadr()
    return symbolp(m) and (m[0] == ':' or str(m) in PUNC)

def send_app(exp):
    if exp.len() < 2: return False
    m = exp.cadr()
    return symbolp(m) and (m[0] == '.' or str(m) in PUNC)

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

def construct_arglist(operand_codes):
    operand_codes = operand_codes.reverse()
    if operand_codes.nullp(): return make_ir_seq((), (argl_r,),
                                                LOAD_IMM(argl_r, nil))
    code_to_get_last_arg = append_ir_seqs(operand_codes.car(),
                                          make_ir_seq((val_r,), (argl_r,),
                                              LIST(argl_r, val_r)))
    if operand_codes.cdr().nullp(): return code_to_get_last_arg
    return preserving((env_r,), code_to_get_last_arg,
            code_to_get_rest_args(operand_codes.cdr()))

cons_s = S('cons')
def code_to_get_rest_args(operand_codes):
    code_for_next_arg = preserving((argl_r,), operand_codes.car(),
                                   make_ir_seq((val_r, argl_r), (argl_r),
                                       CONS(argl_r, val_r, argl_r)))
    if operand_codes.cdr().nullp(): return code_for_next_arg
    return preserving((env_r,), code_for_next_arg,
            code_to_get_rest_args(operand_codes.cdr()))

addr_r = S('addr')
primitive_procedure_s = S('primitive-procedure')
def compile_procedure_call(target, linkage, cenv, message):
  primitive_branch = make_label('primitive-branch')
  compiled_branch = make_label('compiled-branch')
  after_call = make_label('after-call')

  compiled_linkage = linkage
  if linkage is next_s: compiled_linkage = after_call
  return \
  append_ir_seqs(make_ir_seq((proc_r,), (addr_r,),
                      COMPILED_OBJECT_METHOD(addr_r, proc_r, message),
                      BPRIM(addr_r, primitive_branch)),
                 parallel_ir_seqs(
                     append_ir_seqs(
                         compiled_branch,
                         compile_meth_invoc(target, compiled_linkage, cenv, message)),
                     append_ir_seqs(
                         primitive_branch,
                         end_with_linkage(linkage,
                             make_ir_seq((addr_r, proc_r, argl_r), (target,),
                               APPLY_PRIM_METH(target, addr_r, proc_r, argl_r))))),
                 after_call)

def compile_meth_invoc(target, linkage, cenv, message):
    if target is val_r:
        if linkage is return_s:
            return make_ir_seq((addr_r, continue_r), all_writable_regs,
                    GOTO_REG(addr_r))
        else:
            return make_ir_seq((addr_r,), all_writable_regs,
                    LOAD_ADDR(continue_r, linkage),
                    GOTO_REG(addr_r))
    else:
        if linkage is return_s:
            raise RuntimeError, ('return linkage, target not val -- COMPILE %s' % target)
        else:
            proc_return = make_label('proc_return')
            return make_ir_seq((addr_r,), all_writable_regs,
                LOAD_ADDR(continue_r, proc_return),
                GOTO_REG(addr_r),
                proc_return,
                MOV(target, val_r),
                GOTO_LABEL(linkage))

def compile_linkage(linkage):
    if linkage is S('return'):
        return make_ir_seq((S('continue'),), (), GOTO_REG(S('continue')))
    if linkage is next_s:
        return empty_instruction_seq()
    return make_ir_seq((), (), GOTO_LABEL(linkage))

def end_with_linkage(linkage, ir_seq):
    return preserving((continue_r,), ir_seq, compile_linkage(linkage))

def eval(exp, env):
    return analyze(exp)(env)

def analyze(exp):
    if self_evaluatingp(exp): return analyze_self_evaluating(exp)
    if tagged_list(exp, quote_s): return analyze_quoted(exp)
    if variablep(exp): return analyze_variable(exp)
    if tagged_list(exp, set__s): return analyze_assignment(exp)
    if tagged_list(exp, def_s): return analyze_definition(exp)
    if tagged_list(exp, if_s): return analyze_if(exp)
    if tagged_list(exp, fn_s): return analyze(fn2obj(exp))
    if tagged_list(exp, obj_s): return analyze_obj(exp)
    if tagged_list(exp, begin_s): return analyze_sequence(exp.cdr())
    if pairp(exp): return analyze_application(exp)
    raise Exception, 'Unknown expression type %s' % exp

def analyze_self_evaluating(exp):
    return lambda env: exp

def analyze_quoted(exp):
    qval = exp.cadr()
    return lambda env: qval

def analyze_variable(exp):
    return lambda env: env[exp]

def analyze_assignment(exp):
    var = exp.cadr()
    vproc = analyze(exp.caddr())
    def execute(env):
        env[var] = vproc(env)
        return 'ok'
    return execute

def analyze_definition(exp):
    var = definition_variable(exp)
    vproc = analyze(definition_value(exp))
    def execute(env):
        env.define(var, vproc(env))
        return 'ok'
    return execute

def analyze_if(exp):
    pproc = analyze(exp.cadr())
    cproc = analyze(exp.caddr())
    aproc = analyze(if_alternative(exp))
    def execute(env):
        if truep(pproc(env)):
            return cproc(env)
        else:
            return aproc(env)
    return execute

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

def analyze_obj(exp):
    def f(to):
        name = to.caar()
        vars = to.cdar()
        bproc = analyze_sequence(scan_out_defines(to.cdr()))
        return plist(name, vars, bproc)
    return lambda env: make_object(exp.cddr().map(f), env)

def analyze_sequence(exps):
    def sequentially(proc1, proc2):
        def execute(env):
            proc1(env)
            return proc2(env)
        return execute
    def loop(first, rest):
        if rest.nullp():
            return first
        else:
            return loop(sequentially(first, rest.car()), rest.cdr())
    procs = exps.map(analyze)
    if procs.nullp(): raise Exception, 'Empty sequence -- ANALYZE'
    return loop(procs.car(), procs.cdr())

def analyze_application(exp):
    fproc = analyze(exp.car())
    if (not exp.cdr().nullp()) and symbolp(exp.cadr()):
        f, msg = None, exp.cadr()
        if msg[0] == ':': f, msg = call, S(msg[1:])
        if msg[0] == '.': f, msg = send, S(msg[1:])
        if len(msg) == 1 and msg in PUNC: f, msg = call, msg
        if f:
            aprocs = exp.cddr().map(analyze)
            def execute(env):
                return f(fproc(env), msg, map(lambda ap: ap(env), aprocs))
            return execute
    aprocs = exp.cdr().map(analyze)
    def execute(env):
        return execute_application(fproc(env),
                                   map(lambda aproc: aproc(env), aprocs))
    return execute

def execute_application(proc, args):
    if callable(proc):
        return proc(*args)
    if isinstance(proc, Object):
        return proc.run(*args)
    raise Exception, 'Unknown procedure type ' + proc + type(proc)

def truep(v):
    #if v is None: return False
    if isinstance(v, Integer): return not not v
    #if isinstance(v, String): return not not v
    #if isinstance(v, S): return not not v
    if v is False: return False
    return True

method_s = S('method')
def call(rcv, msg, args):
    if isinstance(rcv, Object):
        if msg not in rcv.methods:
            raise RuntimeError, 'object does not understand %s' % msg
        meth = rcv.methods[msg]
        if callable(meth): return meth(*((rcv,) + tuple(args)))
        if tagged_list(meth, method_s):
            params, bproc = meth.cdr()
            return bproc(rcv.env.extend(params, args))
        raise Exception, 'Unknown method type %r' % (meth,)
    else:
        return getattr(rcv, msg)(*args)

def send(rcv, msg, args):
    if isinstance(rcv, Object):
        if send_s in rcv.methods:
            return rcv.send(msg, args)
        promise, resolver = make_promise()
        tasks.append((rcv, msg, args, resolver))
        return promise
    raise Exception, 'Unknown receiver type %r' % (rcv,)

def eval_sequence(exps, env):
    return eval(cons(begin_s, exps), env)

def variablep(exp):
    return symbolp(exp)

def definition_variable(exp):
    if symbolp(exp.cadr()): return exp.cadr()
    return exp.caadr()

def definition_value(exp):
    if symbolp(exp.cadr()):
        if exp.cdddr().nullp(): return exp.caddr()
        return make_obj(exp.cdddr())
    return make_fn(exp.cdadr(), exp.cddr())

def make_obj(tos):
    return cons(obj_s, cons(nil, tos))

def make_fn(parameters, body):
    return cons(fn_s, cons(parameters, body))

def make_procedure(parameters, bproc, env):
    return make_object(((run_s, parameters, bproc),), env)

def make_object(tos, env):
    return Object(tos, env)

def self_evaluatingp(exp):
    return isinstance(exp, Integer) or isinstance(exp, String) or isinstance(exp, Decimal)

def tagged_list(exp, tag):
    return pairp(exp) and (not exp.nullp()) and exp.car() is tag

class EmptyEnvironment(dict):
    def __setitem__(self, var, val):
        self[var]

    def __str__(self):
        return '{}'

class Environment(object):
    def __init__(self, parent, frame, order):
        self.parent = parent
        self.frame = frame
        self.order = order

    @classmethod
    def make(clas, **frame):
        return clas(EmptyEnvironment(), frame, ())

    def extend(self, vars, vals):
        frame = {}
        frame.update(zip(vars, vals))
        return Environment(self, frame, vars)

    @staticmethod
    def check(var, val):
        if val is unassigned_s:
            raise RuntimeError, ('Access unassigned variable %r' % (var,))
        return val

    def __getitem__(self, var):
        if var in self.frame: return self.check(var, self.frame[var])
        return self.check(var, self.parent[var])

    def __setitem__(self, var, val):
        if var in self.frame:
            self.frame[var] = val
        else:
            self.parent[var] = val

    def define(self, var, val):
        if isinstance(val, Object):
            val.name = var
        self.frame[var] = val

    def lexical_lookup(self, frame, displacement):
        if frame > 0: return self.parent.lexical_lookup(frame - 1, displacement)
        return self.frame[order[displacement]]

    def lexical_set(self, frame, displacement, val):
        if frame > 0: return self.parent.lexical_set(frame - 1, displacement, val)
        self.frame[order[displacement]] = val

    def __repr__(self):
        names = ' '.join(self.frame.keys())
        return '{%s} -> %r' % (names, self.parent)

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
    return plist(cons(binding, voids))

def make_promise():
    return pair2tuple(global_env['make-promise'].run())

def work_left():
    return len(tasks)

def get_task():
    return tasks.pop(0)


PUNC = '+-*/%=<>'

global_env = Environment.make(**{
    'true':True,
    'false':False,
    'is?':isp,
    'cons':cons,
    'make-dict':make_dict,
    'rep':rep,
    'pr':pr,
    'error':error,
    'send':send,
    'call':call,
})

tasks = []

