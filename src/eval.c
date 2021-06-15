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
    T_AND,
    T_XOR,
    T_OR,

    T_COMMA,
    T_OPENBRACE,
    T_CLOSEBRACE,

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
                break;
            case '<':
            case '>':
                if (s[1] == s[0]) {
                    s++;
                    break;
                }
            default:
            if (isdigit(*s)) {
                strtoull(s, &p, 0);
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
    } else if (!strcmp(s, "<<")) {
        type = T_SHL;
    } else if (!strcmp(s, ">>")) {
        type = T_SHR;
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

#ifdef HAVE_MAIN
static int
varlist(const char *name, LONG *oldval, LONG newval)
{
    static struct tuple_t {
        struct tuple_t *next;
        char *name;
        LONG value;
    } *n, *vars = NULL;
    if (!name) {
        while (vars) {
            n = vars->next;
            free(vars->name);
            free(vars);
            vars = n;
        }
        return 0;
    }
    if (oldval) {
        for (n = vars; n; n = n->next) {
            if (!strcmp(n->name, name)) {
                *oldval = n->value;
                return 1;
            }
        }
        return 0;
    }
    n = xmalloc(sizeof(struct tuple_t));
    n->name = xstrdup(name);
    n->value = newval;
    n->next = vars;
    vars = n;
    return 0;
}
#endif

static unsigned LONG
sqrti(unsigned LONG n, unsigned LONG *r)
{
    unsigned LONG root = 0;
    unsigned LONG remainder = n;
    unsigned LONG place = (unsigned LONG)1 << (sizeof(unsigned LONG) * 8 - 2);

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

static LONG R_rvalue_exp(int *ok, LONG M);

static LONG
R_pow_exp(int *ok, LONG M)
{
    LONG n;
    ENTER();
    n = R_rvalue_exp(ok, M);
    if (*ok && (IS(T_POW))) {
        LONG v;
        next_token(); /* skip '**' */
        v = R_pow_exp(ok, M);
        if (*ok) {
            if (v < 0) {
                message(D_ERROR, MSG_ERROR, _("negative powers not supported"));
                *ok = 0;
            } else {
                LONG val = 1;
                while (v--) {
                    val *= n;
                }
                n = val;
            }
        }
    }
    LEAVE();
    return n;
}

static LONG
R_mul_exp(int *ok, LONG M)
{
    LONG n;
    ENTER();
    n = R_pow_exp(ok, M);
    while (*ok && (IS(T_MUL) || IS(T_DIV) || IS(T_MOD))) {
        LONG v;
        enum TOKTYPE op = token.type;
        next_token(); /* skip '*' */
        v = R_pow_exp(ok, M);
        if (*ok) {
            if (op == T_MUL) {
                n *= v;
            } else if (v) {
                if (op == T_DIV) {
                    n /= v;
                } else {
                    n %= v;
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
R_add_exp(int *ok, LONG M)
{
    LONG n;
    ENTER();
    n = R_mul_exp(ok, M);
    while (*ok && (IS(T_ADD) || IS(T_SUB))) {
        LONG v;
        enum TOKTYPE op = token.type;
        next_token(); /* skip '+' */
        v = R_mul_exp(ok, M);
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
R_shift_exp(int *ok, LONG M)
{
    LONG n;
    ENTER();
    n = R_add_exp(ok, M);
    while (*ok && (IS(T_SHL) || IS(T_SHR))) {
        LONG v;
        enum TOKTYPE op = token.type;
        next_token(); /* skip '<<' */
        v = R_add_exp(ok, M);
        if (*ok) {
            if (op == T_SHL) {
                n <<= v;
            } else {
                n >>= v;
            }
        }
    }
    LEAVE();
    return n;
}

static LONG
R_and_exp(int *ok, LONG M)
{
    LONG n;
    ENTER();
    n = R_shift_exp(ok, M);
    while (*ok && IS(T_AND)) {
        LONG v;
        next_token(); /* skip '&' */
        v = R_shift_exp(ok, M);
        if (*ok) {
            n &= v;
        }
    }
    LEAVE();
    return n;
}

static LONG
R_xor_exp(int *ok, LONG M)
{
    LONG n;
    ENTER();
    n = R_and_exp(ok, M);
    while (*ok && IS(T_XOR)) {
        LONG v;
        next_token(); /* skip '^' */
        v = R_and_exp(ok, M);
        if (*ok) {
            n ^= v;
        }
    }
    LEAVE();
    return n;
}

static LONG
R_or_exp(int *ok, LONG M)
{
    LONG n;
    ENTER();
    n = R_xor_exp(ok, M);
    while (*ok && IS(T_OR)) {
        LONG v;
        next_token(); /* skip '|' */
        v = R_xor_exp(ok, M);
        if (*ok) {
            n |= v;
        }
    }
    LEAVE();
    return n;
}

static struct node *
R_argument_exp_list(int *ok, LONG M)
{
    struct node *p = NULL;
    LONG n;
    ENTER();
    n = R_or_exp(ok, M);
    while (*ok) {
        struct node *q = xmalloc(sizeof(struct node));
        q->val = n;
        q->next = p;
        p = q;
        if (!IS(T_COMMA)) {
            break;
        }
        next_token(); /* skip ',' */
        n = R_or_exp(ok, M);
    }
    LEAVE();
    return p;
}

static LONG
R_rvalue_exp(int *ok, LONG M)
{
    LONG n = 0;
    ENTER();
    if (IS(T_ID) && peek_token() == T_OPENBRACE) {
        struct node *p, *args = NULL;
        char *func = xstrdup(token.sym);
        next_token(); /* skip ID */
        next_token(); /* skip '(' */
        if (!IS(T_CLOSEBRACE)) {
            args = R_argument_exp_list(ok, M);
        }
        if (*ok) {
            if (!IS(T_CLOSEBRACE)) {
                expect("')'");
                *ok = 0;
            } else {
                next_token(); /* skip ')' */
                if (!strcmp(func, "abs")) {
                    if (!args || args->next) {
                        message(D_ERROR, MSG_ERROR, _("function '%s' requires one argument"), func);
                        *ok = 0;
                    } else {
                        n = args->val;
                        if (n < 0) {
                            n = -n;
                        }
                    }
                } else if (!strcmp(func, "sqrt")) {
                    if (!args || args->next) {
                        message(D_ERROR, MSG_ERROR, _("function '%s' requires one argument"), func);
                        *ok = 0;
                    } else {
                        unsigned LONG r;
                        n = args->val;
                        if (n < 0) {
                            message(D_ERROR, _("Warning"), _("negative number treated as unsigned 0x%"LFMT"x"), n);
                        }
                        n = sqrti(n, &r);
                    }
                } else if (!strcmp(func, "min")) {
                    if (!args || !args->next) {
                        message(D_ERROR, MSG_ERROR, _("function '%s' requires at least two arguments"), func);
                        *ok = 0;
                    } else {
                        /*args = reverse_list(args);*/
                        n = args->val;
                        for (p = args->next; p; p = p->next) {
                            if (n > p->val) {
                                n = p->val;
                            }
                        }
                    }
                } else if (!strcmp(func, "max")) {
                    if (!args || !args->next) {
                        message(D_ERROR, MSG_ERROR, _("function '%s' requires at least two arguments"), func);
                        *ok = 0;
                    } else {
                        /*args = reverse_list(args);*/
                        n = args->val;
                        for (p = args->next; p; p = p->next) {
                            if (n < p->val) {
                                n = p->val;
                            }
                        }
                    }
                } else {
                    message(D_ERROR, MSG_ERROR, _("unknown function '%s'"), func);
                    *ok = 0;
                }
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
        n = R_or_exp(ok, M);
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
        n = R_rvalue_exp(ok, M);
    } else if (IS(T_SUB)) {
        next_token(); /* skip '-' */
        n = -R_rvalue_exp(ok, M);
    } else if (IS(T_NOT)) {
        next_token(); /* skip '~' */
        n = ~R_rvalue_exp(ok, M);
    } else if (IS(T_ID)) {
#ifdef HAVE_MAIN
        char answer[256];
        if (!varlist(token.sym, &n, 0)) {
            fprintf(stderr, "unknown variable '%s': ", token.sym);
            fgets(answer, sizeof(answer), stdin);
            n = strtoull(answer, NULL, 0);
            varlist(token.sym, NULL, n);
        }
        fprintf(stderr, "%s = %"LFMT"d\n", token.sym, n);
        next_token(); /* skip ID */
#else
        if (!strcmp(token.sym, "M") || !strcmp(token.sym, "m")) {
            n = M;
            next_token(); /* skip ID */
        } else {
            message(D_ERROR, MSG_ERROR, _("unknown identifier: '%s'"), token.sym);
            *ok = 0;
        }
#endif
    } else if (IS(T_INT)) {
        n = strtoull(token.sym, NULL, 0);
        next_token(); /* skip number */
    } else {
        expect("expression");
        *ok = 0;
    }
    LEAVE();
    return n;
}

static LONG
do_eval_expr(char *buf, int *ok, LONG M)
{
    LONG n = 0;
    if (tokenize(buf) <= 0) {
        *ok = 0;
        return 0;
    }
    *ok = 1;
    next_token();
    n = R_or_exp(ok, M);
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
        val = do_eval_expr(buf, &ok, 0);
        if (ok) {
            fprintf(stderr, "E = %"LFMT"d\n", val);
        }
    }
    varlist(NULL, NULL, 0);
    free_tokens(1);
    return 0;
}
#else
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
    static unsigned LONG M = 0;

    enum {
        EVAL_DLG_HEIGHT = 13,
        EVAL_DLG_WIDTH  = 58
    };

    static QuickWidget quick_widgets[] = {
        { quick_button, 9, 12, 9, EVAL_DLG_HEIGHT, N_("&Cancel"), 0, B_CANCEL, 0, 0, NULL },
        { quick_button, 7, 12, 9, EVAL_DLG_HEIGHT, N_("D&ec"), 0, B_USER + 2, 0, 0, NULL },
        { quick_button, 5, 12, 9, EVAL_DLG_HEIGHT, N_("He&x"), 0, B_USER + 1, 0, 0, NULL },
        { quick_button, 3, 12, 9, EVAL_DLG_HEIGHT, N_("&Mem"), 0, B_USER + 0, 0, 0, NULL },
        { quick_button, 1, 12, 9, EVAL_DLG_HEIGHT, N_("&OK"), 0, B_ENTER, 0, 0, NULL },
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
        { quick_label, 35, EVAL_DLG_WIDTH, 2, EVAL_DLG_HEIGHT, "", 0, 0, 0, 0, 0 },
        { quick_label, 2, EVAL_DLG_WIDTH, 2, EVAL_DLG_HEIGHT, N_(" Enter expression:"), 0, 0, 0, 0, 0 },
        NULL_QuickWidget
    };
    static QuickDialog Quick_input = {
        EVAL_DLG_WIDTH, EVAL_DLG_HEIGHT, -1, 0, N_("Evaluate"), "[Evaluate]", quick_widgets, 0
    };

    quick_widgets[15].str_result = &exp;
    quick_widgets[15].text = last_eval_string;

    while (1) {
        int ok, rv;
        LONG result;

        char memo[256];
        quick_widgets[16].text = "";
        if (M) {
            snprintf(memo, sizeof(memo), "M:0x%"LFMT"x", M);
            quick_widgets[16].text = memo;
        }

        rv = quick_dialog(&Quick_input);
        if (rv < B_USER && rv != B_ENTER) {
            break;
        }

        result = do_eval_expr(exp, &ok, M);

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
            if (rv == B_USER) {
                M = result;
            }
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
        } else if (rv == B_USER) {
            M = 0;
        }

        g_free(last_eval_string);
        quick_widgets[15].text = last_eval_string = exp;
        exp = NULL;

        quick_widgets[9].text = result_bin;
        quick_widgets[10].text = result_oct;
        quick_widgets[11].text = result_neg;
        quick_widgets[12].text = result_dec;
        quick_widgets[13].text = result_asc;
        quick_widgets[14].text = result_hex;

        switch (rv) {
            case B_USER + 1:
                return ok ? strdup(result_hex) : NULL;
            case B_USER + 2:
                return ok ? strdup(result_dec) : NULL;
        }
    }

    g_free (exp);
    return NULL;
}
#endif

#endif
