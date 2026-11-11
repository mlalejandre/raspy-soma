#ifndef SOMA_WEB_JS_I_H
#define SOMA_WEB_JS_I_H
#include "web_js.h"
#include "web_f64.h"
#include "web_util.h"

typedef struct jstr { uint32_t len, hash; uint8_t interned; char d[1]; } jstr;
typedef struct jobj jobj;
typedef struct jv { uint8_t t; union { f64 n; int b; jstr *s; jobj *o; } u; } jv;
enum { JT_UNDEF = 0, JT_NULL, JT_BOOL, JT_NUM, JT_STR, JT_OBJ };

enum { JC_OBJECT = 0, JC_ARRAY, JC_FUNC, JC_NATIVE, JC_ERROR, JC_DATE, JC_REGEXP, JC_PROMISE, JC_MAP, JC_SET, JC_HOST, JC_BOOL, JC_NUMBER, JC_STRING, JC_ARGS, JC_BOUND };
enum { JP_ENUM = 1, JP_ACC = 2, JP_RO = 4, JP_NOCONF = 8 };
struct jprop { jstr *key; jv val; jv set; uint8_t flags; };

struct jrt;
typedef jv (*jnative)(struct jrt *rt, jv thisv, jv *args, int argc, int magic);

struct jdyn {
    jv (*get)(struct jrt *rt, jobj *o, jstr *key, int *found);
    void (*put)(struct jrt *rt, jobj *o, jstr *key, jv val);
    int (*del)(struct jrt *rt, jobj *o, jstr *key);
    int (*has)(struct jrt *rt, jobj *o, jstr *key);
    jobj *(*keys)(struct jrt *rt, jobj *o);
};
struct scope;
struct jn;
struct jobj {
    uint8_t cls, frozen, extensible, flag; int16_t magic;
    jobj *proto; struct jprop *props; uint32_t np, cap; uint32_t *idx; uint32_t icap;
    jv *el; uint32_t alen, acap;                         /* array / map entries / args */
    union {
        struct { struct jn *node; struct scope *env; jobj *home; jv bthis; struct jn *cls; } fn;
        struct { jnative f; } nat;
        struct { void *p; int kind; } host;
        struct { f64 t; } date;
        struct { void *prog; jstr *src; jstr *flags; } re;
        struct { int state; jv value; void *react; int handled; } prom;
    } u;
};

/* ---------- AST ---------- */
enum {
    N_NUM = 1, N_STR, N_TPL, N_REGEX, N_IDENT, N_THIS, N_NULL, N_TRUE, N_FALSE, N_UNDEF, N_ARRAY, N_OBJECT, N_PROP, N_FUNC, N_CLASS,
    N_UNARY, N_UPDATE, N_BINARY, N_LOGICAL, N_ASSIGN, N_COND, N_CALL, N_NEW, N_MEMBER, N_SEQ, N_SPREAD, N_AWAIT, N_SUPER, N_HOLE, N_TAGGED, N_NEWTARGET,
    N_VAR, N_EXPR, N_BLOCK, N_IF, N_FOR, N_FORIN, N_FOROF, N_WHILE, N_DO, N_RETURN, N_BREAK, N_CONTINUE, N_THROW, N_TRY, N_SWITCH, N_CASE,
    N_LABEL, N_EMPTY, N_FUNCDECL, N_CLASSDECL, N_PROGRAM, N_DECL
};
enum { NF_ARROW = 1, NF_ASYNC = 2, NF_EXPRBODY = 4, NF_METHOD = 8, NF_GETTER = 16, NF_SETTER = 32, NF_STATIC = 64, NF_CTOR = 128, NF_DERIVED = 256,
       NF_COMPUTED = 512, NF_SHORT = 1024, NF_OPT = 2048, NF_PAREN = 4096, NF_FIELD = 8192, NF_LEX = 16384 };
