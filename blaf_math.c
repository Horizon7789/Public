/*
 * ╔══════════════════════════════════════════════════════════════╗
 * ║  BLAF — blaf_math.c                                         ║
 * ║  BODMAS Arithmetic Engine                                   ║
 * ╚══════════════════════════════════════════════════════════════╝
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <ctype.h>
#include "blaf_math.h"

/* ═══════════════════════════════════════════════════════════════
   WORD → NUMBER TABLE
   ═══════════════════════════════════════════════════════════════ */

typedef struct { const char *word; long long value; int is_multiplier; } NumWord;

static const NumWord num_words[] = {
    /* zero to nineteen */
    {"zero",          0,  0}, {"one",          1,  0},
    {"two",           2,  0}, {"three",        3,  0},
    {"four",          4,  0}, {"five",         5,  0},
    {"six",           6,  0}, {"seven",        7,  0},
    {"eight",         8,  0}, {"nine",         9,  0},
    {"ten",          10,  0}, {"eleven",      11,  0},
    {"twelve",       12,  0}, {"thirteen",    13,  0},
    {"fourteen",     14,  0}, {"fifteen",     15,  0},
    {"sixteen",      16,  0}, {"seventeen",   17,  0},
    {"eighteen",     18,  0}, {"nineteen",    19,  0},
    /* tens */
    {"twenty",       20,  0}, {"thirty",      30,  0},
    {"forty",        40,  0}, {"fifty",       50,  0},
    {"sixty",        60,  0}, {"seventy",     70,  0},
    {"eighty",       80,  0}, {"ninety",      90,  0},
    /* multipliers */
    {"hundred",     100,  1}, {"thousand",   1000, 1},
    {"million",  1000000, 1}, {"billion", 1000000000LL, 1},
    {NULL, 0, 0}
};

/* Operator word → symbol */
typedef struct { const char *word; char op; } OpWord;
static const OpWord op_words[] = {
    {"plus",            '+'},
    {"add",             '+'},
    {"added to",        '+'},
    {"minus",           '-'},
    {"subtract",        '-'},
    {"subtracted from", '-'},
    {"take away",       '-'},
    {"times",           '*'},
    {"multiplied by",   '*'},
    {"multiply",        '*'},
    {"x",               '*'},   /* "3 x 4" */
    {"divided by",      '/'},
    {"divide",          '/'},
    {"over",            '/'},
    {"mod",             '%'},
    {"modulo",          '%'},
    {"remainder",       '%'},
    {"to the power of", '^'},
    {"to the power",    '^'},
    {"squared",         'Q'},   /* special: x^2 */
    {"cubed",           'C'},   /* special: x^3 */
    {"power of",        '^'},
    {"^",               '^'},
    {NULL, 0}
};

/* Special function words */
typedef struct { const char *word; int fn_id; } FnWord;
#define FN_SQRT    1
#define FN_ABS     2
#define FN_FLOOR   3
#define FN_CEIL    4
#define FN_LOG     5
#define FN_LOG10   6

static const FnWord fn_words[] = {
    {"square root of",  FN_SQRT},
    {"square root",     FN_SQRT},
    {"sqrt",            FN_SQRT},
    {"sqrt of",         FN_SQRT},
    {"absolute value of", FN_ABS},
    {"absolute value",  FN_ABS},
    {"abs",             FN_ABS},
    {"floor of",        FN_FLOOR},
    {"ceiling of",      FN_CEIL},
    {"log of",          FN_LOG},
    {"ln of",           FN_LOG},
    {"log10 of",        FN_LOG10},
    {NULL, 0}
};

/* ═══════════════════════════════════════════════════════════════
   HELPERS
   ═══════════════════════════════════════════════════════════════ */

static void lower_copy(char *d, const char *s, int n) {
    int i; for(i=0;i<n-1&&s[i];i++) d[i]=tolower((unsigned char)s[i]); d[i]='\0';
}

static void skip_ws(const char **p) {
    while (**p && isspace((unsigned char)**p)) (*p)++;
}

