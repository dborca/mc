/*
 *  Recursive descent expression calculator
 *
 *  Copyright (c) 2021 Daniel Borca
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */


#include <config.h>
#include <ctype.h>
#include <stdio.h>
#ifdef HAVE_MAIN
#include <assert.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

#define _(string) string
#define N_(string) string
#define MSG_ERROR ((char *)-1)

enum {
   D_NORMAL = 0,
   D_ERROR  = 1
};

static void
message(int flags, char *title, const char *fmt, ...)
{
    va_list ap;
    fprintf(stderr, "%s: ", (title == MSG_ERROR) ? _("Error") : title);
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fprintf(stderr, "\n");
    (void)flags;
}
#else
#include "global.h"
#include "wtools.h"
#include "charsets.h"
#endif

#ifdef HAVE_LONG_LONG
#define LONG long long
#define LFMT "ll"
int eval_prec_scaler = 4;
#else
#define LONG long
#define LFMT "l"
int eval_prec_scaler = 3;
#endif

#define SET_SCALER() \
    do { \
        FIXED_ONE = 1; \
        if (signed_op >= 2) { \
            int i; \
            if (eval_prec_scaler < 1) { \
                eval_prec_scaler = 1; \
            } \
            if (eval_prec_scaler > 9) { \
                eval_prec_scaler = 9; \
            } \
            for (i = 0; i < eval_prec_scaler; i++) { \
                FIXED_ONE *= 10; \
            } \
        } \
    } while (0)

static int signed_op = 0;

static LONG FIXED_ONE = 1;

static const char *eval_mode_str[] = {
    N_("Unsigned"),
    N_(" Signed "),
    N_(" Scaled "),
    N_(" Scale2 "),
};

static const char *eval_mode_hot[] = {
    N_("Un&signed"),
    N_(" &Signed "),
    N_(" &Scaled "),
    N_(" &Scale2 "),
};

/*** util *******************************************************************/

#ifdef EVAL_DEBUG
#define DBG(fmt, ...) fprintf(stderr, fmt, ##__VA_ARGS__)
#define DBG2(fmt, ...) /*fprintf(stderr, fmt, ##__VA_ARGS__)*/
static int logindent = 0;
#define ENTER() DBG2("%*c %s(%s)\n", 2 * logindent++, ';', __FUNCTION__, token.sym)
#define LEAVE() --logindent
#else
#define DBG(fmt, ...)
#define DBG2(fmt, ...)
#define ENTER()
#define LEAVE()
#endif

static void *
xmalloc(size_t size)
{
    void *p = malloc(size);
    assert(p);
    return p;
}

static char *
xstrdup(const char *s)
{
    void *p = strdup(s);
    assert(p);
    return p;
}

static char *
xstrndup(const char *s, size_t len)
{
    char *p = malloc(len + 1);
    assert(p);
    memcpy(p, s, len);
    p[len] = '\0';
    return p;
}

static char *
ftoa(LONG n)
{
    static char tmp[32];
    char *p = tmp;
    unsigned LONG q;
    if (n < 0) {
        *p++ = '-';
        n = -n;
    }
    q = (unsigned LONG)n / FIXED_ONE;
    snprintf(p, sizeof(tmp) - 1, "%"LFMT"u.%0*"LFMT"d", q, eval_prec_scaler, n - q * FIXED_ONE);
    if (signed_op > 2) {
        char *dot = strchr(tmp, '.');
        char *end = strchr(dot, '\0');
        char *sep = dot++;
        while (end > dot + 1 && end[-1] == '0') {
            end--;
        }
        while ((sep -= 3) > p) {
            memmove(sep + 1, sep, end++ - sep);
            *sep = '\'';
        }
        *end = '\0';
    }
    return tmp;
}

static LONG
strton(const char *str, char **endptr, int base)
{
    const char *p = str;
    if (FIXED_ONE > 1) {
        LONG n;
        if (base && base != 10) {
            *endptr = (char *)str;
            return 0;
        }
        n = strtoull(str, endptr, 10) * FIXED_ONE;
        if (**endptr == '.') {
            LONG div = 1;
            while (isdigit(*++(*endptr))) {
                LONG ch = **endptr - '0';
                if (div > FIXED_ONE / 10) {
                    n += signed_op > 2 && ch >= 5;
                    while (isdigit(*++(*endptr))) {
                    }
                    break;
                }
                div *= 10;
                n += ch * (FIXED_ONE / div);
            }
        }
        return n;
    }
    if ((base | 2) == 2 && str[0] == '0' && (str[1] | 0x20) == 'b' && (str[2] | 1) == '1') {
        p += 2;
        base = 2;
    }
    return strtoull(p, endptr, base);
}

/*** lexer ******************************************************************/

#define MAXTOK 128

#define isalph_(c) (isalpha(c) || ((c) == '_'))
#define isalnu_(c) (isalnum(c) || ((c) == '_'))

enum TOKTYPE {
    T_INVALID,

    T_POW,
    T_NOT,
    T_MUL,
    T_DIV,
    T_MOD,
    T_ADD,
    T_SUB,
    T_SHL,
    T_SHR,
    T_ASR,
    T_AND,
    T_XOR,
    T_OR,

    T_EQ,
    T_NE,
    T_LT,
    T_GT,
    T_LE,
    T_GE,

    T_LNOT,
    T_LAND,
    T_LOR,

    T_QUEST,
    T_COLON,

    T_COMMA,
    T_OPENBRACE,
    T_CLOSEBRACE,
    T_ASSIGN,

    T_DEFINE,
    T_UNDEF,
    T_UNSET,

    T_ID,
    T_INT,

    T_EOI
};

static struct TOKEN {
    enum TOKTYPE type;
    const char *sym;
} token;

static char **tokens = NULL;
static int ntok, itok;

