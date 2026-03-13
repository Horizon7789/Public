/*
 * ╔══════════════════════════════════════════════════════════════╗
 * ║  BLAF — blaf_instructions.c                                 ║
 * ╚══════════════════════════════════════════════════════════════╝
 */

#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include "blaf_instructions.h"

static InstructionState g_inst = {INST_NONE, {{0}}, 0, 0};
static int g_init = 0;

/* ── helpers ── */
static void lower_copy(char *d, const char *s, int n) {
    int i; for (i=0;i<n-1&&s[i];i++) d[i]=tolower((unsigned char)s[i]); d[i]='\0';
}
static int contains(const char *hay, const char *needle) {
    return strstr(hay, needle) != NULL;
}

void instructions_init(void) {
    if (g_init) return;
    g_init = 1;
    memset(&g_inst, 0, sizeof(g_inst));
    printf("[INSTRUCTIONS] Retainer initialized.\n");
}

/* ═══════════════════════════════════════════════════════════════
   INSTRUCTION PARSER
   Detects common patterns and maps them to flags.
   ═══════════════════════════════════════════════════════════════ */

int parse_instruction(const char *input) {
    char l[512];
    lower_copy(l, input, 512);

    /* CLEAR ALL */
    if (contains(l, "forget") || contains(l, "clear all") ||
        contains(l, "reset instructions") || contains(l, "start over")) {
        instructions_clear();
        printf("[INSTRUCTIONS] All instructions cleared.\n");
        return 1;
    }

    /* SHORT ANSWER */
    if (contains(l, "short answer") || contains(l, "keep it short") ||
        contains(l, "be brief")     || contains(l, "summarize only")) {
        set_instruction_flag(INST_SHORT_ANSWER);
        clear_instruction_flag(INST_LONG_ANSWER);
        printf("[INSTRUCTIONS] ✓ Short answer mode ON.\n");
        return 1;
    }

    /* LONG / DETAILED */
    if (contains(l, "long answer")   || contains(l, "detailed answer") ||
        contains(l, "explain fully") || contains(l, "be thorough")) {
        set_instruction_flag(INST_LONG_ANSWER);
        clear_instruction_flag(INST_SHORT_ANSWER);
        printf("[INSTRUCTIONS] ✓ Long answer mode ON.\n");
        return 1;
    }

    /* FORMAL */
    if (contains(l, "formal") || contains(l, "professional tone")) {
        set_instruction_flag(INST_FORMAL_TONE);
        clear_instruction_flag(INST_CASUAL_TONE);
        printf("[INSTRUCTIONS] ✓ Formal tone ON.\n");
        return 1;
    }

    /* CASUAL */
    if (contains(l, "casual") || contains(l, "relaxed") ||
        contains(l, "informal")) {
        set_instruction_flag(INST_CASUAL_TONE);
        clear_instruction_flag(INST_FORMAL_TONE);
        printf("[INSTRUCTIONS] ✓ Casual tone ON.\n");
        return 1;
    }

    /* NO BULLETS */
    if (contains(l, "no bullet") || contains(l, "don't use bullet") ||
        contains(l, "no list")   || contains(l, "no formatting")) {
        set_instruction_flag(INST_NO_BULLETS);
        clear_instruction_flag(INST_BULLETS_ONLY);
        printf("[INSTRUCTIONS] ✓ No bullet points.\n");
        return 1;
    }

    /* BULLETS */
    if (contains(l, "use bullets") || contains(l, "bullet points") ||
        contains(l, "list format")) {
        set_instruction_flag(INST_BULLETS_ONLY);
        clear_instruction_flag(INST_NO_BULLETS);
        printf("[INSTRUCTIONS] ✓ Bullet point format ON.\n");
        return 1;
    }

    /* ALWAYS EXPLAIN */
    if (contains(l, "always explain") || contains(l, "show reasoning") ||
        contains(l, "explain your")) {
        set_instruction_flag(INST_EXPLAIN);
        clear_instruction_flag(INST_NO_EXPLAIN);
        printf("[INSTRUCTIONS] ✓ Always explain reasoning.\n");
        return 1;
    }

    /* NO EXPLAIN */
    if (contains(l, "don't explain") || contains(l, "no explanation") ||
        contains(l, "skip reasoning")) {
        set_instruction_flag(INST_NO_EXPLAIN);
        clear_instruction_flag(INST_EXPLAIN);
        printf("[INSTRUCTIONS] ✓ Skip explanations.\n");
        return 1;
    }

    /* WEB ON/OFF */
    if (contains(l, "always search") || contains(l, "use web") ||
        contains(l, "search online")) {
        set_instruction_flag(INST_WEB_ON);
        clear_instruction_flag(INST_WEB_OFF);
        printf("[INSTRUCTIONS] ✓ Web search always ON.\n");
        return 1;
    }
    if (contains(l, "no web") || contains(l, "offline only") ||
        contains(l, "don't search")) {
        set_instruction_flag(INST_WEB_OFF);
        clear_instruction_flag(INST_WEB_ON);
        printf("[INSTRUCTIONS] ✓ Web search OFF.\n");
        return 1;
    }

    /* CONTEXT ON/OFF */
    if (contains(l, "give context") || contains(l, "always give context") ||
        contains(l, "add context")) {
        set_instruction_flag(INST_CONTEXT_ON);
        clear_instruction_flag(INST_CONTEXT_OFF);
        printf("[INSTRUCTIONS] ✓ Always provide context.\n");
        return 1;
    }
    if (contains(l, "no context") || contains(l, "skip context")) {
        set_instruction_flag(INST_CONTEXT_OFF);
        clear_instruction_flag(INST_CONTEXT_ON);
        printf("[INSTRUCTIONS] ✓ Skip context.\n");
        return 1;
    }

    /* Word limit: "answer in X words" */
    int word_limit = 0;
    if (sscanf(l, "answer in %d word", &word_limit) == 1 ||
        sscanf(l, "max %d word", &word_limit) == 1) {
        g_inst.max_response_words = word_limit;
        printf("[INSTRUCTIONS] ✓ Max response: %d words.\n", word_limit);
        return 1;
    }

    /* "don't do X" or "never X" — store as custom */
    if (contains(l, "don't ") || contains(l, "do not ") ||
        contains(l, "never ")  || contains(l, "always ") ||
        contains(l, "stop ")) {
        if (add_custom_instruction(input) == 0) {
            printf("[INSTRUCTIONS] ✓ Custom rule saved: \"%s\"\n", input);
            return 1;
        }
    }

    /* List instructions */
    if (contains(l, "what are my instructions") ||
        contains(l, "show instructions") ||
        contains(l, "list instructions")) {
        instructions_list();
        return 1;
    }

    return 0; /* not an instruction */
}