struct jfi { jstr **vn; int nvn; int ncap; int has_closure; int uses_args; jstr *name; const char *src; uint32_t srclen; int nparams; };
struct jn {
    uint8_t t, op; uint16_t fl; int line;
    struct jn *a, *b, *c, *d, *next;
    jstr *str; f64 num; void *p; int ival;
};

enum { B_ADD = 1, B_SUB, B_MUL, B_DIV, B_MOD, B_EXP, B_EQ, B_NE, B_SEQ, B_SNE, B_LT, B_GT, B_LE, B_GE, B_SHL, B_SHR, B_USHR, B_AND, B_OR, B_XOR, B_INSTANCEOF, B_IN, B_LAND, B_LOR, B_NULLISH };

/* ---------- ambito ---------- */
struct jvar { jstr *name; jv v; uint8_t kind; };       /* 0 var, 1 let, 2 const */
struct scope {
    struct scope *parent; struct jvar *vars; uint16_t n, cap; uint8_t is_fn, arrow, heap;
    jv this_v; jobj *callee; jv *argv; int argc; jobj *newtarget; jobj *home;
};

struct jtimer { struct jtimer *next; int id; uint64_t due; uint32_t interval; int repeat; jv fn; jv *args; int nargs; };
struct jmicro { struct jmicro *next; jv fn; jv a, b; int kind; jobj *prom; };

/* interrupcion estructurada */
struct jtry { struct jtry *up; void *jb[5]; jv exc; int depth; char *sstop; struct scope *scope; };

struct jrt {
    struct wjs *js;
    struct wa_arena ar; uint32_t heap_cap;
    char *sstack, *stop, *send;
    struct jtry *try_top; int depth; uint32_t steps; uint32_t step_limit; int halted; char errbuf[256];
    jobj *global; struct scope *gscope;
    jobj *p_object, *p_function, *p_array, *p_string, *p_number, *p_boolean, *p_error, *p_date, *p_regexp, *p_promise, *p_map, *p_set, *p_iter, *p_symbol;
    jobj *p_errs[8];
    jstr **itab; uint32_t itab_cap, itab_n;
    jstr *s_length, *s_prototype, *s_constructor, *s_proto, *s_name, *s_message, *s_empty, *s_undefined, *s_null, *s_true, *s_false, *s_object, *s_function,
         *s_number, *s_string, *s_boolean, *s_nan, *s_stack, *s_then, *s_value, *s_done, *s_next, *s_index, *s_input, *s_get, *s_set;
    jstr *s_chars[128];
    struct jtimer *timers, *timers_tail; int next_timer; uint64_t now;
    struct jmicro *mhead, *mtail;
    const struct wjs_host *host; struct wdoc *doc;
    uint32_t rng[4];
    int in_event; jobj *cur_event; int async_depth;
    jv last_exc; int exc_pending;
    int cur_line; const char *cur_src;
    jv retval; jstr *brk_label; int sc_flag; jobj *new_target; char *stack_lo; uint64_t task_start; uint32_t big_stack_size; char *big_stack; int in_task;
    jobj *g_console; struct jv *tmp_roots; jobj *cur_native; jobj *unhandled; int timer_depth;
    void *dom;                    /* estado del DOM (web_js_dom.c) */
    void *recache; uint32_t recache_pos;   /* cache de regex compiladas */
    const struct jdyn *dyn;       /* ganchos de propiedades dinamicas para objetos host (style, dataset, storage) */
    int timed_out;                /* halted por tiempo (se puede reanudar con la siguiente tarea) */
    struct jtry *base_try;        /* marco de excepcion mas externo (para abortar scripts detenidos) */
};