static int
tokenize(const char *s)
{
    const char *last;

    if (!tokens) {
        tokens = xmalloc(MAXTOK * sizeof(char *));
    }

    for (ntok = 0; *s; s++) {
        char *p;
        if (*s == ';') {
            break;
        }
        if (isspace(*s)) {
            continue;
        }
        last = s;
#if 1
#define BEG '\"'
#define END '\"'
        if (*s == BEG) {
            do {
                s++;
            } while (*s && *s != END);
            if (*s == '\0') {
                goto err_syntax;
            }
            continue;
        }
#endif

        switch (*s) {
            case '&':
            case '|':
            case '=':
            case '*':
                if (s[1] == s[0]) {
                    s++;
                }
            case '~':
            case '/':
            case '%':
            case '+':
            case '-':
            case '^':
            case ',':
            case '(':
            case ')':
            case '?':
            case ':':
                break;
            case '<':
            case '>':
                if (s[1] == s[0]) {
                    s++;
                    if (s[1] == s[0]) {
                        s++;
                    }
                    break;
                }
            case '!':
                if (s[1] == '=') {
                    s++;
                }
                break;
            case '#':
                while (isalph_(s[1])) {
                    s++;
                }
                /*if (strncmp(last, "#define", s + 1 - last) &&
                    strncmp(last, "#undef", s + 1 - last) &&
                    strncmp(last, "#unset", s + 1 - last)) {
                    goto err_syntax;
                }*/
                break;
            default:
            if (isdigit(*s)) {
                strton(s, &p, 0);
                if (*p == 'k' || *p == 'M' || *p == 'G' || (signed_op > 2 && *p == '%')) {
                    p++;
                }
                if (isalnu_(*p)) {
                    goto err_syntax;
                }
                s = p - 1;
            } else if (isalph_(*s)) {
                while (isalnu_(s[1])) {
                    s++;
                }
            } else {
                goto err_syntax;
            }
        }
        if (ntok >= MAXTOK) {
            message(D_ERROR, MSG_ERROR, _("too many tokens"));
            goto err;
        }
        p = xstrndup(last, s + 1 - last);
        tokens[ntok++] = p;
    }
    itok = 0;
    return ntok;
  err_syntax:
    for (s = last; *s && !isspace(*s); s++) {
    }
    message(D_ERROR, MSG_ERROR, _("invalid token %.*s"), (int)(s - last), last);
  err:
    while (--ntok >= 0) {
        free(tokens[ntok]);
    }
    return -1;
}

static void
free_tokens(int full)
{
    while (--ntok >= 0) {
        free(tokens[ntok]);
    }
    if (full) {
        free(tokens);
        tokens = NULL;
    }
}

static enum TOKTYPE
eval_token(const char *s)
{
    enum TOKTYPE type;
    if (!strcmp(s, "**")) {
        type = T_POW;
    } else if (!strcmp(s, "~")) {
        type = T_NOT;
    } else if (!strcmp(s, "*")) {
        type = T_MUL;
    } else if (!strcmp(s, "/")) {
        type = T_DIV;
    } else if (!strcmp(s, "%")) {
        type = T_MOD;
    } else if (!strcmp(s, "+")) {
        type = T_ADD;
    } else if (!strcmp(s, "-")) {
        type = T_SUB;
    } else if (!strcmp(s, "<<<")) {
        type = T_SHL;
    } else if (!strcmp(s, ">>>")) {
        type = T_SHR;
    } else if (!strcmp(s, "<<")) {
        type = T_SHL;
    } else if (!strcmp(s, ">>")) {
        type = T_ASR;
    } else if (!strcmp(s, "<=")) {
        type = T_LE;
    } else if (!strcmp(s, ">=")) {
        type = T_GE;
    } else if (!strcmp(s, "<")) {
        type = T_LT;
    } else if (!strcmp(s, ">")) {
        type = T_GT;
    } else if (!strcmp(s, "&&")) {
        type = T_LAND;
    } else if (!strcmp(s, "||")) {
        type = T_LOR;
    } else if (!strcmp(s, "==")) {
        type = T_EQ;
    } else if (!strcmp(s, "?")) {
        type = T_QUEST;
    } else if (!strcmp(s, ":")) {
        type = T_COLON;
    } else if (!strcmp(s, "!=")) {
        type = T_NE;
    } else if (!strcmp(s, "!")) {
        type = T_LNOT;
    } else if (!strcmp(s, "&")) {
        type = T_AND;
    } else if (!strcmp(s, "^")) {
        type = T_XOR;
    } else if (!strcmp(s, "|")) {
        type = T_OR;
    } else if (!strcmp(s, ",")) {
        type = T_COMMA;
    } else if (!strcmp(s, "(")) {
        type = T_OPENBRACE;
    } else if (!strcmp(s, ")")) {
        type = T_CLOSEBRACE;
    } else if (!strcmp(s, "=")) {
        type = T_ASSIGN;
    } else if (!strcmp(s, "#define")) {
        type = T_DEFINE;
    } else if (!strcmp(s, "#undef")) {
        type = T_UNDEF;
    } else if (!strcmp(s, "#unset")) {
        type = T_UNSET;
    } else if (*s == '#') {
        type = T_INVALID;
    } else if (isdigit(*s)) {
        type = T_INT;
    } else {
        type = T_ID;
    }
    return type;
}

static void
next_token(void)
{
    token.sym = NULL;
    token.type = T_EOI;
    if (itok < ntok) {
        token.sym = tokens[itok++];
        token.type = eval_token(token.sym);
    }
}

enum TOKTYPE
peek_token(void)
{
    enum TOKTYPE type = T_EOI;
    if (itok < ntok) {
        type = eval_token(tokens[itok]);
    }
    return type;
}

static size_t
copy_tokens(char **copy)
{
    int i, n = ntok - itok;
    for (i = itok; i < ntok; i++) {
        *copy++ = xstrdup(tokens[i]);
    }
    return n;
}

static void
replace_tokens(int *old_itok, int *old_ntok, char **old_tokens, struct TOKEN *old_tok, char **new_tokens, int new_ntok)
{
    *old_itok = itok;
    *old_ntok = ntok;
    *old_tok = token;
    memcpy(old_tokens, tokens, new_ntok * sizeof(char *));
    memcpy(tokens, new_tokens, new_ntok * sizeof(char *));
    itok = 0;
    ntok = new_ntok;
    next_token();
}

static void
restore_tokens(int old_itok, int old_ntok, char **old_tokens, struct TOKEN *old_tok)
{
    memcpy(tokens, old_tokens, ntok * sizeof(char *));
    token = *old_tok;
    itok = old_itok;
    ntok = old_ntok;
}

/*** eval *******************************************************************/

#define IS(t) (token.type == (t))

struct node {
    struct node *next;
    LONG val;
};

static struct tuple_t {
    struct tuple_t *next;
    char *name;
    LONG value;
} *vars = NULL;

static int
varlist(const char *name, LONG *oldval, LONG newval, int existing)
{
    struct tuple_t *n;
    if (!name) {
        while (vars) {
            n = vars->next;
            free(vars->name);
            free(vars);
            vars = n;
        }
        return 0;
    }
    for (n = vars; n; n = n->next) {
        if (!strcmp(n->name, name)) {
            break;
        }
    }
    if (oldval) {
        if (!n) {
            return 0;
        }
        *oldval = n->value;
        return 1;
    }
    if (!n) {
        if (existing) {
            return 0;
        }
        n = xmalloc(sizeof(struct tuple_t));
        n->name = xstrdup(name);
        n->next = vars;
        vars = n;
    }
    n->value = newval;
    return 1;
}

static struct func_t {
    struct func_t *next;
    char *name;
    char *args[MAXTOK];
    size_t narg;
    char *expr[MAXTOK];
    size_t ntok;
    int signed_op;
} *user = NULL;

static int call_depth = 0;

