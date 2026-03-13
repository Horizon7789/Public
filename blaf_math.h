/*
 * ╔══════════════════════════════════════════════════════════════╗
 * ║  BLAF — blaf_math.h                                         ║
 * ║  BODMAS Arithmetic Engine                                   ║
 * ║                                                              ║
 * ║  Handles:                                                    ║
 * ║    - Numeric:  3 + 4 * 2 / (1 + 1)                         ║
 * ║    - Words:    two plus four times three                     ║
 * ║    - Mixed:    2 plus three * 4                             ║
 * ║    - Output:   23 (twenty three)                            ║
 * ║                                                              ║
 * ║  Order: Brackets → Orders → Division →                      ║
 * ║         Multiplication → Addition → Subtraction             ║
 * ╚══════════════════════════════════════════════════════════════╝
 */

#ifndef BLAF_MATH_H
#define BLAF_MATH_H

/* ═══════════════════════════════════════════════════════════════
   RESULT
   ═══════════════════════════════════════════════════════════════ */

typedef struct {
    double value;
    char   numeric_str[64];   /* "23"                  */
    char   word_str[256];     /* "twenty three"        */
    char   full_str[320];     /* "23 (twenty three)"   */
    int    is_integer;        /* 1 if .0 remainder     */
    int    success;           /* 0 = parse/math error  */
    char   error[128];        /* error message if any  */
} MathResult;

/* ═══════════════════════════════════════════════════════════════
   API
   ═══════════════════════════════════════════════════════════════ */

void math_init(void);

/*
 * math_detect()
 * Returns 1 if input looks like a math expression.
 * Call this BEFORE sending to the query pipeline.
 *
 * Examples that return 1:
 *   "3 + 4"
 *   "two plus two"
 *   "what is 10 divided by 2"
 *   "square root of 16"
 *   "(3 + 4) * 2"
 */
int math_detect(const char *input);

/*
 * math_evaluate()
 * Parse and evaluate a math expression.
 * Handles numeric, word, and mixed forms.
 *
 * Examples:
 *   math_evaluate("2 + 2")           → value=4, full="4 (four)"
 *   math_evaluate("two plus two")    → value=4, full="4 (four)"
 *   math_evaluate("(3+4) * 2^3")     → value=56, full="56 (fifty six)"
 *   math_evaluate("10 / 4")          → value=2.5, full="2.5"
 *   math_evaluate("square root of 9")→ value=3, full="3 (three)"
 */
MathResult math_evaluate(const char *input);

/*
 * math_number_to_words()
 * Convert a number to its English word form.
 *
 *   math_number_to_words(42, out, sizeof(out))
 *   → "forty two"
 *
 *   math_number_to_words(1000000, out, sizeof(out))
 *   → "one million"
 */
void math_number_to_words(long long n, char *out, int outlen);

/*
 * math_words_to_number()
 * Convert English word form to a number.
 *
 *   math_words_to_number("forty two") → 42
 *   math_words_to_number("one hundred and five") → 105
 * Returns 0 and sets *ok=0 on failure.
 */
double math_words_to_number(const char *words, int *ok);

#endif /* BLAF_MATH_H */