/* ═══════════════════════════════════════════════════════════════
   FLAG OPERATIONS
   ═══════════════════════════════════════════════════════════════ */

void set_instruction_flag(uint32_t flag)   { g_inst.flags |= flag; }
void clear_instruction_flag(uint32_t flag) { g_inst.flags &= ~flag; }
int  has_instruction(uint32_t flag)        { return (g_inst.flags & flag) != 0; }

int add_custom_instruction(const char *text) {
    if (g_inst.custom_count >= MAX_CUSTOM_INST) return -1;
    strncpy(g_inst.custom[g_inst.custom_count++], text, MAX_INST_LEN-1);
    return 0;
}

int remove_custom_instruction(const char *text) {
    for (int i = 0; i < g_inst.custom_count; i++) {
        if (strcasecmp(g_inst.custom[i], text) == 0) {
            for (int j = i; j < g_inst.custom_count-1; j++)
                strncpy(g_inst.custom[j], g_inst.custom[j+1], MAX_INST_LEN-1);
            g_inst.custom_count--;
            return 0;
        }
    }
    return -1;
}

const InstructionState* get_instructions(void) { return &g_inst; }

void instructions_list(void) {
    printf("\n[INSTRUCTIONS] Active rules:\n");
    if (g_inst.flags == 0 && g_inst.custom_count == 0) {
        printf("  (none)\n\n"); return;
    }
    if (has_instruction(INST_SHORT_ANSWER))  printf("  • Short answers\n");
    if (has_instruction(INST_LONG_ANSWER))   printf("  • Long/detailed answers\n");
    if (has_instruction(INST_FORMAL_TONE))   printf("  • Formal tone\n");
    if (has_instruction(INST_CASUAL_TONE))   printf("  • Casual tone\n");
    if (has_instruction(INST_NO_BULLETS))    printf("  • No bullet points\n");
    if (has_instruction(INST_BULLETS_ONLY))  printf("  • Bullet format\n");
    if (has_instruction(INST_EXPLAIN))       printf("  • Always explain\n");
    if (has_instruction(INST_NO_EXPLAIN))    printf("  • No explanations\n");
    if (has_instruction(INST_WEB_ON))        printf("  • Web always ON\n");
    if (has_instruction(INST_WEB_OFF))       printf("  • Web OFF\n");
    if (has_instruction(INST_CONTEXT_ON))    printf("  • Always give context\n");
    if (has_instruction(INST_CONTEXT_OFF))   printf("  • No context\n");
    if (g_inst.max_response_words > 0)
        printf("  • Max %d words\n", g_inst.max_response_words);
    for (int i = 0; i < g_inst.custom_count; i++)
        printf("  • %s\n", g_inst.custom[i]);
    printf("\n");
}

void instructions_clear(void) {
    memset(&g_inst, 0, sizeof(g_inst));
}