static unsigned LONG
sqrti(unsigned LONG n, unsigned LONG *r)
{
    unsigned LONG root = 0;
    unsigned LONG remainder = n;
    unsigned LONG place = (unsigned LONG)1 << (sizeof(n) * 8 - 2);

    while (place > remainder) {
        place = place >> 2;
    }
    while (place) {
        if (remainder >= root + place) {
            remainder -= root + place;
            root += place << 1;
        }
        root = root >> 1;
        place = place >> 2;
    }

    *r = remainder;
    return root;
}

static unsigned LONG
cbrti(unsigned LONG x)
{
    int s;
    unsigned LONG b;
    unsigned int y = 0;
    for (s = sizeof(x) * 8 / 3 * 3; s >= 0; s -= 3) {
        y += y;
        b = 3 * y * ((unsigned LONG)y + 1) + 1;
        if ((x >> s) >= b) {
            x -= b << s;
            y++;
        }
    }
    return y;
}

static unsigned LONG
gcd(unsigned LONG a, unsigned LONG b)
{
    int k;

    /* gcd(0, b) == b; gcd(a, 0) == a; gcd(0, 0) == 0 */
    if (a == 0) {
        return b;
    }
    if (b == 0) {
        return a;
    }

    /* find K, the greatest power of 2 that divides both a and b */
    for (k = 0; ((a | b) & 1) == 0; k++) {
        a >>= 1;
        b >>= 1;
    }

    /* divide a by 2 until it becomes odd */
    while ((a & 1) == 0) {
        a >>= 1;
    }

    /* a is always odd here */
    do {
        /* if b is even, remove all factor of 2 in b */
        while ((b & 1) == 0) {
            b >>= 1;
        }

        /* a and b are both odd. Swap if necessary so a <= b, then set b = b - a (which is even) */
        if (a > b) {
            LONG t = a;
            a = b;
            b = t;
        }

        b -= a;
    } while (b);

    /* restore common factors of 2 */
    return a << k;
}

static unsigned LONG
bfill(unsigned LONG n)
{
    size_t i;
    if (n) {
        n--;
        for (i = 1; i < sizeof(n) * 8; i *= 2) {
            n |= n >> i;
        }
    }
    return n;
}

static LONG
fn_abs(const char *func, struct node *args, int *ok)
{
    LONG n = args->val;
    if (n < 0) {
        n = -n;
    }
    return n;
}

static LONG
fn_sqrt(const char *func, struct node *args, int *ok)
{
    unsigned LONG r;
    LONG n = args->val;
    if (n < 0 && signed_op) {
        message(D_ERROR, MSG_ERROR, _("negative square root"));
        *ok = 0;
        return 0;
    }
    return sqrti(n * FIXED_ONE, &r);
}

static LONG
fn_cbrt(const char *func, struct node *args, int *ok)
{
    LONG n = args->val;
    if (n < 0 && signed_op) {
        return -cbrti(-n * FIXED_ONE * FIXED_ONE);
    }
    return cbrti(n * FIXED_ONE * FIXED_ONE);
}

static LONG
fn_min(const char *func, struct node *args, int *ok)
{
    struct node *p;
    LONG n = args->val;
    for (p = args->next; p; p = p->next) {
        if (signed_op) {
            if (n > p->val) {
                n = p->val;
            }
        } else {
            if ((unsigned LONG)n > (unsigned LONG)p->val) {
                n = p->val;
            }
        }
    }
    return n;
}

static LONG
fn_max(const char *func, struct node *args, int *ok)
{
    struct node *p;
    LONG n = args->val;
    for (p = args->next; p; p = p->next) {
        if (signed_op) {
            if (n < p->val) {
                n = p->val;
            }
        } else {
            if ((unsigned LONG)n < (unsigned LONG)p->val) {
                n = p->val;
            }
        }
    }
    return n;
}

static LONG
fn_avg(const char *func, struct node *args, int *ok)
{
    int i;
    struct node *p;
    LONG n = 0;
    for (i = 0, p = args; p; p = p->next, i++) {
        n += p->val;
    }
    return n / i;
}

static LONG
fn_floor(const char *func, struct node *args, int *ok)
{
    unsigned LONG a = args->val;
    unsigned LONG n = args->next->val;
    if (!a) {
        message(D_ERROR, MSG_ERROR, _("division by zero"));
        *ok = 0;
    } else {
        n = n / a * a;
    }
    return n;
}

static LONG
fn_ceil(const char *func, struct node *args, int *ok)
{
    unsigned LONG a = args->val;
    unsigned LONG n = args->next->val;
    if (!a) {
        message(D_ERROR, MSG_ERROR, _("division by zero"));
        *ok = 0;
    } else {
        n += a - 1;
        n = n / a * a;
    }
    return n;
}

static LONG
fn_gcd(const char *func, struct node *args, int *ok)
{
    return gcd(args->val, args->next->val);
}

static LONG
fn_lcm(const char *func, struct node *args, int *ok)
{
    unsigned LONG a = args->val;
    unsigned LONG b = args->next->val;
    if (a == 0 && b == 0) {
        return 0;
    }
    return a / gcd(a, b) * b;
}

static LONG
fn_floor2(const char *func, struct node *args, int *ok)
{
    unsigned LONG n = args->val;
    if (n) {
        unsigned LONG k = n;
        n = bfill(n);
        if (n + 1 == 0 || n + 1 > k) {
            n /= 2;
        }
        n++;
    }
    return n;
}

static LONG
fn_ceil2(const char *func, struct node *args, int *ok)
{
    return bfill(args->val) + 1;
}

static LONG
fn_bits(const char *func, struct node *args, int *ok)
{
    int bits;
    unsigned LONG n = args->val;
    for (bits = 0; n; bits++) {
        n >>= 1;
    }
    return bits;
}

static LONG
fn_sext(const char *func, struct node *args, int *ok)
{
    LONG n = args->next->val;
    unsigned LONG bits = args->val;
    if (bits < 1 || bits > sizeof(LONG) * 8) {
        message(D_ERROR, MSG_ERROR, _("invalid number of bits %"LFMT"u in function '%s'"), bits, func);
        *ok = 0;
        return 0;
    }
    bits = sizeof(LONG) * 8 - bits;
    return n << bits >> bits;
}

static LONG R_assignment_exp(int *ok, int existing, int dry);

static LONG
fn_default(const char *func, struct node *args, int *ok)
{
    struct func_t *f = user;
    if (call_depth > 1000) {
        message(D_ERROR, MSG_ERROR, _("nested call too deep"));
        *ok = 0;
        return 0;
    }
    while (f) {
        if (!strcmp(f->name, func)) {
            LONG n = 0;
            size_t i;
            struct tuple_t *old_vars;

            if (f->signed_op != signed_op) {
                message(D_ERROR, MSG_ERROR, _("function '%s' was defined in a different mode"), func);
                *ok = 0;
                return 0;
            }

            /* XXX works as long as the function body does not create new vars */
            for (old_vars = vars, i = f->narg; i > 0 && args; args = args->next) {
                struct tuple_t *v = xmalloc(sizeof(struct tuple_t));
                v->name = xstrdup(f->args[--i]);
                v->value = args->val;
                v->next = vars;
                vars = v;
            }

            if (i || args) {
                message(D_ERROR, MSG_ERROR, _("function '%s' requires %zu parameters"), func, f->narg);
                *ok = 0;
            } else {
                int itok, ntok;
                struct TOKEN tok;
                char *tokens[MAXTOK];
                replace_tokens(&itok, &ntok, tokens, &tok, f->expr, f->ntok);
                call_depth++;
                n = R_assignment_exp(ok, 1, 0);
                call_depth--;
                restore_tokens(itok, ntok, tokens, &tok);
            }

            while (vars != old_vars) {
                struct tuple_t *v = vars;
                vars = vars->next;
                free(v->name);
                free(v);
            }
            return n;
        }
        f = f->next;
    }
    message(D_ERROR, MSG_ERROR, _("unknown function '%s'"), func);
    *ok = 0;
    return 0;
}

