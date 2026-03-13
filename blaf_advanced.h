/*
 * ╔══════════════════════════════════════════════════════════════╗
 * ║         BLAF — Bit-Level AI Flow                            ║
 * ║         blaf_advanced.h — Advanced Engine Functions         ║
 * ║                                                              ║
 * ║  Compile: gcc -O2 -o blaf blaf.c blaf_mapper.c              ║
 * ║                          blaf_advanced.c                    ║
 * ╚══════════════════════════════════════════════════════════════╝
 */

#ifndef BLAF_ADVANCED_H
#define BLAF_ADVANCED_H

#include <stdint.h>
#include "blaf_mapper.h"

/* ═══════════════════════════════════════════════════════════════
   FORWARD STRUCT DECLARATIONS
   (full definitions in blaf_advanced.c)
   ═══════════════════════════════════════════════════════════════ */

typedef struct SuperpositionPath  SuperpositionPath;
typedef struct MetaPointer        MetaPointer;
typedef struct BitTrace           BitTrace;
typedef struct Fingerprint        Fingerprint;
typedef struct ResponseTemplate   ResponseTemplate;

/* ═══════════════════════════════════════════════════════════════
   I. WEB LOOKUP & DUAL SOURCE VERIFICATION
   ═══════════════════════════════════════════════════════════════ */

/*
 * web_lookup()
 * Fetches word definition from Wikipedia or Encyclopedia.
 * Parses part-of-speech and sector from the article.
 * Registers the word at TRUST_SANDBOX via map_word_web().
 *
 * Returns 1 if found and mapped, 0 if not found.
 *
 * Example:
 *   web_lookup("reentrancy");
 */
int web_lookup(const char *word);

/*
 * dual_source_verify()
 * Queries two independent trusted sources for the same word.
 * If both agree on class + sector → calls promote_integrity().
 * Sources: Wikipedia first, then Encyclopedia Britannica.
 *
 * Returns 1 if both sources agree, 0 if conflict or not found.
 *
 * Example:
 *   dual_source_verify("blockchain");
 */
int dual_source_verify(const char *word);

/* ═══════════════════════════════════════════════════════════════
   II. INTEGRITY PROMOTION
   ═══════════════════════════════════════════════════════════════ */

/*
 * promote_integrity()
 * Bumps a ConceptBlock's trust level by one step after quorum.
 * Ladder: SANDBOX(0) → SINGLE(1) → VERIFIED(3) → IMMUTABLE(7)
 *
 * Returns new trust level, or -1 if word not found.
 *
 * Example:
 *   promote_integrity("blockchain", SECTOR_FINANCE);
 */
int promote_integrity(const char *word, uint8_t sector);

/* ═══════════════════════════════════════════════════════════════
   III. META POINTER COMPRESSION  (1 + 2 = C)
   ═══════════════════════════════════════════════════════════════ */

/* A MetaPointer is a compressed address for a high-frequency pair */
struct MetaPointer {
    uint32_t compressed_id;    /* Unique ID for the compressed pair   */
    uint32_t payload_a;        /* Original block A payload            */
    uint32_t payload_b;        /* Original block B payload            */
    uint8_t  sector;           /* Shared sector of the pair           */
    uint8_t  frequency;        /* How often this pair appears         */
    char     label[32];        /* Human-readable label e.g. "sol_reen"*/
};

#define MAX_META_POINTERS 256

/*
 * meta_pointer_compress()
 * Detects high-frequency concept pairs in the concept table.
 * Compresses them into a MetaPointer for O(1) lookup.
 * threshold = minimum frequency to trigger compression.
 *
 * Returns number of pairs compressed.
 *
 * Example:
 *   meta_pointer_compress(5);  // compress pairs seen 5+ times
 */
int meta_pointer_compress(int threshold);

/*
 * meta_pointer_lookup()
 * Check if two concept payloads have a compressed MetaPointer.
 * Returns pointer to MetaPointer if found, NULL otherwise.
 *
 * Example:
 *   MetaPointer *mp = meta_pointer_lookup(payload_a, payload_b);
 */
MetaPointer* meta_pointer_lookup(uint32_t payload_a, uint32_t payload_b);

/*
 * meta_pointer_unzip()
 * Expands a MetaPointer back to its two original concept payloads.
 * Used for deep-dive technical auditing.
 *
 * Example:
 *   meta_pointer_unzip(mp, &block_a, &block_b);
 */
void meta_pointer_unzip(MetaPointer *mp, uint32_t *out_a, uint32_t *out_b);

/* ═══════════════════════════════════════════════════════════════
   IV. SUPERPOSITION & COLLAPSE GATE
   ═══════════════════════════════════════════════════════════════ */

