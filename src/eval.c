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

#ifdef USE_EVALUATOR

#ifdef HAVE_LONG_LONG
#define LONG long long
#define LFMT "ll"
#else
#define LONG long
#define LFMT "l"
#endif

#ifdef HAVE_MAIN
#include <assert.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

#define _(string) string
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

static int signed_op = 0;

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

static LONG
strton(const char *str, char **endptr, int base)
{
    const char *p = str;
    if ((base | 2) == 2 && str[0] == '0' && (str[1] | 0x20) == 'b') {
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

    T_COMMA,
    T_OPENBRACE,
    T_CLOSEBRACE,
    T_ASSIGN,

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
            case '*':
                if (s[1] == '*') {
                    s++;
                }
            case '~':
            case '/':
            case '%':
            case '+':
            case '-':
            case '&':
            case '^':
            case '|':
            case ',':
            case '(':
            case ')':
            case '=':
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
            default:
            if (isdigit(*s)) {
                strton(s, &p, 0);
                if (*p == 'k' || *p == 'M' || *p == 'G') {
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

/*** eval *******************************************************************/

#define IS(t) (token.type == (t))

struct node {
    struct node *next;
    LONG val;
};

static void *
reverse_list(struct node *n)
{
    struct node *next, *prev = NULL;
    while (n) {
        next = n->next;
        n->next = prev;
        prev = n;
        n = next;
    }
    return prev;
}

static struct tuple_t {
    struct tuple_t *next;
    char *name;
    LONG value;
} *vars = NULL;

static int
varlist(const char *name, LONG *oldval, LONG newval)
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
        n = xmalloc(sizeof(struct tuple_t));
        n->name = xstrdup(name);
        n->next = vars;
        vars = n;
    }
    n->value = newval;
    return 1;
}

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
    return sqrti(n, &r);
}

static LONG
fn_cbrt(const char *func, struct node *args, int *ok)
{
    LONG n = args->val;
    if (n < 0 && signed_op) {
        return -cbrti(-n);
    }
    return cbrti(n);
}

static LONG
fn_min(const char *func, struct node *args, int *ok)
{
    struct node *p;
    unsigned LONG n = args->val;
    for (p = args->next; p; p = p->next) {
        if (signed_op) {
            if ((LONG)n > p->val) {
                n = p->val;
            }
        } else {
            if (n > p->val) {
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
    unsigned LONG n = args->val;
    for (p = args->next; p; p = p->next) {
        if (signed_op) {
            if ((LONG)n < p->val) {
                n = p->val;
            }
        } else {
            if (n < p->val) {
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
fn_cdq(const char *func, struct node *args, int *ok)
{
    return (int)args->val;
}

static LONG
fn_error(const char *func, struct node *args, int *ok)
{
    message(D_ERROR, MSG_ERROR, _("unknown function '%s'"), func);
    *ok = 0;
    return 0;
}

static LONG
builtin_call(const char *func, struct node *args, int *ok)
{
    static const struct {
        const char *name;
        int nargs;
        int flags;
        LONG (*call)(const char *name, struct node *args, int *ok);
    } tab[] = {
        { "abs",   1, 0, fn_abs },
        { "sqrt",  1, 0, fn_sqrt },
        { "cbrt",  1, 0, fn_cbrt },
        { "min",   2, 1, fn_min },
        { "max",   2, 1, fn_max },
        { "avg",   1, 1, fn_avg },
        { "floor", 2, 0, fn_floor },
        { "ceil",  2, 0, fn_ceil },
        { "gcd",   2, 0, fn_gcd },
        { "lcm",   2, 0, fn_lcm },
        { "floor2", 1, 0, fn_floor2 },
        { "ceil2", 1, 0, fn_ceil2 },
        { "bits",  1, 0, fn_bits },
        { "cdq",   1, 0, fn_cdq },
        { NULL,   -1, 0, fn_error }
    };

    size_t i;
    int n, nargs;
    struct node *p;
    for (i = 0; tab[i].name && strcmp(func, tab[i].name); i++) {
        continue;
    }
    for (n = 0, p = args; p; p = p->next, n++) {
        continue;
    }
    nargs = tab[i].nargs;
    if (nargs >= 0 && nargs != n) {
        if (tab[i].flags) {
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
    return tab[i].call(func, args, ok);
}

static void
expect(const char *what)
{
    const char *sym = token.sym;
    if (IS(T_INVALID)) {
        return;
    }
    if (IS(T_EOI)) {
        sym = "end of input";
    }
    message(D_ERROR, MSG_ERROR, _("expected %s before %s"), what, sym);
}

static LONG R_rvalue_exp(int *ok);

static LONG
R_pow_exp(int *ok)
{
    LONG n;
    ENTER();
    n = R_rvalue_exp(ok);
    if (*ok && (IS(T_POW))) {
        LONG k;
        next_token(); /* skip '**' */
        k = R_pow_exp(ok);
        if (*ok) {
            if (k < 0) {
                message(D_ERROR, MSG_ERROR, _("negative powers not supported"));
                *ok = 0;
            } else {
                LONG p = n;
                n = 1;
                while (n) {
                    if (k & 1) {
                        n *= p;
                    }
                    k >>= 1;
                    if (k == 0) {
                        break;
                    }
                    p *= p;
                }
            }
        }
    }
    LEAVE();
    return n;
}

static LONG
R_mul_exp(int *ok)
{
    LONG n;
    ENTER();
    n = R_pow_exp(ok);
    while (*ok && (IS(T_MUL) || IS(T_DIV) || IS(T_MOD))) {
        LONG v;
        enum TOKTYPE op = token.type;
        next_token(); /* skip '*' */
        v = R_pow_exp(ok);
        if (*ok) {
            if (op == T_MUL) {
                n *= v;
            } else if (v) {
                if (op == T_DIV) {
                    if (signed_op) {
                        n /= v;
                    } else {
                        n = (unsigned LONG)n / v;
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
R_add_exp(int *ok)
{
    LONG n;
    ENTER();
    n = R_mul_exp(ok);
    while (*ok && (IS(T_ADD) || IS(T_SUB))) {
        LONG v;
        enum TOKTYPE op = token.type;
        next_token(); /* skip '+' */
        v = R_mul_exp(ok);
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
R_shift_exp(int *ok)
{
    LONG n;
    ENTER();
    n = R_add_exp(ok);
    while (*ok && (IS(T_SHL) || IS(T_SHR) || IS(T_ASR))) {
        LONG v;
        enum TOKTYPE op = token.type;
        next_token(); /* skip '<<' */
        v = R_add_exp(ok);
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
                    case T_SHR:
                        n = (unsigned LONG)n >> v;
                        break;
                    case T_ASR:
                        n >>= v;
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
R_and_exp(int *ok)
{
    LONG n;
    ENTER();
    n = R_shift_exp(ok);
    while (*ok && IS(T_AND)) {
        LONG v;
        next_token(); /* skip '&' */
        v = R_shift_exp(ok);
        if (*ok) {
            n &= v;
        }
    }
    LEAVE();
    return n;
}

static LONG
R_xor_exp(int *ok)
{
    LONG n;
    ENTER();
    n = R_and_exp(ok);
    while (*ok && IS(T_XOR)) {
        LONG v;
        next_token(); /* skip '^' */
        v = R_and_exp(ok);
        if (*ok) {
            n ^= v;
        }
    }
    LEAVE();
    return n;
}

static LONG
R_or_exp(int *ok)
{
    LONG n;
    ENTER();
    n = R_xor_exp(ok);
    while (*ok && IS(T_OR)) {
        LONG v;
        next_token(); /* skip '|' */
        v = R_xor_exp(ok);
        if (*ok) {
            n |= v;
        }
    }
    LEAVE();
    return n;
}

static LONG
R_assignment_exp(int *ok)
{
    LONG n;
    char *lval = NULL;
    ENTER();
    /* we cannot tell here if we have lvalue or rvalue, so use magic */
    if (IS(T_ID) && peek_token() == T_ASSIGN) {
        lval = xstrdup(token.sym);
        next_token(); /* skip ID */
        next_token(); /* skip '=' */
    }
    n = R_or_exp(ok);
    if (*ok && lval) {
        /* XXX should ban internal functions */
        varlist(lval, NULL, n);
    }
    free(lval);
    LEAVE();
    return n;
}

static struct node *
R_argument_exp_list(int *ok)
{
    struct node *p = NULL;
    LONG n;
    ENTER();
    n = R_or_exp(ok);
    while (*ok) {
        struct node *q = xmalloc(sizeof(struct node));
        q->val = n;
        q->next = p;
        p = q;
        if (!IS(T_COMMA)) {
            break;
        }
        next_token(); /* skip ',' */
        n = R_or_exp(ok);
    }
    LEAVE();
    return p;
}

static LONG
R_rvalue_exp(int *ok)
{
    LONG n = 0;
    ENTER();
    if (IS(T_ID) && peek_token() == T_OPENBRACE) {
        struct node *p, *args = NULL;
        char *func = xstrdup(token.sym);
        next_token(); /* skip ID */
        next_token(); /* skip '(' */
        if (!IS(T_CLOSEBRACE)) {
            args = R_argument_exp_list(ok);
        }
        if (*ok) {
            if (!IS(T_CLOSEBRACE)) {
                expect("')'");
                *ok = 0;
            } else {
                next_token(); /* skip ')' */
                n = builtin_call(func, args, ok);
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
        n = R_or_exp(ok);
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
        n = R_rvalue_exp(ok);
    } else if (IS(T_SUB)) {
        next_token(); /* skip '-' */
        n = -R_rvalue_exp(ok);
    } else if (IS(T_NOT)) {
        next_token(); /* skip '~' */
        n = ~R_rvalue_exp(ok);
    } else if (IS(T_ID)) {
        if (!varlist(token.sym, &n, 0)) {
            message(D_ERROR, MSG_ERROR, _("unknown identifier: '%s'"), token.sym);
            *ok = 0;
        }
        next_token(); /* skip ID */
    } else if (IS(T_INT)) {
        char *bp;
        n = strton(token.sym, &bp, 0);
        switch (*bp) {
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
    n = R_assignment_exp(ok);
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
        if (!strncmp(buf, "@sig", 4)) {
            signed_op ^= 1;
            printf("signed_op = %d\n", signed_op);
            continue;
        }
        if (!strncmp(buf, "@var", 4)) {
            struct tuple_t *n;
            for (n = vars; n; n = n->next) {
                printf("%s = 0x%"LFMT"x\n", n->name, n->value);
            }
            continue;
        }
        if (!strncmp(buf, "@clr", 4)) {
            varlist(NULL, NULL, 0);
            continue;
        }
        if (!strncmp(buf, "@bye", 4)) {
            break;
        }
        val = do_eval_expr(buf, &ok);
        if (ok) {
            fprintf(stderr, "> 0x%"LFMT"x (%"LFMT"d)\n", val, val);
        }
    }
    varlist(NULL, NULL, 0);
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
    size_t l1, l2;
    int rows, cols;
    Listbox *listbox;
    struct tuple_t *n = NULL;

    if (action == B_USER + 3) {
        varlist(NULL, NULL, 0);
    }

    if (!vars) {
        goto done;
    }

    w.x = current_dlg->x;
    w.y = current_dlg->y;
    w.cols = current_dlg->cols;
    w.lines = current_dlg->lines;

    rows = 0;
    cols = 11;
    for (l1 = 0, l2 = 0, n = vars; n; n = n->next) {
        size_t len = strlen(n->name);
        if (l1 < len) {
            l1 = len;
        }
        len = snprintf(buf, sizeof(buf), "0x%"LFMT"x", n->value);
        if (l2 < len) {
            l2 = len;
        }
        rows++;
    }
    if (cols < l1 + l2 + 5) {
        cols = l1 + l2 + 5;
    }

    listbox = create_listbox_compact(&w, cols + 2, rows, _(" Variables "), NULL);
    /*listbox = create_listbox_window (cols + 2, rows, _(" Variables "), NULL);*/
    if (listbox == NULL) {
        return 0;
    }
    for (n = vars; n; n = n->next) {
        snprintf(buf, sizeof(buf), "%-*s = 0x%"LFMT"x", (int)l1, n->name, n->value);
        LISTBOX_APPEND_TEXT(listbox, 0, buf, NULL);
    }

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

char *
do_eval(void)
{
    char *exp = NULL;
    static char *last_eval_string;
    static char result_hex[19];
    static char result_oct[24];
    static char result_dec[21];
    static char result_neg[22];
    static char result_asc[9];
    static char result_bin[45];
    bcback callback = show_vars;

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
        { quick_checkbox, 1, 3, 8, EVAL_DLG_HEIGHT, N_(" &Signed arithmetic "), 11, 0, &signed_op, NULL, NULL },
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

    quick_widgets[0].result = (int *)&callback;
    quick_widgets[1].result = (int *)&callback;
    quick_widgets[17].str_result = &exp;
    quick_widgets[17].text = last_eval_string;

    while (1) {
        int ok, rv;
        LONG result;

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
        if (ok) {
            int i;
            char *p;
            unsigned LONG tmp;
            snprintf(result_hex, sizeof(result_hex), "0x%"LFMT"x", result);
            snprintf(result_dec, sizeof(result_dec), "%"LFMT"u", result);
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
                snprintf(result_neg, sizeof(result_dec), "%"LFMT"d", result);
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
                return ok ? strdup(result_hex) : NULL;
            case B_USER + 1:
                return ok ? strdup(result_dec) : NULL;
        }
    }

    g_free (exp);
    return NULL;
}
#endif

#endif