#define FUN_VARARG 1
#define FUN_NOFRAC 2

static const struct {
    const char *name;
    int nargs;
    int flags;
    LONG (*call)(const char *name, struct node *args, int *ok);
} builtins[] = {
    { "abs",    1, 0,          fn_abs },
    { "sqrt",   1, 0,          fn_sqrt },
    { "cbrt",   1, 0,          fn_cbrt },
    { "min",    2, FUN_VARARG, fn_min },
    { "max",    2, FUN_VARARG, fn_max },
    { "avg",    1, FUN_VARARG, fn_avg },
    { "floor",  2, 0,          fn_floor },
    { "ceil",   2, 0,          fn_ceil },
    { "gcd",    2, 0,          fn_gcd },
    { "lcm",    2, 0,          fn_lcm },
    { "floor2", 1, FUN_NOFRAC, fn_floor2 },
    { "ceil2",  1, FUN_NOFRAC, fn_ceil2 },
    { "bits",   1, FUN_NOFRAC, fn_bits },
    { "sext",   2, FUN_NOFRAC, fn_sext },
    { NULL,    -1, 0,          fn_default }
};

static LONG
builtin_call(const char *func, struct node *args, int *ok, int dry)
{
    size_t i;
    int n, nargs;
    struct node *p;
    for (i = 0; builtins[i].name && (strcmp(func, builtins[i].name) || (FIXED_ONE > 1 && builtins[i].flags & FUN_NOFRAC)); i++) {
        continue;
    }
    for (n = 0, p = args; p; p = p->next, n++) {
        continue;
    }
    nargs = builtins[i].nargs;
    if (nargs >= 0 && nargs != n) {
        if (builtins[i].flags & FUN_VARARG) {
            if (nargs > n) {
                message(D_ERROR, MSG_ERROR, _("function '%s' requires at least %d arguments"), func, nargs);
                *ok = 0;
                return 0;
            }
        } else {
            message(D_ERROR, MSG_ERROR, _("function '%s' requires %d arguments"), func, nargs);
            *ok = 0;
            return 0;
        }
    }
    return dry ? 0 : builtins[i].call(func, args, ok);
}

static void
expect(const char *what)
{
    const char *sym = token.sym;
    /*if (IS(T_INVALID)) {
        return;
    }*/
    if (IS(T_EOI)) {
        sym = "end of input";
    }
    message(D_ERROR, MSG_ERROR, _("expected %s before %s"), what, sym);
}

static LONG R_rvalue_exp(int *ok, int dry);

static LONG
R_pow_exp(int *ok, int dry)
{
    LONG n;
    ENTER();
    n = R_rvalue_exp(ok, dry);
    if (*ok && (IS(T_POW))) {
        LONG k;
        next_token(); /* skip '**' */
        k = R_pow_exp(ok, dry);
        if (*ok) {
            LONG p;
            int neg = 0;
            if (k < 0) {
                neg = 1;
                k = -k;
            }
            p = n;
            n = FIXED_ONE;
            if (FIXED_ONE > 1) {
                LONG q = k;
                k /= FIXED_ONE;
                if (k * FIXED_ONE != q) {
                    message(D_ERROR, MSG_ERROR, _("fraction powers not supported"));
                    *ok = 0;
                    n = 0;
                }
            }
            while (n) {
                if (k & 1) {
                    n = n * p / FIXED_ONE;
                }
                k >>= 1;
                if (k == 0) {
                    break;
                }
                p = p * p / FIXED_ONE;
            }
            if (*ok && neg) {
                if (!n) {
                    message(D_ERROR, MSG_ERROR, _("division by zero"));
                    *ok = 0;
                } else if (signed_op) {
                    n = FIXED_ONE * FIXED_ONE / n;
                } else {
                    n = (unsigned LONG)FIXED_ONE * FIXED_ONE / n;
                }
            }
        }
    }
    LEAVE();
    return n;
}

static LONG
R_mul_exp(int *ok, int dry)
{
    LONG n;
    ENTER();
    n = R_pow_exp(ok, dry);
    while (*ok && (IS(T_MUL) || IS(T_DIV) || IS(T_MOD))) {
        LONG v;
        enum TOKTYPE op = token.type;
        next_token(); /* skip '*' */
        v = R_pow_exp(ok, dry);
        if (*ok) {
            if (op == T_MUL || dry) {
                n = n * v / FIXED_ONE;
            } else if (v) {
                if (op == T_DIV) {
                    if (signed_op) {
                        n = n * FIXED_ONE / v;
                    } else {
                        n = (unsigned LONG)n * FIXED_ONE / v;
                    }
                } else {
                    if (signed_op) {
                        n %= v;
                    } else {
                        n = (unsigned LONG)n % v;
                    }
                }
            } else {
                message(D_ERROR, MSG_ERROR, _("division by zero"));
                *ok = 0;
            }
        }
    }
    LEAVE();
    return n;
}

static LONG
R_add_exp(int *ok, int dry)
{
    LONG n;
    ENTER();
    n = R_mul_exp(ok, dry);
    while (*ok && (IS(T_ADD) || IS(T_SUB))) {
        LONG v;
        enum TOKTYPE op = token.type;
        next_token(); /* skip '+' */
        v = R_mul_exp(ok, dry);
        if (*ok) {
            if (op == T_ADD) {
                n += v;
            } else {
                n -= v;
            }
        }
    }
    LEAVE();
    return n;
}

static LONG
R_shift_exp(int *ok, int dry)
{
    LONG n;
    ENTER();
    n = R_add_exp(ok, dry);
    while (*ok && (IS(T_SHL) || IS(T_SHR) || IS(T_ASR))) {
        LONG v;
        enum TOKTYPE op = token.type;
        if (FIXED_ONE > 1) {
            message(D_ERROR, MSG_ERROR, _("bitwise op not supported"));
            *ok = 0;
            break;
        }
        next_token(); /* skip '<<' */
        v = R_add_exp(ok, dry);
        if (*ok) {
            if (v < 0) {
                message(D_ERROR, MSG_ERROR, _("negative shift count"));
                *ok = 0;
            } else {
                v &= sizeof(LONG) * 8 - 1;
                switch (op) {
                    case T_SHL:
                        n <<= v;
                        break;
                    case T_ASR:
                        if (signed_op) {
                            n >>= v;
                            break;
                        }
                    case T_SHR:
                        n = (unsigned LONG)n >> v;
                        break;
                    default:; /* silence */
                }
            }
        }
    }
    LEAVE();
    return n;
}