/* One active interpretation path */
struct SuperpositionPath {
    uint32_t block_payloads[16];  /* Concept chain for this path      */
    int      length;              /* Number of blocks in path         */
    uint8_t  sector;              /* Sector this path resolved to     */
    uint8_t  confidence;          /* Alignment score                  */
    uint8_t  alive;               /* 1 = still active, 0 = collapsed  */
};

#define MAX_SUPER_PATHS 8

/*
 * superposition_mode()
 * Called when a word maps to multiple sectors simultaneously.
 * Forks the processing into parallel paths (one per sector match).
 * Sets the Superposition Bit in the context register.
 *
 * word     = the ambiguous word
 * paths    = output array of SuperpositionPath (caller provides)
 * max_paths= max paths to fork
 *
 * Returns number of active paths forked.
 *
 * Example:
 *   SuperpositionPath paths[MAX_SUPER_PATHS];
 *   int n = superposition_mode("virus", paths, MAX_SUPER_PATHS);
 */
int superposition_mode(const char *word,
                       SuperpositionPath *paths,
                       int max_paths);

/*
 * collapse_gate()
 * Called when the next Identifier word arrives after superposition.
 * Kills all paths whose sector doesn't align with the new context.
 * The surviving path becomes the resolved interpretation.
 *
 * paths      = active SuperpositionPath array
 * path_count = number of paths
 * next_word  = the new Identifier that resolves ambiguity
 *
 * Returns index of the surviving path, -1 if all collapsed.
 *
 * Example:
 *   int winner = collapse_gate(paths, n, "attack");
 *   // "attack" in SECURITY context kills the BIOLOGY path
 */
int collapse_gate(SuperpositionPath *paths,
                  int path_count,
                  const char *next_word);

/* ═══════════════════════════════════════════════════════════════
   V. USER CONFIRMATION  (Phase 2 — personal layer)
   ═══════════════════════════════════════════════════════════════ */

/*
 * user_confirm()
 * Presents an unmapped word to the user for manual classification.
 * Stores result in the PERSONAL layer only — never touches global table.
 * Personal mappings are tagged with a user_id for isolation.
 *
 * word    = the unmapped word
 * user_id = unique identifier for this user's personal layer
 *
 * Returns 0 if user confirmed, 1 if user skipped, -1 on error.
 *
 * NOTE: Phase 2 feature — currently prompts via stdin.
 *       Replace stdin with your UI callback when building the interface.
 *
 * Example:
 *   user_confirm("fomo", "user_42");
 */
int user_confirm(const char *word, const char *user_id);

/*
 * user_lookup()
 * Checks the personal layer for a word before hitting the global table.
 * Returns user's personal ConceptDef if found, NULL otherwise.
 *
 * Example:
 *   ConceptDef *cd = user_lookup("fomo", "user_42");
 */
ConceptDef* user_lookup(const char *word, const char *user_id);

/* ═══════════════════════════════════════════════════════════════
   VI. RESPONSE TEMPLATE ENGINE  (Phase 2 — NL generation)
   ═══════════════════════════════════════════════════════════════ */

/* A response template ties a sector + intent to an output pattern */
struct ResponseTemplate {
    uint8_t sector;             /* Which sector this template serves  */
    uint8_t intent;             /* 0=statement 1=question 2=command   */
    char    pattern[256];       /* Pattern with {NOUN} {VERB} slots   */
};

#define MAX_TEMPLATES 128

/*
 * response_template_engine()
 * Selects the best template for the current reasoning summary,
 * fills in concept slots from the reasoning window,
 * and returns a natural language response string.
 *
 * sector     = dominant sector from reasoning pass
 * intent     = detected intent (0/1/2)
 * slot_words = array of words to fill into template slots
 * slot_count = number of slot words available
 * out_buf    = output buffer for the generated response
 * buf_len    = size of out_buf
 *
 * Returns 0 on success, -1 if no matching template found.
 *
 * Example:
 *   char response[512];
 *   const char *slots[] = {"virus", "infect", "system"};
 *   response_template_engine(SECTOR_BIOLOGY, 0, slots, 3,
 *                            response, sizeof(response));
 */
int response_template_engine(uint8_t sector,
                              uint8_t intent,
                              const char **slot_words,
                              int slot_count,
                              char *out_buf,
                              int buf_len);

/*
 * register_template()
 * Add a new response template at runtime.
 *
 * Example:
 *   register_template(SECTOR_SECURITY, 0,
 *       "The {NOUN} can {VERB} the {NOUN2} if left unpatched.");
 */