/* ═══════════════════════════════════════════════════════════════
   WORD → NUMBER CONVERSION
   Handles: "forty two", "one hundred and five", "three million"
   ═══════════════════════════════════════════════════════════════ */

double math_words_to_number(const char *words, int *ok) {
    *ok = 0;
    if (!words || !words[0]) return 0;

    char l[256]; lower_copy(l, words, 256);

    /* Strip "and" — "one hundred and five" → "one hundred five" */
    char cleaned[256] = {0};
    const char *src = l;
    char *dst = cleaned;
    while (*src) {
        if (strncmp(src, " and ", 5) == 0) { *dst++ = ' '; src += 5; }
        else *dst++ = *src++;
    }
    *dst = '\0';

    /* Tokenize and accumulate */
    long long current  = 0;
    long long result   = 0;
    int       found    = 0;

    char *tok = strtok(cleaned, " \t");
    while (tok) {
        int matched = 0;
        for (int i = 0; num_words[i].word; i++) {
            if (strcmp(tok, num_words[i].word) == 0) {
                if (num_words[i].is_multiplier) {
                    if (current == 0) current = 1;
                    if (num_words[i].value >= 1000) {
                        result  += current * num_words[i].value;
                        current  = 0;
                    } else {
                        current *= num_words[i].value;
                    }
                } else {
                    current += num_words[i].value;
                }
                found   = 1;
                matched = 1;
                break;
            }
        }
        if (!matched) {
            /* Not a number word — fail gracefully */
            tok = strtok(NULL, " \t");
            continue;
        }
        tok = strtok(NULL, " \t");
    }

    result += current;
    if (found) { *ok = 1; return (double)result; }
    return 0;
}

/* ═══════════════════════════════════════════════════════════════
   NUMBER → WORDS
   ═══════════════════════════════════════════════════════════════ */

static const char *ones[] = {
    "", "one", "two", "three", "four", "five", "six", "seven",
    "eight", "nine", "ten", "eleven", "twelve", "thirteen",
    "fourteen", "fifteen", "sixteen", "seventeen", "eighteen", "nineteen"
};
static const char *tens_w[] = {
    "", "", "twenty", "thirty", "forty", "fifty",
    "sixty", "seventy", "eighty", "ninety"
};

static void say_below_1000(long long n, char *out, int outlen) {
    out[0] = '\0';
    if (n == 0) return;
    char tmp[128] = {0};
    if (n >= 100) {
        snprintf(tmp, sizeof(tmp), "%s hundred", ones[n/100]);
        n %= 100;
        if (n) strncat(tmp, " ", sizeof(tmp)-strlen(tmp)-1);
    }
    if (n >= 20) {
        char t2[32]; snprintf(t2, sizeof(t2), "%s", tens_w[n/10]);
        if (n % 10) { char t3[32]; snprintf(t3,sizeof(t3)," %s",ones[n%10]); strncat(t2,t3,sizeof(t2)-strlen(t2)-1); }
        strncat(tmp, t2, sizeof(tmp)-strlen(tmp)-1);
    } else if (n > 0) {
        strncat(tmp, ones[n], sizeof(tmp)-strlen(tmp)-1);
    }
    strncpy(out, tmp, outlen-1);
}

void math_number_to_words(long long n, char *out, int outlen) {
    out[0] = '\0';
    if (n == 0) { strncpy(out, "zero", outlen-1); return; }

    char result[512] = {0};
    if (n < 0) { strncpy(result, "negative ", sizeof(result)-1); n = -n; }

    char part[128];

    if (n >= 1000000000LL) {
        say_below_1000(n/1000000000LL, part, sizeof(part));
        strncat(result, part, sizeof(result)-strlen(result)-1);
        strncat(result, " billion", sizeof(result)-strlen(result)-1);
        n %= 1000000000LL;
        if (n) strncat(result, " ", sizeof(result)-strlen(result)-1);
    }
    if (n >= 1000000) {
        say_below_1000(n/1000000, part, sizeof(part));
        strncat(result, part, sizeof(result)-strlen(result)-1);
        strncat(result, " million", sizeof(result)-strlen(result)-1);
        n %= 1000000;
        if (n) strncat(result, " ", sizeof(result)-strlen(result)-1);
    }
    if (n >= 1000) {
        say_below_1000(n/1000, part, sizeof(part));
        strncat(result, part, sizeof(result)-strlen(result)-1);
        strncat(result, " thousand", sizeof(result)-strlen(result)-1);
        n %= 1000;
        if (n) strncat(result, " ", sizeof(result)-strlen(result)-1);
    }
    if (n > 0) {
        say_below_1000(n, part, sizeof(part));
        strncat(result, part, sizeof(result)-strlen(result)-1);
    }
    strncpy(out, result, outlen-1);
}