static LONG
R_rel_exp(int *ok, int dry)
{
    LONG n;
    ENTER();
    n = R_shift_exp(ok, dry);
    while (*ok && (IS(T_LT) || IS(T_GT) || IS(T_LE) || IS(T_GE))) {
        LONG v;
        enum TOKTYPE op = token.type;
        next_token(); /* skip '>=' */
        v = R_shift_exp(ok, dry);
        if (*ok) {
            switch (op) {
                case T_LT:
                    if (signed_op) {
                        n = n < v;
                    } else {
                        n = (unsigned LONG)n < (unsigned LONG)v;
                    }
                    break;
                case T_GT:
                    if (signed_op) {
                        n = n > v;
                    } else {
                        n = (unsigned LONG)n > (unsigned LONG)v;
                    }
                    break;
                case T_LE:
                    if (signed_op) {
                        n = n <= v;
                    } else {
                        n = (unsigned LONG)n <= (unsigned LONG)v;
                    }
                    break;
                case T_GE:
                    if (signed_op) {
                        n = n >= v;
                    } else {
                        n = (unsigned LONG)n >= (unsigned LONG)v;
                    }
                    break;
                default:; /* silence */
            }
            n *= FIXED_ONE;
        }
    }
    LEAVE();
    return n;
}

static LONG
R_eq_exp(int *ok, int dry)
{
    LONG n;
    ENTER();
    n = R_rel_exp(ok, dry);
    while (*ok && (IS(T_EQ) || IS(T_NE))) {
        LONG v;
        enum TOKTYPE op = token.type;
        next_token(); /* skip '==' */
        v = R_rel_exp(ok, dry);
        if (*ok) {
            n = op == T_NE ^ n == v ? FIXED_ONE : 0;
        }
    }
    LEAVE();
    return n;
}

static LONG
R_and_exp(int *ok, int dry)
{
    LONG n;
    ENTER();
    n = R_eq_exp(ok, dry);
    while (*ok && IS(T_AND)) {
        LONG v;
        if (FIXED_ONE > 1) {
            message(D_ERROR, MSG_ERROR, _("bitwise op not supported"));
            *ok = 0;
            break;
        }
        next_token(); /* skip '&' */
        v = R_eq_exp(ok, dry);
        if (*ok) {
            n &= v;
        }
    }
    LEAVE();
    return n;
}

static LONG
R_xor_exp(int *ok, int dry)
{
    LONG n;
    ENTER();
    n = R_and_exp(ok, dry);
    while (*ok && IS(T_XOR)) {
        LONG v;
        if (FIXED_ONE > 1) {
            message(D_ERROR, MSG_ERROR, _("bitwise op not supported"));
            *ok = 0;
            break;
        }
        next_token(); /* skip '^' */
        v = R_and_exp(ok, dry);
        if (*ok) {
            n ^= v;
        }
    }
    LEAVE();
    return n;
}

static LONG
R_or_exp(int *ok, int dry)
{
    LONG n;
    ENTER();
    n = R_xor_exp(ok, dry);
    while (*ok && IS(T_OR)) {
        LONG v;
        if (FIXED_ONE > 1) {
            message(D_ERROR, MSG_ERROR, _("bitwise op not supported"));
            *ok = 0;
            break;
        }
        next_token(); /* skip '|' */
        v = R_xor_exp(ok, dry);
        if (*ok) {
            n |= v;
        }
    }
    LEAVE();
    return n;
}

static LONG
R_land_exp(int *ok, int dry)
{
    LONG n;
    ENTER();
    n = R_or_exp(ok, dry);
    if (*ok && IS(T_LAND)) {
        LONG v;
        next_token(); /* skip '&&' */
        v = R_or_exp(ok, dry || !n);
        if (*ok) {
            n = n && v ? FIXED_ONE : 0;
        }
    }
    LEAVE();
    return n;
}

static LONG
R_lor_exp(int *ok, int dry)
{
    LONG n;
    ENTER();
    n = R_land_exp(ok, dry);
    if (*ok && IS(T_LOR)) {
        LONG v;
        next_token(); /* skip '||' */
        v = R_land_exp(ok, dry || n);
        if (*ok) {
            n = n || v ? FIXED_ONE : 0;
        }
    }
    LEAVE();
    return n;
}

static LONG
R_cond_exp(int *ok, int dry)
{
    LONG n, pri, sec;
    ENTER();
    n = R_lor_exp(ok, dry);
    if (*ok && IS(T_QUEST)) {
        next_token(); /* skip '?' */
        pri = R_assignment_exp(ok, 1, dry || !n);
        if (*ok) {
            if (!IS(T_COLON)) {
                expect("':'");
                *ok = 0;
            } else {
                next_token(); /* skip ':' */
                sec = R_cond_exp(ok, dry || n);
                if (*ok) {
                    n = n ? pri : sec;
                }
            }
        }
    }
    LEAVE();
    return n;
}

static struct node *
R_argument_exp_list(int *ok, int dry)
{
    struct node *p = NULL;
    LONG n;
    ENTER();
    n = R_cond_exp(ok, dry);
    while (*ok) {
        struct node *q = xmalloc(sizeof(struct node));
        q->val = n;
        q->next = p;
        p = q;
        if (!IS(T_COMMA)) {
            break;
        }
        next_token(); /* skip ',' */
        n = R_cond_exp(ok, dry);
    }
    LEAVE();
    return p;
}

