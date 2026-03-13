/*
 * ╔══════════════════════════════════════════════════════════════╗
 * ║  BLAF — blaf_instructions.h / blaf_instructions.c          ║
 * ║  Session Instruction Retainer                               ║
 * ║                                                              ║
 * ║  Retains user instructions like:                            ║
 * ║    "short answer", "formal tone", "don't use bullets"       ║
 * ║  Instructions persist until explicitly cleared.             ║
 * ╚══════════════════════════════════════════════════════════════╝
 */

#ifndef BLAF_INSTRUCTIONS_H
#define BLAF_INSTRUCTIONS_H

#include <stdint.h>

/* ═══════════════════════════════════════════════════════════════
   INSTRUCTION FLAGS (bitmask — can combine)
   ═══════════════════════════════════════════════════════════════ */

#define INST_NONE          0x0000
#define INST_SHORT_ANSWER  0x0001   /* Keep responses brief           */
#define INST_LONG_ANSWER   0x0002   /* Give detailed responses        */
#define INST_FORMAL_TONE   0x0004   /* Use formal language            */
#define INST_CASUAL_TONE   0x0008   /* Use casual language            */
#define INST_NO_BULLETS    0x0010   /* No bullet points               */
#define INST_BULLETS_ONLY  0x0020   /* Bullet points preferred        */
#define INST_EXPLAIN       0x0040   /* Always explain reasoning       */
#define INST_NO_EXPLAIN    0x0080   /* Skip reasoning, just answer    */
#define INST_WEB_ON        0x0100   /* Always search web              */
#define INST_WEB_OFF       0x0200   /* Never search web               */
#define INST_CONTEXT_ON    0x0400   /* Always give context            */
#define INST_CONTEXT_OFF   0x0800   /* Skip context                   */

/* Max free-form custom instructions */
#define MAX_CUSTOM_INST    32
#define MAX_INST_LEN       128

/* ═══════════════════════════════════════════════════════════════
   INSTRUCTION STATE
   ═══════════════════════════════════════════════════════════════ */

typedef struct {
    uint32_t flags;                          /* bitmask of active flags   */
    char     custom[MAX_CUSTOM_INST][MAX_INST_LEN]; /* free-form rules    */
    int      custom_count;
    int      max_response_words;             /* 0 = no limit              */
} InstructionState;

/* ═══════════════════════════════════════════════════════════════
   API
   ═══════════════════════════════════════════════════════════════ */

void instructions_init (void);

/*
 * parse_instruction()
 * Detects if user input is an instruction and applies it.
 * Returns 1 if input was an instruction (caller should not process further).
 * Returns 0 if input is a normal query.
 *
 * Examples:
 *   parse_instruction("short answer")    → sets INST_SHORT_ANSWER
 *   parse_instruction("be formal")       → sets INST_FORMAL_TONE
 *   parse_instruction("don't use bullets") → sets INST_NO_BULLETS
 *   parse_instruction("forget all instructions") → clears all
 */
int parse_instruction(const char *input);

/* Direct flag setters */
void set_instruction_flag  (uint32_t flag);
void clear_instruction_flag(uint32_t flag);
int  has_instruction       (uint32_t flag);

/* Add / remove a custom free-form instruction */
int  add_custom_instruction   (const char *text);
int  remove_custom_instruction(const char *text);

/* Get the full state (for use by output engine) */
const InstructionState* get_instructions(void);

/* Print all active instructions */
void instructions_list(void);

/* Clear everything */
void instructions_clear(void);

#endif /* BLAF_INSTRUCTIONS_H */