/* ═══════════════════════════════════════════════════════════════
   EXPRESSION NORMALISER
   Converts word form to a numeric infix string.
   "two plus three times four" → "2+3*4"
   "(three plus four) times two squared" → "(3+4)*2^2"
   ═══════════════════════════════════════════════════════════════ */

static void normalise_expression(const char *input, char *out, int outlen) {
    char l[512]; lower_copy(l, input, 512);

    /* Strip query preamble */
    const char *preambles[] = {
        "what is ", "calculate ", "compute ", "evaluate ",
        "solve ", "work out ", "find ", "what's ", "whats ", NULL
    };
    for (int i = 0; preambles[i]; i++) {
        int len = strlen(preambles[i]);
        if (strncmp(l, preambles[i], len) == 0) {
            memmove(l, l+len, strlen(l)-len+1);
            break;
        }
    }

    /* Replace function words FIRST (longest match) */
    for (int i = 0; fn_words[i].word; i++) {
        char *pos = strstr(l, fn_words[i].word);
        if (!pos) continue;
        int  wlen = strlen(fn_words[i].word);
        char fn_tag[32];
        switch (fn_words[i].fn_id) {
            case FN_SQRT:  snprintf(fn_tag, sizeof(fn_tag), "sqrt("); break;
            case FN_ABS:   snprintf(fn_tag, sizeof(fn_tag), "abs(");  break;
            case FN_FLOOR: snprintf(fn_tag, sizeof(fn_tag), "floor(");break;
            case FN_CEIL:  snprintf(fn_tag, sizeof(fn_tag), "ceil("); break;
            case FN_LOG:   snprintf(fn_tag, sizeof(fn_tag), "log(");  break;
            case FN_LOG10: snprintf(fn_tag, sizeof(fn_tag), "log10(");break;
            default: fn_tag[0]='\0';
        }
        char tmp[512];
        int before = (int)(pos - l);
        snprintf(tmp, sizeof(tmp), "%.*s%s%s)", before, l, fn_tag, pos+wlen);
        strncpy(l, tmp, 511);
    }

    /* Replace multi-word operators (longest first) */
    for (int i = 0; op_words[i].word; i++) {
        char *pos;
        while ((pos = strstr(l, op_words[i].word))) {
            int wlen = strlen(op_words[i].word);
            char rep[4] = {' ', op_words[i].op, ' ', '\0'};
            /* Handle "squared"/"cubed" as postfix */
            if (op_words[i].op == 'Q') strncpy(rep, "^2", 4);
            if (op_words[i].op == 'C') strncpy(rep, "^3", 4);
            char tmp[512];
            int before = (int)(pos - l);
            snprintf(tmp, sizeof(tmp), "%.*s%s%s", before, l, rep, pos+wlen);
            strncpy(l, tmp, 511);
        }
    }

    /* Replace number words with digits (scan tokens) */
    char result[512] = {0};
    char *tok = l;
    while (*tok) {
        /* Skip whitespace → copy spaces */
        while (*tok && isspace((unsigned char)*tok)) {
            strncat(result, " ", sizeof(result)-strlen(result)-1);
            tok++;
        }
        if (!*tok) break;

        /* Try to match number word at current position */
        int matched = 0;
        for (int i = 0; num_words[i].word; i++) {
            int wlen = strlen(num_words[i].word);
            if (strncmp(tok, num_words[i].word, wlen) == 0 &&
                (!tok[wlen] || isspace((unsigned char)tok[wlen]) || tok[wlen]==')'||tok[wlen]=='(')) {
                char nbuf[32]; snprintf(nbuf, sizeof(nbuf), "%lld", num_words[i].value);
                strncat(result, nbuf, sizeof(result)-strlen(result)-1);
                tok += wlen;
                matched = 1;
                break;
            }
        }
        if (!matched) {
            /* Copy character as-is */
            char ch[2] = {*tok++, '\0'};
            strncat(result, ch, sizeof(result)-strlen(result)-1);
        }
    }

    /* Compact spaces around operators */
    char final[512] = {0};
    char *p = result;
    char *f = final;
    int last_was_space = 0;
    while (*p) {
        if (isspace((unsigned char)*p)) {
            if (!last_was_space) { *f++ = ' '; last_was_space = 1; }
        } else {
            *f++ = *p;
            last_was_space = 0;
        }
        p++;
    }
    *f = '\0';

    /* Trim leading/trailing space */
    char *start = final;
    while (*start == ' ') start++;
    int len = strlen(start);
    while (len > 0 && start[len-1] == ' ') start[--len] = '\0';

    strncpy(out, start, outlen-1);
}