static LONG
R_rvalue_exp(int *ok, int dry)
{
    LONG n = 0;
    ENTER();
    if (IS(T_ID) && peek_token() == T_OPENBRACE) {
        struct node *p, *args = NULL;
        char *func = xstrdup(token.sym);
        next_token(); /* skip ID */
        next_token(); /* skip '(' */
        if (!IS(T_CLOSEBRACE)) {
            args = R_argument_exp_list(ok, dry);
        }
        if (*ok) {
            if (!IS(T_CLOSEBRACE)) {
                expect("')'");
                *ok = 0;
            } else {
                next_token(); /* skip ')' */
                n = builtin_call(func, args, ok, dry);
            }
        }
        while (args) {
            p = args;
            args = args->next;
            free(p);
        }
        free(func);
    } else if (IS(T_OPENBRACE)) {
        next_token(); /* skip '(' */
        n = R_cond_exp(ok, dry);
        if (*ok) {
            if (!IS(T_CLOSEBRACE)) {
                expect("')'");
                *ok = 0;
            } else {
                next_token(); /* skip ')' */
            }
        }
    } else if (IS(T_ADD)) {
        next_token(); /* skip '+' */
        n = R_rvalue_exp(ok, dry);
    } else if (IS(T_SUB)) {
        next_token(); /* skip '-' */
        n = -R_rvalue_exp(ok, dry);
    } else if (IS(T_NOT)) {
        next_token(); /* skip '~' */
        n = ~R_rvalue_exp(ok, dry);
        if (FIXED_ONE > 1) {
            message(D_ERROR, MSG_ERROR, _("bitwise op not supported"));
            *ok = 0;
        }
    } else if (IS(T_LNOT)) {
        next_token(); /* skip '!' */
        n = R_rvalue_exp(ok, dry) ? 0 : FIXED_ONE;
    } else if (IS(T_ID)) {
        if (!dry && !varlist(token.sym, &n, 0, 0)) {
            message(D_ERROR, MSG_ERROR, _("unknown identifier: '%s'"), token.sym);
            *ok = 0;
        }
        next_token(); /* skip ID */
    } else if (IS(T_INT)) {
        char *bp;
        n = strton(token.sym, &bp, 0);
        switch (*bp) {
            case '%':
                n /= 100;
                break;
            case 'G':
                n *= 1024;
            case 'M':
                n *= 1024;
            case 'k':
                n *= 1024;
        }
        next_token(); /* skip number */
    } else {
        expect("expression");
        *ok = 0;
    }
    LEAVE();
    return dry ? 0 : n;
}

static LONG
R_assignment_exp(int *ok, int existing, int dry)
{
    LONG n;
    ENTER();
    while (1) {
        char *lval = NULL;
        /* we cannot tell here if we have lvalue or rvalue, so use magic */
        if (IS(T_ID) && peek_token() == T_ASSIGN) {
            lval = xstrdup(token.sym);
            next_token(); /* skip ID */
            next_token(); /* skip '=' */
        }
        n = R_cond_exp(ok, dry);
        if (*ok && lval && !dry) {
            /* XXX should ban internal functions */
            if (!varlist(lval, NULL, n, existing)) {
                message(D_ERROR, MSG_ERROR, _("unknown identifier: '%s'"), lval);
                *ok = 0;
            }
        }
        free(lval);
        if (*ok == 0 || !IS(T_COMMA)) {
            break;
        }
        next_token(); /* skip ',' */
    }
    LEAVE();
    return n;
}

static void
kill_func(struct func_t *f)
{
    size_t i;
    for (i = 0; i < f->ntok; i++) {
        free(f->expr[i]);
    }
    for (i = 0; i < f->narg; i++) {
        free(f->args[i]);
    }
    free(f->name);
    free(f);
}

static int
add_func(int *ok)
{
    size_t i;
    struct func_t *f, *prev;

    if (!IS(T_ID) || peek_token() != T_OPENBRACE) {
        expect("function definition");
        *ok = 0;
        return 0;
    }

    for (i = 0; builtins[i].name; i++) {
        if (!strcmp(builtins[i].name, token.sym)) {
            message(D_ERROR, MSG_ERROR, _("cannot alter builtin '%s'"), token.sym);
            *ok = 0;
            return 0;
        }
    }

    f = xmalloc(sizeof(struct func_t));
    f->name = NULL;
    f->narg = 0;
    f->ntok = 0;
    f->signed_op = signed_op;

    f->name = xstrdup(token.sym);
    next_token(); /* skip identifier */
    next_token(); /* skip '(' */
    if (!IS(T_CLOSEBRACE)) {
        while (1) {
            if (!IS(T_ID)) {
                goto err;
            }
            f->args[f->narg++] = xstrdup(token.sym);
            next_token(); /* skip parameter */
            if (!IS(T_COMMA)) {
                break;
            }
            next_token(); /* skip ',' */
        }
        if (!IS(T_CLOSEBRACE)) {
            goto err;
        }
    }

    f->ntok = copy_tokens(f->expr);

    next_token(); /* skip ')' */
    R_assignment_exp(ok, 1, 1);
    if (*ok == 0) {
        goto errq;
    }
    if (!IS(T_EOI)) {
        goto err;
    }

    f->next = user;
    user = f;

    for (prev = user, f = f->next; f; prev = f, f = f->next) {
        if (!strcmp(f->name, user->name)) {
            prev->next = f->next;
            kill_func(f);
            break;
        }
    }
    return 1;

  err:
    message(D_ERROR, MSG_ERROR, _("bad function definition"));
    *ok = 0;
  errq:
    kill_func(f);
    return 0;
}

static LONG
handle_defines(int *ok)
{
    LONG n = 0;
    if (IS(T_DEFINE)) {
        next_token(); /* skip '#define' */
        return add_func(ok);
    }
    if (IS(T_UNDEF)) {
        next_token(); /* skip '#undef' */
        while (IS(T_ID)) {
            struct func_t *f, *prev;
            for (prev = NULL, f = user; f; prev = f, f = f->next) {
                if (!strcmp(f->name, token.sym)) {
                    if (prev == NULL) {
                        user = f->next;
                    } else {
                        prev->next = f->next;
                    }
                    kill_func(f);
                    n++;
                    break;
                }
            }
            next_token(); /* skip ID */
        }
        if (!IS(T_EOI)) {
            expect("function name");
        }
        return n;
    }
    if (IS(T_UNSET)) {
        next_token(); /* skip '#unset' */
        while (IS(T_ID)) {
            struct tuple_t *v, *prev;
            for (prev = NULL, v = vars; v; prev = v, v = v->next) {
                if (!strcmp(v->name, token.sym)) {
                    if (prev == NULL) {
                        vars = v->next;
                    } else {
                        prev->next = v->next;
                    }
                    free(v->name);
                    free(v);
                    n++;
                    break;
                }
            }
            next_token(); /* skip ID */
        }
        if (!IS(T_EOI)) {
            expect("variable name");
        }
        return n;
    }
    return n;
}

static LONG
do_eval_expr(char *buf, int *ok)
{
    LONG n = 0;
    if (tokenize(buf) <= 0) {
        *ok = 0;
        return 0;
    }
    *ok = 1;
    next_token();
    if (IS(T_DEFINE) || IS(T_UNDEF) || IS(T_UNSET)) {
        n = handle_defines(ok);
        free_tokens(0);
        return n * FIXED_ONE;
    }
    n = R_assignment_exp(ok, 0, 0);
    if (*ok && !IS(T_EOI)) {
        message(D_ERROR, MSG_ERROR, _("junk at '%s'"), token.sym);
        *ok = 0;
    }
    free_tokens(0);
    return n;
}

/*** GUI ********************************************************************/