/* ---------- utilidades de valores ---------- */
#define JUNDEF ((jv){ JT_UNDEF, { 0 } })
static inline jv jnull(void) { jv v; v.t = JT_NULL; v.u.n = 0; return v; }
static inline jv jundef(void) { jv v; v.t = JT_UNDEF; v.u.n = 0; return v; }
static inline jv jbool(int b) { jv v; v.t = JT_BOOL; v.u.n = 0; v.u.b = b != 0; return v; }
static inline jv jnum(f64 n) { jv v; v.t = JT_NUM; v.u.n = n; return v; }
static inline jv jint(int64_t n) { jv v; v.t = JT_NUM; v.u.n = f64_from_i64(n); return v; }
static inline jv jstrv(jstr *s) { jv v; v.t = JT_STR; v.u.n = 0; v.u.s = s; return v; }
static inline jv jobjv(jobj *o) { jv v; v.t = JT_OBJ; v.u.n = 0; v.u.o = o; return v; }
#define IS_OBJ(v) ((v).t == JT_OBJ)
#define IS_STR(v) ((v).t == JT_STR)
#define IS_NUM(v) ((v).t == JT_NUM)
#define IS_UNDEF(v) ((v).t == JT_UNDEF)
#define IS_NULLISH(v) ((v).t <= JT_NULL)