/* ═══════════════════════════════════════════════════════════════
   RECURSIVE DESCENT EVALUATOR
   Implements full BODMAS:
     Brackets → Orders (^) → Division/Multiplication → Add/Sub
   ═══════════════════════════════════════════════════════════════ */

static const char *eval_pos;
static int eval_error;

static double parse_expr(void);
static double parse_term(void);
static double parse_power(void);
static double parse_unary(void);
static double parse_primary(void);

static void eval_skip_ws(void) {
    while (*eval_pos && isspace((unsigned char)*eval_pos)) eval_pos++;
}

static double parse_primary(void) {
    eval_skip_ws();

    /* Bracketed expression */
    if (*eval_pos == '(') {
        eval_pos++;
        double val = parse_expr();
        eval_skip_ws();
        if (*eval_pos == ')') eval_pos++;
        else eval_error = 1;
        return val;
    }

    /* Built-in functions */
    const struct { const char *name; int fn; } fns[] = {
        {"sqrt(",  FN_SQRT}, {"abs(",   FN_ABS},
        {"floor(", FN_FLOOR},{"ceil(",  FN_CEIL},
        {"log(",   FN_LOG},  {"log10(", FN_LOG10},
        {NULL, 0}
    };
    for (int i = 0; fns[i].name; i++) {
        int l = strlen(fns[i].name);
        if (strncmp(eval_pos, fns[i].name, l) == 0) {
            eval_pos += l;
            double arg = parse_expr();
            eval_skip_ws();
            if (*eval_pos == ')') eval_pos++;
            switch (fns[i].fn) {
                case FN_SQRT:  return sqrt(arg);
                case FN_ABS:   return fabs(arg);
                case FN_FLOOR: return floor(arg);
                case FN_CEIL:  return ceil(arg);
                case FN_LOG:   return (arg>0)?log(arg):0;
                case FN_LOG10: return (arg>0)?log10(arg):0;
            }
        }
    }

    /* Number (integer or decimal) */
    char *endp;
    double val = strtod(eval_pos, &endp);
    if (endp == eval_pos) { eval_error = 1; return 0; }
    eval_pos = endp;
    return val;
}

static double parse_unary(void) {
    eval_skip_ws();
    if (*eval_pos == '-') { eval_pos++; return -parse_primary(); }
    if (*eval_pos == '+') { eval_pos++; return  parse_primary(); }
    return parse_primary();
}

static double parse_power(void) {
    double base = parse_unary();
    eval_skip_ws();
    if (*eval_pos == '^') {
        eval_pos++;
        double exp = parse_power(); /* right-associative */
        return pow(base, exp);
    }
    return base;
}