int register_template(uint8_t sector, uint8_t intent, const char *pattern);

/* ═══════════════════════════════════════════════════════════════
   VII. BIT TRACE EXPORT  (audit log for smart contracts)
   ═══════════════════════════════════════════════════════════════ */

/* One entry in the bit trace log */
typedef struct {
    char     word[64];
    uint8_t  class_tag;
    uint8_t  sector;
    uint8_t  input_pin;
    uint8_t  output_pin;
    uint8_t  trust;
    uint8_t  gate_result;   /* 1 = OPEN, 0 = BLOCKED               */
    uint32_t payload;
} BitTraceEntry;

struct BitTrace {
    BitTraceEntry entries[64];
    int           count;
    char          input_sentence[512];
    uint8_t       chain_valid;
    uint32_t      trace_hash;   /* Hash of the full trace for verification */
};

/*
 * bit_trace_export()
 * Captures a complete bit trace of the last processed sentence.
 * Writes to a .btrace file for external audit verification.
 *
 * trace    = BitTrace struct to populate
 * filename = output file path (NULL to skip file write)
 *
 * Returns 0 on success, -1 on error.
 *
 * Example:
 *   BitTrace trace;
 *   bit_trace_export(&trace, "audit_20240101.btrace");
 */
int bit_trace_export(BitTrace *trace, const char *filename);

/*
 * bit_trace_verify()
 * Reads a .btrace file and verifies its hash integrity.
 * Used by external auditors to confirm the AI's reasoning.
 *
 * Returns 1 if valid, 0 if tampered, -1 on file error.
 *
 * Example:
 *   int ok = bit_trace_verify("audit_20240101.btrace");
 */
int bit_trace_verify(const char *filename);

/* ═══════════════════════════════════════════════════════════════
   VIII. COLD STORAGE FINGERPRINT  (long-term session memory)
   ═══════════════════════════════════════════════════════════════ */

/* 64-bit summary of a past session's most active sectors */
struct Fingerprint {
    uint64_t mask;          /* Bit mask of dominant sector activity  */
    uint8_t  top_sector;    /* Most active sector in this session    */
    uint32_t session_hash;  /* Hash to identify this session         */
    char     user_id[32];   /* Owner of this fingerprint             */
    char     timestamp[32]; /* When this was created                 */
};

#define MAX_FINGERPRINTS 512

/*
 * cold_storage_save()
 * Saves the current session context as a 64-bit Fingerprint.
 * Called at session end to persist the user's context pattern.
 *
 * user_id  = identifier for this user
 * filename = .bfp file to write fingerprints to
 *
 * Returns 0 on success, -1 on error.
 *
 * Example:
 *   cold_storage_save("user_42", "sessions.bfp");
 */
int cold_storage_save(const char *user_id, const char *filename);

/*
 * cold_storage_ping()
 * Checks if the current input matches any stored Fingerprint.
 * If matched, reloads that session's sector preferences instantly.
 *
 * user_id  = identifier for this user
 * filename = .bfp file to check
 *
 * Returns 1 if a match was found and loaded, 0 if no match.
 *
 * Example:
 *   if (cold_storage_ping("user_42", "sessions.bfp"))
 *       printf("Session context restored.\n");
 */
int cold_storage_ping(const char *user_id, const char *filename);

/*
 * cold_storage_list()
 * Prints all stored fingerprints for a user.
 *
 * Example:
 *   cold_storage_list("user_42", "sessions.bfp");
 */
void cold_storage_list(const char *user_id, const char *filename);

/* ═══════════════════════════════════════════════════════════════
   IX. RESEARCHER KILL SWITCH  (read-only audit lock)
   ═══════════════════════════════════════════════════════════════ */

/*
 * researcher_kill_switch()
 * Locks the entire system into READ-ONLY mode.
 * While locked:
 *   - No new concepts can be added or modified
 *   - No trust levels can be promoted
 *   - No personal layer writes
 *   - All lookups still work normally
 *   - All bit traces are automatically exported
 *
 * Designed for use during sensitive security audits where
 * the AI must not learn or leak any patterns.
 *
 * lock = 1 to engage, 0 to disengage
 *
 * Example:
 *   researcher_kill_switch(1);  // lock before audit
 *   process("audit the contract for reentrancy");
 *   researcher_kill_switch(0);  // unlock after audit
 */
void researcher_kill_switch(int lock);

/*
 * is_locked()
 * Returns 1 if the system is currently in read-only mode.
 *
 * Example:
 *   if (is_locked()) printf("System is in audit mode.\n");
 */
int is_locked(void);

#endif /* BLAF_ADVANCED_H */