/* memoria */
void *js_alloc(struct jrt *rt, uint32_t n);
/* cadenas */
jstr *js_str_new(struct jrt *rt, const char *s, uint32_t n);
jstr *js_str_cstr(struct jrt *rt, const char *s);
jstr *js_intern(struct jrt *rt, jstr *s);
jstr *js_intern_c(struct jrt *rt, const char *s);
jstr *js_intern_n(struct jrt *rt, const char *s, uint32_t n);
jstr *js_str_concat(struct jrt *rt, jstr *a, jstr *b);
int   js_str_eq(const jstr *a, const jstr *b);
/* objetos */
jobj *js_obj_new(struct jrt *rt, jobj *proto);
jobj *js_arr_new(struct jrt *rt, uint32_t cap);
void  js_arr_push(struct jrt *rt, jobj *a, jv v);
struct jprop *js_find_own(jobj *o, jstr *key);
jv    js_get(struct jrt *rt, jv obj, jstr *key);
jv    js_getv(struct jrt *rt, jv obj, jv key);
jv    js_get_c(struct jrt *rt, jv obj, const char *key);
void  js_put(struct jrt *rt, jv obj, jstr *key, jv val);
void  js_putv(struct jrt *rt, jv obj, jv key, jv val);
void  js_set_c(struct jrt *rt, jobj *o, const char *key, jv val);
void  js_def(struct jrt *rt, jobj *o, jstr *key, jv val, int flags);
void  js_def_acc(struct jrt *rt, jobj *o, jstr *key, jv getter, jv setter, int flags);
int   js_has(struct jrt *rt, jobj *o, jstr *key);
int   js_delete(struct jrt *rt, jobj *o, jstr *key);
jobj *js_native(struct jrt *rt, const char *name, jnative f, int nargs, int magic);
void  js_method(struct jrt *rt, jobj *o, const char *name, jnative f, int nargs, int magic);
void  js_getter(struct jrt *rt, jobj *o, const char *name, jnative get, jnative set, int magic);
/* conversion */
jstr *js_tostr(struct jrt *rt, jv v);
f64   js_tonum(struct jrt *rt, jv v);
int   js_truthy(jv v);
jv    js_toprim(struct jrt *rt, jv v, int hint_str);
jobj *js_toobj(struct jrt *rt, jv v);
const char *js_typeof(struct jrt *rt, jv v);
int   js_strict_eq(jv a, jv b);
int   js_loose_eq(struct jrt *rt, jv a, jv b);
int   js_same_zero(jv a, jv b);
int   js_to_index(struct jrt *rt, jv v, int64_t *out);       /* 1 si es indice entero >= 0 */
/* llamadas y errores */
jv    js_call(struct jrt *rt, jv fn, jv thisv, jv *args, int argc);
jv    js_construct(struct jrt *rt, jv fn, jv *args, int argc, jobj *newtarget);
int   js_is_callable(jv v);
void  js_throw(struct jrt *rt, jv exc) __attribute__((noreturn));
void  js_throw_err(struct jrt *rt, int kind, const char *fmt, ...) __attribute__((noreturn));
enum { E_ERROR = 0, E_TYPE, E_REFERENCE, E_RANGE, E_SYNTAX, E_EVAL, E_URI };
jobj *js_make_error(struct jrt *rt, int kind, const char *msg);
int   js_instanceof(struct jrt *rt, jv v, jv ctor);
/* iteracion */
int   js_iterate(struct jrt *rt, jv iterable, int (*cb)(struct jrt *rt, jv item, void *ud), void *ud);
jv    js_make_iter_result(struct jrt *rt, jv value, int done);
jobj *js_array_from(struct jrt *rt, jv *items, uint32_t n);
/* microtareas y promesas */
void  js_micro_enqueue(struct jrt *rt, jv fn, jv a, jv b, int kind, jobj *prom);
void  js_run_micro(struct jrt *rt);
jobj *js_promise_new(struct jrt *rt);
void  js_promise_resolve(struct jrt *rt, jobj *p, jv v);
void  js_promise_reject(struct jrt *rt, jobj *p, jv v);
jv    js_promise_then(struct jrt *rt, jobj *p, jv onf, jv onr);
jv    js_await(struct jrt *rt, jv v);
/* analisis */
struct jn *js_parse(struct jrt *rt, const char *src, uint32_t len, int *err_line, char *errmsg, uint32_t errmax);
jv    js_run_program(struct jrt *rt, struct jn *prog);
jv    js_eval_string(struct jrt *rt, const char *src, uint32_t len, int *ok);
/* biblioteca */
void  js_lib_init(struct jrt *rt);
void  js_dom_init(struct jrt *rt);
void  js_regex_init(struct jrt *rt);
jv    js_json_parse(struct jrt *rt, const char *s, uint32_t n);
jstr *js_json_stringify(struct jrt *rt, jv v, jv replacer, jv indent);
jstr *js_num_to_str(struct jrt *rt, f64 n);
jstr *js_inspect(struct jrt *rt, jv v, int depth);
void  js_log(struct jrt *rt, int level, const char *msg);
/* scope helpers (run) */
struct jvar *js_scope_lookup(struct scope *s, jstr *name);
void  js_step(struct jrt *rt);
void  js_check_stack(struct jrt *rt);
void  js_run_big(struct jrt *rt, void (*fn)(void *), void *arg);
/* puente DOM (web_js_dom.c) */
int   js_dom_dispatch(struct jrt *rt, struct wnode *target, const char *type, int x, int y, int key, const char *keystr, int mods);
void  js_dom_fire_load(struct jrt *rt);
void  js_dom_set_script(struct jrt *rt, struct wnode *n);
void  js_dom_set_url(struct jrt *rt, const char *url);
int   js_pump_timers(struct jrt *rt, int *ran);
jobj *js_regex_new(struct jrt *rt, jstr *src, jstr *flags);
uint64_t web_epoch_ms(void);
/* codigos de operacion de String (lib2 y regex) */
enum { S_CHARAT, S_CHARCODEAT, S_CODEPOINTAT, S_AT, S_INDEXOF, S_LASTINDEXOF, S_INCLUDES, S_STARTSWITH, S_ENDSWITH, S_SLICE, S_SUBSTRING, S_SUBSTR, S_UPPER, S_LOWER, S_TRIM, S_TRIMSTART, S_TRIMEND,
       S_SPLIT, S_REPLACE, S_REPLACEALL, S_MATCH, S_MATCHALL, S_SEARCH, S_PADSTART, S_PADEND, S_REPEAT, S_CONCAT, S_LOCALECMP, S_NORMALIZE, S_TOSTRING, S_ITER, S_ANCHOR };
#define JS_STEP(rt) do { if (++(rt)->steps > (rt)->step_limit) js_step(rt); } while (0)
extern uint64_t web_now_ms(void);
#endif