static double parse_term(void) {
    double val = parse_power();
    eval_skip_ws();
    while (*eval_pos == '*' || *eval_pos == '/' || *eval_pos == '%') {
        char op = *eval_pos++;
        double right = parse_power();
        if (op == '*') val *= right;
        else if (op == '/') {
            if (right == 0) { eval_error = 1; return 0; }
            val /= right;
        } else {
            if ((long long)right == 0) { eval_error = 1; return 0; }
            val = fmod(val, right);
        }
        eval_skip_ws();
    }
    return val;
}

static double parse_expr(void) {
    double val = parse_term();
    eval_skip_ws();
    while (*eval_pos == '+' || *eval_pos == '-') {
        char op = *eval_pos++;
        double right = parse_term();
        if (op == '+') val += right;
        else           val -= right;
        eval_skip_ws();
    }
    return val;
}

/* ═══════════════════════════════════════════════════════════════
   DETECT
   ═══════════════════════════════════════════════════════════════ */

int math_detect(const char *input) {
    if (!input || !input[0]) return 0;
    char l[512]; lower_copy(l, input, 512);

    /* Contains digit + operator */
    if (strpbrk(l, "0123456789")) {
        if (strpbrk(l, "+-*/%^") ||
            strstr(l, "plus") || strstr(l, "minus")  ||
            strstr(l, "times") || strstr(l, "divided") ||
            strstr(l, "modulo") || strstr(l, "power")) return 1;
    }

    /* Contains number word + operator word */
    const char *nw[] = {"zero","one","two","three","four","five","six","seven",
                        "eight","nine","ten","eleven","twelve","thirteen",
                        "fourteen","fifteen","sixteen","seventeen","eighteen",
                        "nineteen","twenty","thirty","forty","fifty",
                        "sixty","seventy","eighty","ninety","hundred",
                        "thousand","million",NULL};
    const char *ow[] = {"plus","minus","times","divided","multiplied",
                        "subtract","add","power","squared","cubed",
                        "square root","modulo",NULL};
    int has_num = 0, has_op = 0;
    for (int i = 0; nw[i]; i++) if (strstr(l, nw[i])) { has_num=1; break; }
    for (int i = 0; ow[i]; i++) if (strstr(l, ow[i])) { has_op=1;  break; }

    if (has_num && has_op) return 1;

    /* Explicit function call */
    if (strstr(l, "square root") || strstr(l, "sqrt")) return 1;

    return 0;
}

/* ═══════════════════════════════════════════════════════════════
   EVALUATE
   ═══════════════════════════════════════════════════════════════ */

MathResult math_evaluate(const char *input) {
    MathResult r = {0, {0}, {0}, {0}, 0, 0, {0}};
    if (!input || !input[0]) {
        strncpy(r.error, "Empty input.", sizeof(r.error)-1);
        return r;
    }

    /* Normalise to numeric infix */
    char normalised[512];
    normalise_expression(input, normalised, sizeof(normalised));

    printf("[MATH] Expression: \"%s\" → \"%s\"\n", input, normalised);

    /* Evaluate */
    eval_pos   = normalised;
    eval_error = 0;
    double val = parse_expr();

    if (eval_error) {
        snprintf(r.error, sizeof(r.error),
                 "Could not parse: \"%s\"", normalised);
        return r;
    }

    r.value      = val;
    r.success    = 1;
    r.is_integer = (val == floor(val) && val >= -1e15 && val <= 1e15);

    /* Numeric string */
    if (r.is_integer)
        snprintf(r.numeric_str, sizeof(r.numeric_str), "%lld", (long long)val);
    else
        snprintf(r.numeric_str, sizeof(r.numeric_str), "%.6g", val);

    /* Word string — only for integers in reasonable range */
    if (r.is_integer && val >= -999999999LL && val <= 999999999LL) {
        math_number_to_words((long long)val, r.word_str, sizeof(r.word_str));
        snprintf(r.full_str, sizeof(r.full_str), "%s (%s)",
                 r.numeric_str, r.word_str);
    } else {
        strncpy(r.full_str, r.numeric_str, sizeof(r.full_str)-1);
    }

    return r;
}

void math_init(void) {
    printf("[MATH] BODMAS engine ready. "
           "Supports: +-*/^%% brackets functions word-numbers.\n");
}