#ifdef HAVE_MAIN
int
main(void)
{
    int ok;
    LONG val;
    char buf[BUFSIZ];
    while (fgets(buf, sizeof(buf), stdin)) {
        if (!strncmp(buf, "@m", 2)) {
            signed_op = (signed_op + 1) % (sizeof(eval_mode_str) / sizeof(*eval_mode_str));
            SET_SCALER();
            printf("mode = %s\n", eval_mode_str[signed_op]);
            continue;
        }
        if (!strncmp(buf, "@f", 2)) {
            size_t i;
            struct func_t *f;
            for (f = user; f; f = f->next) {
                printf("%s(", f->name);
                for (i = 0; i < f->narg; i++) {
                    if (i) {
                        printf(", ");
                    }
                    printf("%s", f->args[i]);
                }
                printf(") =");
                for (i = 0; i < f->ntok; i++) {
                    printf(" %s", f->expr[i]);
                }
                printf("\n");
            }
            continue;
        }
        if (!strncmp(buf, "@v", 2)) {
            struct tuple_t *n;
            for (n = vars; n; n = n->next) {
                if (FIXED_ONE > 1) {
                    printf("%s = %s\n", n->name, ftoa(n->value));
                } else {
                    printf("%s = 0x%"LFMT"x\n", n->name, n->value);
                }
            }
            continue;
        }
        if (!strncmp(buf, "@c", 2)) {
            varlist(NULL, NULL, 0, 0);
            continue;
        }
        if (!strncmp(buf, "@q", 2)) {
            break;
        }
        val = do_eval_expr(buf, &ok);
        if (ok) {
            if (FIXED_ONE > 1) {
                fprintf(stderr, "> 0x%"LFMT"x (%s)\n", val, ftoa(val));
            } else {
                fprintf(stderr, "> 0x%"LFMT"x (%"LFMT"d)\n", val, val);
            }
        }
    }
    varlist(NULL, NULL, 0, 0);
    while (user) {
        struct func_t *f = user;
        user = user->next;
        kill_func(f);
    }
    free_tokens(1);
    return 0;
}
#else
static int
show_vars (int action)
{
    int rv;
    Widget w, *in;
    char buf[256];
    char *longbuf;
    size_t l1, l2;
    int rows, cols;
    Listbox *listbox;
    struct func_t *f;
    struct tuple_t *n = NULL;

    if (action == B_USER + 3) {
        varlist(NULL, NULL, 0, 0);
        goto done;
    }

    if (!vars && !user) {
        goto done;
    }

    w.x = current_dlg->x;
    w.y = current_dlg->y;
    w.cols = current_dlg->cols;
    w.lines = current_dlg->lines;

    rows = vars && user;
    cols = 11;
    for (l1 = 0, l2 = 0, n = vars; n; n = n->next) {
        size_t len = strlen(n->name);
        if (l1 < len) {
            l1 = len;
        }
        if (FIXED_ONE > 1) {
            len = strlen(ftoa(n->value));
        } else {
            len = snprintf(buf, sizeof(buf), "0x%"LFMT"x", n->value);
        }
        if (l2 < len) {
            l2 = len;
        }
        rows++;
    }
    if (cols < l1 + l2 + 3) {
        cols = l1 + l2 + 3;
    }
    for (f = user; f; f = f->next) {
        size_t i, len = strlen(f->name);
        if (!f->narg) {
            len += 2;
        }
        for (i = 0; i < f->narg; i++) {
            len += strlen(f->args[i]) + 2;
        }
        len += 2;
        for (i = 0; i < f->ntok; i++) {
            len += strlen(f->expr[i]) + 1;
        }
        if (cols < len) {
            cols = len;
        }
        rows++;
    }

    listbox = create_listbox_compact(&w, cols + 2, rows, _(" Variables "), NULL);
    /*listbox = create_listbox_window (cols + 2, rows, _(" Variables "), NULL);*/
    if (listbox == NULL) {
        return 0;
    }
    for (n = vars; n; n = n->next) {
        if (FIXED_ONE > 1) {
            snprintf(buf, sizeof(buf), "%-*s = %s", (int)l1, n->name, ftoa(n->value));
        } else {
            snprintf(buf, sizeof(buf), "%-*s = 0x%"LFMT"x", (int)l1, n->name, n->value);
        }
        LISTBOX_APPEND_TEXT(listbox, 0, buf, NULL);
    }
    longbuf = xmalloc(cols + 1);
    if (vars && user) {
        memset(longbuf, '-', cols);
        longbuf[cols] = '\0';
        LISTBOX_APPEND_TEXT(listbox, 0, longbuf, NULL);
    }
    for (f = user; f; f = f->next) {
        size_t i, len;
        char *cp = longbuf;
        len = strlen(f->name);
        memcpy(cp, f->name, len);
        cp += len;
        *cp++ = '(';
        for (i = 0; i < f->narg; i++) {
            if (i) {
                *cp++ = ',';
                *cp++ = ' ';
            }
            len = strlen(f->args[i]);
            memcpy(cp, f->args[i], len);
            cp += len;
        }
        *cp++ = ')';
        *cp++ = ' ';
        *cp++ = '=';
        for (i = 0; i < f->ntok; i++) {
            *cp++ = ' ';
            len = strlen(f->expr[i]);
            memcpy(cp, f->expr[i], len);
            cp += len;
        }
        *cp = '\0';
        LISTBOX_APPEND_TEXT(listbox, 0, longbuf, NULL);
    }
    free(longbuf);

    rv = run_listbox(listbox);
    if (rv != -1) {
        for (n = vars; n && rv > 0; n = n->next) {
            rv--;
        }
    }

  done:
    in = find_widget_type(current_dlg, input_callback);
    if (in) {
        if (n) {
            stuff((WInput *)in, n->name, 1);
        }
        dlg_select_widget(in);
    }

    return 0;
}

static int
switch_mode (int action)
{

    Widget w, *in;
    Listbox *listbox;
    int rv, i, rows, cols;

    rows = sizeof(eval_mode_str) / sizeof(*eval_mode_str);
    cols = 0;

    for (i = 0; i < rows; i++) {
        int len = strlen(eval_mode_str[i]);
        if (cols < len) {
            cols = len;
        }
    }

    w.x = current_dlg->x;
    w.y = current_dlg->y;
    w.cols = current_dlg->cols;
    w.lines = current_dlg->lines;

    listbox = create_listbox_compact(&w, cols + 2, rows, NULL, NULL);

    for (i = 0; i < rows; i++) {
        LISTBOX_APPEND_TEXT(listbox, 0, eval_mode_str[i], NULL);
    }
    listbox_select_by_number(listbox->list, signed_op);

    rv = run_listbox(listbox);
    if (rv != -1) {
        /*WButton *b = (WButton *)current_dlg->current;
        button_set_text(b, eval_mode_str[signed_op = rv]);*/
        signed_op = rv;
        SET_SCALER();
    }

    in = find_widget_type(current_dlg, input_callback);
    if (in) {
        dlg_select_widget(in);
    }
    return 1;
}

