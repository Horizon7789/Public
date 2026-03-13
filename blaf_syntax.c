#include "blaf_syntax.h"
#include <string.h>
#include <stdio.h>

SentenceSchema grammar_library[MAX_GRAMMAR_LIBRARY];
int schema_count = 0;

void syntax_record_sentence(const char *input) {
    char buf[512];
    strncpy(buf, input, 511);
    
    uint8_t current_pattern[MAX_SCHEMA_LENGTH];
    int p_idx = 0;

    char *tok = strtok(buf, " \t\n");
    while (tok && p_idx < MAX_SCHEMA_LENGTH) {
        // Use your existing get_pos_entry from blaf_grammar.c
        const POSEntry *pos = get_pos_entry(tok);
        
        if (pos) {
            current_pattern[p_idx++] = pos->class_tag;
        } else {
            // If not in POS tables, it's likely a General Noun/Verb from your concept table
            current_pattern[p_idx++] = CLASS_NOUN; // Default heuristic
        }
        tok = strtok(NULL, " \t\n");
    }

    // Check if this DNA already exists
    for (int i = 0; i < schema_count; i++) {
        if (grammar_library[i].length == p_idx && 
            memcmp(grammar_library[i].pos_sequence, current_pattern, p_idx) == 0) {
            grammar_library[i].frequency++;
            return;
        }
    }

    // New Schema Discovery
    if (schema_count < MAX_GRAMMAR_LIBRARY) {
        grammar_library[schema_count].length = p_idx;
        memcpy(grammar_library[schema_count].pos_sequence, current_pattern, p_idx);
        grammar_library[schema_count].frequency = 1;
        schema_count++;
    }
}


#include "blaf_core.h" // For get_word_from_hash

char* syntax_generate_from_fact(uint32_t s_hash, uint32_t v_hash, uint32_t o_hash) {
    static char output[512];
    const char *s_word = get_word_from_hash(s_hash);
    const char *v_word = get_word_from_hash(v_hash);
    const char *o_word = get_word_from_hash(o_hash);

    if (!s_word || !v_word || !o_word) return "I have the data, but I'm missing the words to say it.";

    // 1. Search for a suitable schema
    // For now, we look for a schema that has at least 3 parts (S-V-O)
    SentenceSchema *best_fit = NULL;
    for (int i = 0; i < schema_count; i++) {
        if (grammar_library[i].length >= 3) {
            best_fit = &grammar_library[i];
            break; 
        }
    }

    // 2. The Fallback: If no schema is found, use a basic Subject-Verb-Object
    if (!best_fit) {
        snprintf(output, sizeof(output), "%s %s %s.", s_word, v_word, o_word);
        return output;
    }

    // 3. The Generative Fill: Map hashes into the learned structure
    char construction[512] = {0};
    int s_filled = 0, v_filled = 0, o_filled = 0;

    for (int i = 0; i < best_fit->length; i++) {
        uint8_t cls = best_fit->pos_sequence[i];

        if (cls == CLASS_NOUN && !s_filled) {
            strcat(construction, s_word); s_filled = 1;
        } else if ((cls == CLASS_VERB || cls == CLASS_AUX_VERB) && !v_filled) {
            strcat(construction, v_word); v_filled = 1;
        } else if (cls == CLASS_NOUN && s_filled && !o_filled) {
            strcat(construction, o_word); o_filled = 1;
        } else if (cls == CLASS_ARTICLE) {
            strcat(construction, "the"); // Simple heuristic filler
        }
        
        if (i < best_fit->length - 1) strcat(construction, " ");
    }
    
    strcat(construction, ".");
    strncpy(output, construction, sizeof(output)-1);
    return output;
}

void syntax_init(void) {
    FILE *fp = fopen("blaf_syntax.bin", "rb");
    if (!fp) {
        schema_count = 0;
        printf("[SYNTAX] No library found, starting fresh.\n");
        return;
    }
    fread(&schema_count, sizeof(int), 1, fp);
    fread(grammar_library, sizeof(SentenceSchema), schema_count, fp);
    fclose(fp);
    printf("[SYNTAX] Loaded %d sentence patterns.\n", schema_count);
}