char *
do_eval(void)
{
    char *exp = NULL;
    static char *last_eval_string;
    static char result_hex[19];
    static char result_oct[24];
    static char result_dec[32];
    static char result_neg[22];
    static char result_asc[9];
    static char result_bin[45];
    bcback vars_cb = show_vars;
    bcback mode_cb = switch_mode;

    enum {
        EVAL_DLG_HEIGHT = 13,
        EVAL_DLG_WIDTH  = 58
    };

    static QuickWidget quick_widgets[] = {
        { quick_button, 45, EVAL_DLG_WIDTH, 2, EVAL_DLG_HEIGHT, N_("V"), 0, B_USER + 2, 0, 0, NULL },
        { quick_button, 50, EVAL_DLG_WIDTH, 2, EVAL_DLG_HEIGHT, N_("C"), 0, B_USER + 3, 0, 0, NULL },
        { quick_button, 41, EVAL_DLG_WIDTH, 9, EVAL_DLG_HEIGHT, N_("&Cancel"), 0, B_CANCEL, 0, 0, NULL },
        { quick_button, 30, EVAL_DLG_WIDTH, 9, EVAL_DLG_HEIGHT, N_("D&ec"), 0, B_USER + 1, 0, 0, NULL },
        { quick_button, 19, EVAL_DLG_WIDTH, 9, EVAL_DLG_HEIGHT, N_("He&x"), 0, B_USER + 0, 0, 0, NULL },
        { quick_button, 7, EVAL_DLG_WIDTH, 9, EVAL_DLG_HEIGHT, N_("&OK"), 0, B_ENTER, 0, 0, NULL },
        { quick_button, 23, EVAL_DLG_WIDTH, 8, EVAL_DLG_HEIGHT, "mode", 0, B_USER + 4, 0, 0, NULL },
        { quick_label, 2, EVAL_DLG_WIDTH, 7, EVAL_DLG_HEIGHT, N_(" Bin: "), 0, 0, 0, 0, 0 },
        { quick_label, 2, EVAL_DLG_WIDTH, 6, EVAL_DLG_HEIGHT, N_(" Oct: "), 0, 0, 0, 0, 0 },
        { quick_label, 2, EVAL_DLG_WIDTH, 5, EVAL_DLG_HEIGHT, N_(" Dec: "), 0, 0, 0, 0, 0 },
        { quick_label, 2, EVAL_DLG_WIDTH, 4, EVAL_DLG_HEIGHT, N_(" Hex: "), 0, 0, 0, 0, 0 },
        { quick_label, 8, EVAL_DLG_WIDTH, 7, EVAL_DLG_HEIGHT, "", 0, 0, 0, 0, 0 },
        { quick_label, 8, EVAL_DLG_WIDTH, 6, EVAL_DLG_HEIGHT, "", 0, 0, 0, 0, 0 },
        { quick_label, 32, EVAL_DLG_WIDTH, 5, EVAL_DLG_HEIGHT, "", 0, 0, 0, 0, 0 },
        { quick_label, 8, EVAL_DLG_WIDTH, 5, EVAL_DLG_HEIGHT, "", 0, 0, 0, 0, 0 },
        { quick_label, 32, EVAL_DLG_WIDTH, 4, EVAL_DLG_HEIGHT, "", 0, 0, 0, 0, 0 },
        { quick_label, 8, EVAL_DLG_WIDTH, 4, EVAL_DLG_HEIGHT, "", 0, 0, 0, 0, 0 },
        { quick_input, 3, EVAL_DLG_WIDTH, 3, EVAL_DLG_HEIGHT, "", 52, 0, 0, 0, N_("Evaluate") },
        { quick_label, 2, EVAL_DLG_WIDTH, 2, EVAL_DLG_HEIGHT, N_(" Enter expression:"), 0, 0, 0, 0, 0 },
        NULL_QuickWidget
    };
    static QuickDialog Quick_input = {
        EVAL_DLG_WIDTH, EVAL_DLG_HEIGHT, -1, 0, N_("Evaluate"), "[Evaluate]", quick_widgets, 0
    };

    quick_widgets[0].result = (int *)&vars_cb;
    quick_widgets[1].result = (int *)&vars_cb;
    quick_widgets[6].result = (int *)&mode_cb;
    quick_widgets[17].str_result = &exp;
    quick_widgets[17].text = last_eval_string;

    while (1) {
        int ok, rv;
        LONG result;

        quick_widgets[6].text = eval_mode_hot[signed_op];
        rv = quick_dialog(&Quick_input);
        if (rv < B_USER && rv != B_ENTER) {
            break;
        }

        result = do_eval_expr(exp, &ok);

        *result_hex = '\0';
        *result_dec = '\0';
        *result_oct = '\0';
        *result_bin = '\0';
        *result_asc = '\0';
        *result_neg = '\0';
        if (ok && FIXED_ONE > 1) {
            snprintf(result_dec, sizeof(result_dec), "%s", ftoa(result));
        } else if (ok) {
            int i;
            char *p;
            unsigned LONG tmp;
            snprintf(result_hex, sizeof(result_hex), "0x%"LFMT"x", result);
            snprintf(result_dec, sizeof(result_dec), signed_op ? "%"LFMT"d" : "%"LFMT"u", result);
            snprintf(result_oct, sizeof(result_oct), result ? "0%"LFMT"o" : "%"LFMT"o", result);
            tmp = result;
            p = result_bin + sizeof(result_bin) - 1;
            *p-- = '\0';
            for (i = 0; p >= result_bin; i++) {
                *p-- = (tmp & 1) + '0';
                tmp >>= 1;
                if ((i & 3) == 3 && p > result_bin) {
                    *p-- = '\'';
                }
            }
            tmp = result;
            p = result_asc + sizeof(result_asc) - 1;
            *p-- = '\0';
            while (p >= result_asc) {
                int ch = convert_to_display_c(tmp & 0xFF);
                if (ch < 0x20) {
                    ch = '.';
                }
                if (ch > 0x7E) {
                    ch = '?';
                }
                *p-- = ch;
                tmp >>= 8;
            }
            if (result < 0) {
                snprintf(result_neg, sizeof(result_neg), signed_op ? "%"LFMT"u" : "%"LFMT"d", result);
            }
        } else if (exp && *exp) {
            strcpy(result_neg, "Error");
        }

        g_free(last_eval_string);
        quick_widgets[17].text = last_eval_string = exp;
        exp = NULL;

        quick_widgets[11].text = result_bin;
        quick_widgets[12].text = result_oct;
        quick_widgets[13].text = result_neg;
        quick_widgets[14].text = result_dec;
        quick_widgets[15].text = result_asc;
        quick_widgets[16].text = result_hex;

        switch (rv) {
            case B_USER + 0:
                return *result_hex ? strdup(result_hex) : NULL;
            case B_USER + 1:
                if (*result_dec) {
                    char *ret = malloc(32);
                    if (ret) {
                        char *p, *q = ret;
                        for (p = result_dec; *p; p++) {
                            if (*p == '\'') {
                                continue;
                            }
                            *q++ = *p;
                        }
                        *q = '\0';
                        return ret;
                    }
                }
                return NULL;
        }
    }

    g_free (exp);
    return NULL;
}
#endif
