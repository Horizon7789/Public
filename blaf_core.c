/*
 * ╔══════════════════════════════════════════════════════════════╗
 * ║  BLAF — blaf_core.c  v0.6                                   ║
 * ║  Core Engine — Tokenizer, Sentence Builder, Reasoning       ║
 * ║  No main(). Compile with main.c.                            ║
 * ╚══════════════════════════════════════════════════════════════╝
 *
 *  Architecture:
 *  ┌──────────────────────────────────────────────────────────┐
 *  │  INPUT → Tokenizer → Prism Filter → Sentence Buffer      │
 *  │        → Curiosity Engine (unknown word resolver)         │
 *  │        → Chain Validator → Reasoning Window              │
 *  │        → blaf_learn (POS tag + SVO facts)                │
 *  │        → Response Builder → OUTPUT                       │
 *  └──────────────────────────────────────────────────────────┘
 */

#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>

#include "blaf_core.h"
#include "blaf_grammar.h"
#include "blaf_sectors.h"
#include "blaf_advanced.h"
#include "blaf_llm.h"
#include "blaf_syntax.h"
#include "blaf_learn.h"     /* learn_sentence(), learn_paragraph() */

/* Curiosity resolver — defined later in this file */
static void resolve_unknown_word(const char *word);

/* ═══════════════════════════════════════════════════════════════
   GLOBAL STATE
   ═══════════════════════════════════════════════════════════════ */

ConceptEntry    concept_table[MAX_CONCEPTS];
int             concept_count = 0;

Fact            fact_pool[MAX_FACT_POOL];
int             fact_count = 0;

ContextRegister ctx       = {0, SECTOR_GENERAL, 0, 0};
ReasoningWindow reasoning = {{{{{0}},0,0}},0};
SentenceBuffer  sentence  = {0};

/* ═══════════════════════════════════════════════════════════════
   HASHING
   ═══════════════════════════════════════════════════════════════ */

uint32_t hash_word(const char *str) {
    uint32_t hash = 5381;
    int c;
    while ((c = *str++)) hash = ((hash << 5) + hash) + c;
    return hash;
}

const char* get_word_from_hash(uint32_t hash) {
    for (int i = 0; i < concept_count; i++)
        if (hash_word(concept_table[i].word) == hash)
            return concept_table[i].word;
    return NULL;
}

/* ═══════════════════════════════════════════════════════════════
   FACT POOL
   ═══════════════════════════════════════════════════════════════ */

void store_fact_in_pool(uint32_t s_hash, uint32_t v_hash, uint32_t o_hash) {
    uint8_t pred_id = (uint8_t)(v_hash % 255);

    /* Deduplicate */
    for (int i = 0; i < fact_count; i++) {
        if (fact_pool[i].subject_hash == s_hash &&
            fact_pool[i].predicate    == pred_id &&
            fact_pool[i].object_hash  == o_hash) {
            if (fact_pool[i].weight < 255) fact_pool[i].weight++;
            return;
        }
    }

    if (fact_count < MAX_FACT_POOL) {
        fact_pool[fact_count].subject_hash = s_hash;
        fact_pool[fact_count].predicate    = pred_id;
        fact_pool[fact_count].object_hash  = o_hash;
        fact_pool[fact_count].weight       = 1;
        fact_count++;
    } else {
        printf("[POOL] Fact pool full!\n");
    }
}

uint8_t map_verb_to_id(const char *verb) {
    if (strcmp(verb, "is")       == 0) return 0;
    if (strcmp(verb, "has")      == 0) return 1;
    if (strcmp(verb, "contains") == 0) return 2;
    if (strcmp(verb, "uses")     == 0) return 3;
    if (strcmp(verb, "calls")    == 0) return 4;
    return 0xFF;
}

/* ═══════════════════════════════════════════════════════════════
   CONCEPT TABLE
   ═══════════════════════════════════════════════════════════════ */

static int get_next_free_slot(void) {
    for (int i = 0; i < MAX_CONCEPTS; i++) {
        if (concept_table[i].word[0] == '\0') {
            concept_table[i].summary     = NULL;
            concept_table[i].summary_len = 0;
            return i;
        }
    }
    return -1;
}

int add_concept(const char *word, uint8_t type, uint8_t class,
                uint8_t integrity, uint8_t sector,
                uint8_t in_pin, uint8_t out_pin) {
    if (concept_count >= MAX_CONCEPTS) return -1;

    int existing = find_concept_index(word, 0xFF);
    if (existing != -1) return existing;

    int idx = get_next_free_slot();
    if (idx < 0) return -1;

    concept_table[idx].summary     = NULL;
    concept_table[idx].summary_len = 0;

    ConceptEntry *e = &concept_table[concept_count++];
    strncpy(e->word, word, MAX_WORD_LEN - 1);
    e->block.type_tag   = type;
    e->block.class_tag  = class;
    e->block.integrity  = integrity;
    e->block.sector     = sector;
    e->block.input_pin  = in_pin;
    e->block.output_pin = out_pin;
    e->block.payload    = hash_word(word);
    e->sector           = sector;
    e->trust            = integrity;
    return 0;
}

/* ═══════════════════════════════════════════════════════════════
   ACCESSOR FUNCTIONS
   ═══════════════════════════════════════════════════════════════ */

int         get_concept_count  (void)  { return concept_count; }
void*       get_concept_entry  (int i) { return (i>=0&&i<concept_count)?(void*)&concept_table[i]:NULL; }
const char* get_concept_word   (int i) { return (i>=0&&i<concept_count)?concept_table[i].word:NULL; }
uint8_t     get_concept_class  (int i) { return (i>=0&&i<concept_count)?concept_table[i].block.class_tag :0xFF; }
uint8_t     get_concept_sector (int i) { return (i>=0&&i<concept_count)?concept_table[i].block.sector    :0xFF; }
uint8_t     get_concept_trust  (int i) { return (i>=0&&i<concept_count)?concept_table[i].block.integrity :0xFF; }
uint8_t     get_concept_inpin  (int i) { return (i>=0&&i<concept_count)?concept_table[i].block.input_pin :0x00; }
uint8_t     get_concept_outpin (int i) { return (i>=0&&i<concept_count)?concept_table[i].block.output_pin:0x00; }
uint32_t    get_concept_payload(int i) { return (i>=0&&i<concept_count)?concept_table[i].block.payload   :0x00; }

int set_concept_trust(int index, uint8_t trust) {
    if (index < 0 || index >= concept_count) return -1;
    concept_table[index].block.integrity = trust;
    concept_table[index].trust           = trust;
    return 0;
}
int set_concept_sector(int index, uint8_t sector) {
    if (index < 0 || index >= concept_count) return -1;
    concept_table[index].block.sector = sector;
    concept_table[index].sector       = sector;
    return 0;
}
int find_concept_index(const char *word, uint8_t sector) {
    for (int i = 0; i < concept_count; i++) {
        if (strcasecmp(concept_table[i].word, word) == 0 &&
            (concept_table[i].block.sector == sector || sector == 0xFF))
            return i;
    }
    return -1;
}

/* Sentence buffer accessors */
void*       get_last_sentence    (void)  { return (void*)&sentence; }
int         get_last_sentence_len(void)  { return sentence.length; }
const char* get_sentence_word    (int i) { return (i>=0&&i<sentence.length)?sentence.words[i]:NULL; }
void*       get_sentence_block   (int i) { return (i>=0&&i<sentence.length)?(void*)sentence.blocks[i]:NULL; }

/* Prism Filter: sector-aware lookup */
ConceptBlock* lookup(const char *word, uint8_t sector) {
    ConceptBlock *fallback = NULL;
    for (int i = 0; i < concept_count; i++) {
        if (strcasecmp(concept_table[i].word, word) == 0) {
            if (concept_table[i].block.sector == sector)
                return &concept_table[i].block;
            if (concept_table[i].block.sector == SECTOR_GENERAL)
                fallback = &concept_table[i].block;
        }
    }
    return fallback;
}

/* ═══════════════════════════════════════════════════════════════
   POS WEIGHT UPDATE
   Called after blaf_learn tags tokens — updates frequency counters
   so the concept table knows a word's dominant part of speech.
   ═══════════════════════════════════════════════════════════════ */

void update_word_pos_weight(const char *word, int class_tag) {
    int idx = find_concept_index(word, 0xFF);
    if (idx == -1) return;
    if      (class_tag == CLASS_NOUN) concept_table[idx].noun_hits++;
    else if (class_tag == CLASS_VERB) concept_table[idx].verb_hits++;
    else if (class_tag == CLASS_ADJ)  concept_table[idx].adj_hits++;

    /* Promote primary_class when one POS dominates */
    uint16_t best = concept_table[idx].noun_hits;
    uint8_t  cls  = CLASS_NOUN;
    if (concept_table[idx].verb_hits > best) { best = concept_table[idx].verb_hits; cls = CLASS_VERB; }
    if (concept_table[idx].adj_hits  > best) {                                      cls = CLASS_ADJ;  }
    concept_table[idx].primary_class = cls;
}

void sync_concept_pos_with_web(const char *word, int class_tag) {
    int idx = find_concept_index(word, 0xFF);
    if (idx != -1) {
        concept_table[idx].primary_class = (uint8_t)class_tag;
        concept_table[idx].trust         = 7; /* TRUST_VERIFIED */
        concept_table[idx].block.integrity = 7;
    }
}

/* ═══════════════════════════════════════════════════════════════
   INTERNET / ONLINE TEACHER STUBS
   These stubs keep the build clean.
   Real implementations can be added in blaf_syntax.c or a
   new blaf_online.c when an HTTP POS API is available.
   ═══════════════════════════════════════════════════════════════ */

int check_internet_connection(void) {
    /* blaf_learn.c handles all learning locally.
     * Return 0 (offline) so learn_from_sentence() falls through
     * to the local path, which now delegates to blaf_learn. */
    return 0;
}

int tokenize_with_online_teacher(const char *sentence_text, Token *tokens) {
    /* Not implemented — blaf_learn.c's own tagger is used instead. */
    (void)sentence_text; (void)tokens;
    return 0;
}

int tokenize_and_tag_local(const char *sentence_text, Token *tokens) {
    /* Delegate to blaf_learn's tokenizer + POS tagger.
     * We tag into a LearnToken array then convert to Token. */
    LearnToken lt[MAX_TOKENS_PER_SENT];
    memset(lt, 0, sizeof(lt));

    /* We expose a helper from blaf_learn for this */
    extern int learn_tokenize_and_tag(const char *text,
                                       LearnToken *out, int max);
    int n = learn_tokenize_and_tag(sentence_text, lt, MAX_TOKENS_PER_SENT);

    for (int i = 0; i < n && i < MAX_TOKENS; i++) {
        strncpy(tokens[i].word, lt[i].word, MAX_WORD_LEN-1);

        /* Map blaf_learn POSClass → blaf_core CLASS_* */
        switch (lt[i].pos) {
            case POS_NOUN:
            case POS_PROPER:   tokens[i].class = CLASS_NOUN;     break;
            case POS_VERB:     tokens[i].class = CLASS_VERB;     break;
            case POS_AUX_VERB: tokens[i].class = CLASS_AUX_VERB; break;
            case POS_ADJ:      tokens[i].class = CLASS_ADJ;      break;
            case POS_ADV:      tokens[i].class = CLASS_ADV;      break;
            case POS_ARTICLE:  tokens[i].class = CLASS_ARTICLE;  break;
            case POS_PREP:     tokens[i].class = CLASS_PREP;     break;
            case POS_CONJ:     tokens[i].class = CLASS_CONJ;     break;
            case POS_PRONOUN:  tokens[i].class = CLASS_PRONOUN;  break;
            case POS_PUNCT:    tokens[i].class = CLASS_PUNCTUATION; break;
            default:           tokens[i].class = CLASS_NOUN;     break;
        }
        tokens[i].trust = lt[i].trust;
    }
    return n;
}

/* ═══════════════════════════════════════════════════════════════
   LEARN FROM SENTENCE
   Replaces the old stub. Now delegates to blaf_learn's full
   POS tagger + SVO extractor and updates concept POS weights.
   ═══════════════════════════════════════════════════════════════ */

const char* find_sentence_subject(Token *tokens, int t_count) {
    for (int i = 0; i < t_count; i++)
        if (tokens[i].class == CLASS_NOUN ||
            tokens[i].class == CLASS_PRONOUN)
            return tokens[i].word;
    return (t_count > 0) ? tokens[0].word : "";
}

void learn_from_sentence(const char *sentence_text) {
    if (!sentence_text || !sentence_text[0]) return;

    /* Use blaf_learn's proper POS tagger + SVO extractor */
    LearnResult lr = learn_sentence(sentence_text, ctx.active_sector);

    /* Update POS frequency weights in concept table for each tagged word */
    LearnToken lt[MAX_TOKENS_PER_SENT];
    memset(lt, 0, sizeof(lt));
    extern int learn_tokenize_and_tag(const char *text,
                                       LearnToken *out, int max);
    int n = learn_tokenize_and_tag(sentence_text, lt, MAX_TOKENS_PER_SENT);
    for (int i = 0; i < n; i++) {
        int cls = CLASS_NOUN;
        switch (lt[i].pos) {
            case POS_VERB:     cls = CLASS_VERB; break;
            case POS_ADJ:      cls = CLASS_ADJ;  break;
            default:           cls = CLASS_NOUN; break;
        }
        update_word_pos_weight(lt[i].word, cls);
    }

    /* Also record in the hash-based fact pool for brain command compat */
    for (int i = 0; i < lr.fact_count; i++) {
        store_fact_in_pool(
            hash_word(lr.facts[i].subject),
            hash_word(lr.facts[i].predicate),
            hash_word(lr.facts[i].object)
        );
    }

    /* Record sentence structure in blaf_syntax */
    syntax_record_sentence(sentence_text);
}

/* ═══════════════════════════════════════════════════════════════
   EXTRACT FACTS FROM TEXT
   Replaces the old 5-verb stub.
   Now uses blaf_learn's paragraph learner for full SVO extraction.
   ═══════════════════════════════════════════════════════════════ */

void extract_facts_from_text(const char *text, const char *primary_subject) {
    if (!text || !primary_subject) return;

    /* blaf_learn handles sentence splitting, POS tagging, and full
     * SVO/copula/prepositional fact extraction internally */
    int facts_extracted = learn_paragraph(text, ctx.active_sector);

    /* Also push into hash-based pool for brain command backward compat */
    FactTriple triples[32];
    int n = learn_query_facts(primary_subject, triples, 32);
    for (int i = 0; i < n; i++) {
        store_fact_in_pool(
            hash_word(triples[i].subject),
            hash_word(triples[i].predicate),
            hash_word(triples[i].object)
        );
    }

    /* Update saliency of the primary subject */
    int idx = find_concept_index(primary_subject, 0xFF);
    if (idx != -1)
        concept_table[idx].saliency += (uint16_t)(facts_extracted * 5);
}

/* ═══════════════════════════════════════════════════════════════
   LEARN COMPLEX FACT INTERNAL
   Kept for backward compatibility with blaf_core.h declaration.
   Delegates entirely to blaf_learn.
   ═══════════════════════════════════════════════════════════════ */

void learn_complex_fact_internal(Token *tokens, int t_count,
                                  const char *subject) {
    /* Reconstruct sentence from tokens and delegate */
    char sentence_text[512] = {0};
    for (int i = 0; i < t_count; i++) {
        if (i > 0) strncat(sentence_text, " ", sizeof(sentence_text)-strlen(sentence_text)-1);
        strncat(sentence_text, tokens[i].word, sizeof(sentence_text)-strlen(sentence_text)-1);
    }
    LearnResult lr = learn_sentence(sentence_text, ctx.active_sector);
    (void)subject; /* blaf_learn finds subject internally */

    for (int i = 0; i < lr.fact_count; i++) {
        store_fact_in_pool(
            hash_word(lr.facts[i].subject),
            hash_word(lr.facts[i].predicate),
            hash_word(lr.facts[i].object)
        );
    }
}

/* ═══════════════════════════════════════════════════════════════
   CONTEXT REGISTER
   ═══════════════════════════════════════════════════════════════ */

void decay_context(void)  { ctx.register_bits >>= 1; }

void update_context(ConceptBlock *b) {
    if (b->sector != SECTOR_GENERAL) ctx.active_sector = b->sector;
    ctx.register_bits |= ((uint64_t)b->sector << 56);
    decay_context();
}

void reset_context(void) {
    ctx.register_bits = 0;
    ctx.active_sector = SECTOR_GENERAL;
    ctx.superposition = 0;
    ctx.depth         = 0;
}

/* ═══════════════════════════════════════════════════════════════
   TOKENIZER
   ═══════════════════════════════════════════════════════════════ */

void tokenize(const char *input, TokenList *tl) {
    tl->count = 0;
    char buf[MAX_INPUT_LEN];
    strncpy(buf, input, MAX_INPUT_LEN - 1);
    char *tok = strtok(buf, " ,.!?;:\t\n");
    while (tok && tl->count < MAX_TOKENS) {
        for (int i = 0; tok[i]; i++) tok[i] = tolower((unsigned char)tok[i]);
        strncpy(tl->tokens[tl->count++], tok, MAX_WORD_LEN - 1);
        tok = strtok(NULL, " ,.!?;:\t\n");
    }
}

/* ═══════════════════════════════════════════════════════════════
   SENTENCE BUFFER & CHAIN VALIDATOR
   ═══════════════════════════════════════════════════════════════ */

void clear_sentence(SentenceBuffer *sb) {
    memset(sb, 0, sizeof(SentenceBuffer));
    sb->break_at = -1;
}

/* Grammar-backed blocks — one per POS class, reused for grammar words */
static ConceptBlock gram_blocks[16];

int build_sentence(TokenList *tl, SentenceBuffer *sb) {
    clear_sentence(sb);
    int mapped = 0;
    for (int i = 0; i < tl->count && i < MAX_SENTENCE_LEN; i++) {
        strncpy(sb->words[i], tl->tokens[i], MAX_WORD_LEN - 1);

        /* 1. Full concept table — sector-aware prism filter */
        ConceptBlock *b = lookup(tl->tokens[i], ctx.active_sector);
        if (b) {
            sb->blocks[i] = b;
            update_context(b);
            mapped++;
        } else {
            /* 2. Grammar fast-lookup — articles, pronouns, aux-verbs etc.
             *    are always known — never mark them UNMAPPED */
            const POSEntry *pe = get_pos_entry(tl->tokens[i]);
            if (pe) {
                int slot = pe->class_tag & 0x0F;
                gram_blocks[slot].type_tag   = TYPE_LITERAL;
                gram_blocks[slot].class_tag  = pe->class_tag;
                gram_blocks[slot].integrity  = TRUST_IMMUTABLE;
                gram_blocks[slot].sector     = SECTOR_GENERAL;
                gram_blocks[slot].input_pin  = pe->input_pin;
                gram_blocks[slot].output_pin = pe->output_pin;
                gram_blocks[slot].payload    = 0;
                sb->blocks[i] = &gram_blocks[slot];
                update_context(&gram_blocks[slot]);
                mapped++;
            } else {
                sb->blocks[i] = NULL; /* truly unknown */
            }
        }
        sb->length++;
    }
    return mapped;
}

int can_connect(ConceptBlock *a, ConceptBlock *b) {
    return (a->output_pin & b->input_pin) != PIN_NONE;
}

void validate_chain(SentenceBuffer *sb) {
    sb->valid    = 1;
    sb->break_at = -1;
    ConceptBlock *prev = NULL;
    for (int i = 0; i < sb->length; i++) {
        if (!sb->blocks[i]) continue;
        if (prev && !can_connect(prev, sb->blocks[i])) {
            sb->valid    = 0;
            sb->break_at = i;
            return;
        }
        prev = sb->blocks[i];
    }
}

/* ═══════════════════════════════════════════════════════════════
   REASONING WINDOW (16 × 256-bit)
   ═══════════════════════════════════════════════════════════════ */

void clear_reasoning(ReasoningWindow *rw) {
    memset(rw, 0, sizeof(ReasoningWindow));
}

void load_into_reasoning(SentenceBuffer *sb, ReasoningWindow *rw) {
    clear_reasoning(rw);
    int slot_idx = 0, part_idx = 0;
    for (int i = 0; i < sb->length && slot_idx < REASONING_SLOTS; i++) {
        if (!sb->blocks[i]) continue;
        ReasoningSlot *slot = &rw->slots[slot_idx];
        slot->parts[part_idx] = *sb->blocks[i];
        slot->used++;
        if (part_idx > 0) {
            uint8_t overlap = slot->parts[part_idx-1].output_pin
                            & slot->parts[part_idx].input_pin;
            slot->confidence += __builtin_popcount(overlap);
        }
        if (++part_idx >= 4) { slot_idx++; part_idx = 0; }
    }
    rw->active_slots = slot_idx + (part_idx > 0 ? 1 : 0);
}

ReasoningSummary reason(ReasoningWindow *rw) {
    ReasoningSummary rs = {0};
    int sector_counts[256] = {0};
    int class_counts[16]   = {0};
    int total_conf = 0;
    for (int s = 0; s < rw->active_slots; s++) {
        ReasoningSlot *slot = &rw->slots[s];
        total_conf += slot->confidence;
        for (int p = 0; p < slot->used; p++) {
            sector_counts[slot->parts[p].sector]++;
            class_counts[slot->parts[p].class_tag & 0x0F]++;
            if (slot->parts[p].class_tag == CLASS_VERB ||
                slot->parts[p].class_tag == CLASS_AUX_VERB) rs.has_verb = 1;
            if (slot->parts[p].class_tag == CLASS_NOUN ||
                slot->parts[p].class_tag == CLASS_PRONOUN) rs.has_noun = 1;
        }
    }
    int max_s = 0;
    for (int i = 0; i < 256; i++)
        if (sector_counts[i] > max_s) { max_s = sector_counts[i]; rs.dominant_sector = i; }
    int max_c = 0;
    for (int i = 0; i < 16; i++)
        if (class_counts[i] > max_c) { max_c = class_counts[i]; rs.dominant_class = i; }
    rs.avg_confidence = rw->active_slots > 0 ? total_conf / rw->active_slots : 0;
    return rs;
}

/* ═══════════════════════════════════════════════════════════════
   RESPONSE BUILDER
   ═══════════════════════════════════════════════════════════════ */

ResponseFrame build_response(ReasoningSummary *rs, SentenceBuffer *input_sb) {
    ResponseFrame rf = {0};
    rf.sector     = rs->dominant_sector;
    rf.confidence = rs->avg_confidence;
    char *p       = rf.text;
    int   rem     = MAX_RESPONSE_LEN - 1;
    int   n;

    if (!input_sb->valid) {
        snprintf(p, rem,
            "[CHAIN BREAK at word %d] Logic gate blocked — pin mismatch. "
            "Sentence structure invalid.", input_sb->break_at);
        return rf;
    }

    n = snprintf(p, rem,
        "[UNDERSTOOD | Sector: 0x%02X | Confidence: %d | Slots: %d]\n",
        rs->dominant_sector, rs->avg_confidence, reasoning.active_slots);
    p += n; rem -= n;

    for (int i = 0; i < input_sb->length; i++) {
        if (!input_sb->blocks[i])
            n = snprintf(p, rem, "  [%d] %-14s → UNMAPPED (web lookup pending)\n",
                         i, input_sb->words[i]);
        else
            n = snprintf(p, rem,
                "  [%d] %-14s → cls:%-2d sec:0x%02X  "
                "in:0b%08b out:0b%08b trust:%d\n",
                i, input_sb->words[i],
                input_sb->blocks[i]->class_tag,
                input_sb->blocks[i]->sector,
                input_sb->blocks[i]->input_pin,
                input_sb->blocks[i]->output_pin,
                input_sb->blocks[i]->integrity);
        p += n; rem -= n;
    }
    return rf;
}

/* ═══════════════════════════════════════════════════════════════
   CURIOSITY ENGINE
   Resolves unknown words via web → LLM → SANDBOX fallback.
   Never fetches the same word twice per session.
   ═══════════════════════════════════════════════════════════════ */

#define WORD_CACHE_MAX 512
static char g_word_cache[WORD_CACHE_MAX][MAX_WORD_LEN];
static int  g_word_cache_count = 0;

static int word_was_resolved(const char *word) {
    for (int i = 0; i < g_word_cache_count; i++)
        if (strcasecmp(g_word_cache[i], word) == 0) return 1;
    return 0;
}

static void mark_word_resolved(const char *word) {
    if (g_word_cache_count < WORD_CACHE_MAX)
        strncpy(g_word_cache[g_word_cache_count++], word, MAX_WORD_LEN-1);
}

static void resolve_unknown_word(const char *word) {
    if (!word || !word[0]) return;
    if (strlen(word) <= 1)  return;
    /* Skip grammar table words — always known */
    if (get_pos_entry(word))    return;
    if (is_question_word(word)) return;
    /* Skip if already attempted this session */
    if (word_was_resolved(word)) return;

    mark_word_resolved(word);
    printf("║  [CURIOUS] Unknown: \"%s\" — resolving...\n", word);

    /* Step 1: Web lookup */
    int found = web_lookup(word);
    if (found) {
        printf("║  [CURIOUS] \"%s\" mapped via web.\n", word);
        dual_source_verify(word);
        return;
    }

    /* Step 2: LLM classification */
    if (llm_map_concept(word) == 0) {
        printf("║  [CURIOUS] \"%s\" classified by LLM.\n", word);
        return;
    }

    /* Step 3: SANDBOX fallback — at least it's tracked */
    map_word_web(word, CLASS_NOUN, SECTOR_GENERAL, PIN_ANY, PIN_ANY);
    printf("║  [CURIOUS] \"%s\" registered as SANDBOX/NOUN.\n", word);
}

/* ═══════════════════════════════════════════════════════════════
   FULL PROCESSING PIPELINE
   INPUT → TOKENIZE → BUILD → CURIOSITY → VALIDATE
         → LEARN (blaf_learn) → REASON → RESPOND
   ═══════════════════════════════════════════════════════════════ */

void process(const char *input) {
    printf("\n╔══ INPUT ═══════════════════════════════════════════\n");
    printf("║  \"%s\"\n", input);
    printf("╠══ PROCESSING ══════════════════════════════════════\n");

    TokenList tl;
    tokenize(input, &tl);
    printf("║  Tokens  : %d\n", tl.count);

    int mapped = build_sentence(&tl, &sentence);

    /* Curiosity: resolve every unmapped word */
    int resolved_any = 0;
    for (int i = 0; i < sentence.length; i++) {
        if (!sentence.blocks[i]) {
            resolve_unknown_word(sentence.words[i]);
            resolved_any = 1;
        }
    }
    if (resolved_any)
        mapped = build_sentence(&tl, &sentence);

    printf("║  Mapped  : %d / %d\n", mapped, sentence.length);

    validate_chain(&sentence);

    if (sentence.valid) {
        printf("║  Chain   : VALID ✓\n");
        /* Learn using blaf_learn's full POS tagger + SVO extractor.
         * This replaces the old learn_from_sentence() stub and updates:
         *   - fact_pool (hash-based, for brain command)
         *   - blaf_learn fact store (structured, for facts command)
         *   - concept table noun_hits/verb_hits/adj_hits  */
        learn_from_sentence(input);
    } else {
        printf("║  Chain   : BROKEN at word [%d] ✗\n", sentence.break_at);
    }

    load_into_reasoning(&sentence, &reasoning);
    printf("║  Slots   : %d / %d active\n",
           reasoning.active_slots, REASONING_SLOTS);

    ReasoningSummary rs = reason(&reasoning);
    printf("║  Sector  : 0x%02X  |  Class: %d  |  Verb: %d  Noun: %d\n",
           rs.dominant_sector, rs.dominant_class, rs.has_verb, rs.has_noun);

    ResponseFrame rf = build_response(&rs, &sentence);
    printf("╠══ RESPONSE ════════════════════════════════════════\n");
    printf("%s\n", rf.text);
    printf("╚════════════════════════════════════════════════════\n");
}

/* ═══════════════════════════════════════════════════════════════
   PERSISTENCE
   ═══════════════════════════════════════════════════════════════ */

#define PERSIST_MAGIC   0x464C4142u
#define PERSIST_VERSION 0x00000001u
#define PERSIST_PATH    "blaf_knowledge.bin"

int knowledge_save(const char *path) {
    const char *fpath = path ? path : PERSIST_PATH;
    FILE *f = fopen(fpath, "wb");
    if (!f) { fprintf(stderr, "[PERSIST] Cannot write: %s\n", fpath); return -1; }

    uint32_t magic = PERSIST_MAGIC;
    uint32_t ver   = PERSIST_VERSION;
    uint32_t c_qty = (uint32_t)concept_count;

    fwrite(&magic, 4, 1, f);
    fwrite(&ver,   4, 1, f);
    fwrite(&c_qty, 4, 1, f);

    /* Save syntax schemas */
    FILE *fp = fopen("blaf_syntax.bin", "wb");
    if (fp) {
        fwrite(&schema_count, sizeof(int), 1, fp);
        fwrite(grammar_library, sizeof(SentenceSchema), schema_count, fp);
        fclose(fp);
    }

    for (int i = 0; i < concept_count; i++) {
        /* Write fixed struct (summary pointer will be ignored on load) */
        fwrite(&concept_table[i], sizeof(ConceptEntry), 1, f);
        /* Write dynamic summary bytes if present */
        if (concept_table[i].summary_len > 0 &&
            concept_table[i].summary != NULL)
            fwrite(concept_table[i].summary, 1,
                   concept_table[i].summary_len, f);
    }

    /* Write fact pool */
    uint32_t f_qty = (uint32_t)fact_count;
    fwrite(&f_qty,    4,           1,          f);
    fwrite(fact_pool, sizeof(Fact), fact_count, f);

    fclose(f);
    printf("[PERSIST] Saved %d concepts, %d facts.\n",
           concept_count, fact_count);
    return 0;
}

int knowledge_load(const char *path) {
    const char *fpath = path ? path : PERSIST_PATH;
    FILE *f = fopen(fpath, "rb");
    if (!f) {
        printf("[PERSIST] No saved knowledge — starting fresh.\n");
        return 0;
    }

    uint32_t magic, ver, c_qty;
    if (fread(&magic, 4, 1, f) != 1 ||
        fread(&ver,   4, 1, f) != 1) { fclose(f); return 0; }

    if (magic != PERSIST_MAGIC || ver != PERSIST_VERSION) {
        fprintf(stderr, "[PERSIST] Version mismatch — ignoring file.\n");
        fclose(f); return 0;
    }

    fread(&c_qty, 4, 1, f);
    concept_count = (int)c_qty;

    for (int i = 0; i < concept_count; i++) {
        fread(&concept_table[i], sizeof(ConceptEntry), 1, f);
        /* Re-allocate dynamic summary */
        if (concept_table[i].summary_len > 0) {
            concept_table[i].summary =
                malloc(concept_table[i].summary_len + 1);
            if (concept_table[i].summary) {
                fread(concept_table[i].summary, 1,
                      concept_table[i].summary_len, f);
                concept_table[i].summary[concept_table[i].summary_len] = '\0';
            }
        } else {
            concept_table[i].summary = NULL;
        }
    }

    /* Load fact pool */
    uint32_t f_qty;
    if (fread(&f_qty, 4, 1, f) == 1) {
        fact_count = (int)f_qty;
        fread(fact_pool, sizeof(Fact), fact_count, f);
    }

    fclose(f);
    printf("[PERSIST] Loaded %d concepts, %d facts.\n",
           concept_count, fact_count);
    return concept_count;
}

void knowledge_autosave(void) { knowledge_save(NULL); }

/* ═══════════════════════════════════════════════════════════════
   CONCEPT SEED DATABASE
   ═══════════════════════════════════════════════════════════════ */

void seed_concepts(void) {
    /* ARTICLES & FUNCTION WORDS */
    add_concept("the",        TYPE_LITERAL,    CLASS_ARTICLE,  TRUST_IMMUTABLE, SECTOR_GENERAL,  PIN_ANY,                 PIN_NOUN|PIN_ADJ);
    add_concept("a",          TYPE_LITERAL,    CLASS_ARTICLE,  TRUST_IMMUTABLE, SECTOR_GENERAL,  PIN_ANY,                 PIN_NOUN|PIN_ADJ);
    add_concept("an",         TYPE_LITERAL,    CLASS_ARTICLE,  TRUST_IMMUTABLE, SECTOR_GENERAL,  PIN_ANY,                 PIN_NOUN|PIN_ADJ);
    add_concept("is",         TYPE_LITERAL,    CLASS_AUX_VERB, TRUST_IMMUTABLE, SECTOR_GENERAL,  PIN_NOUN|PIN_PRONOUN,    PIN_NOUN|PIN_ADJ|PIN_VERB);
    add_concept("are",        TYPE_LITERAL,    CLASS_AUX_VERB, TRUST_IMMUTABLE, SECTOR_GENERAL,  PIN_NOUN|PIN_PRONOUN,    PIN_NOUN|PIN_ADJ|PIN_VERB);
    add_concept("was",        TYPE_LITERAL,    CLASS_AUX_VERB, TRUST_IMMUTABLE, SECTOR_GENERAL,  PIN_NOUN|PIN_PRONOUN,    PIN_NOUN|PIN_ADJ|PIN_VERB);
    add_concept("can",        TYPE_LITERAL,    CLASS_AUX_VERB, TRUST_IMMUTABLE, SECTOR_GENERAL,  PIN_NOUN|PIN_PRONOUN,    PIN_VERB);
    add_concept("will",       TYPE_LITERAL,    CLASS_AUX_VERB, TRUST_IMMUTABLE, SECTOR_GENERAL,  PIN_NOUN|PIN_PRONOUN,    PIN_VERB);
    add_concept("not",        TYPE_LITERAL,    CLASS_ADV,      TRUST_IMMUTABLE, SECTOR_GENERAL,  PIN_ANY,                 PIN_VERB|PIN_ADJ);
    add_concept("and",        TYPE_LITERAL,    CLASS_CONJ,     TRUST_IMMUTABLE, SECTOR_GENERAL,  PIN_ANY,                 PIN_ANY);
    add_concept("or",         TYPE_LITERAL,    CLASS_CONJ,     TRUST_IMMUTABLE, SECTOR_GENERAL,  PIN_ANY,                 PIN_ANY);
    add_concept("of",         TYPE_LITERAL,    CLASS_PREP,     TRUST_IMMUTABLE, SECTOR_GENERAL,  PIN_NOUN|PIN_VERB,       PIN_NOUN|PIN_ARTICLE);
    add_concept("in",         TYPE_LITERAL,    CLASS_PREP,     TRUST_IMMUTABLE, SECTOR_GENERAL,  PIN_ANY,                 PIN_NOUN|PIN_ARTICLE);
    add_concept("to",         TYPE_LITERAL,    CLASS_PREP,     TRUST_IMMUTABLE, SECTOR_GENERAL,  PIN_ANY,                 PIN_VERB|PIN_NOUN);
    add_concept("for",        TYPE_LITERAL,    CLASS_PREP,     TRUST_IMMUTABLE, SECTOR_GENERAL,  PIN_ANY,                 PIN_NOUN|PIN_VERB|PIN_ARTICLE);
    add_concept("with",       TYPE_LITERAL,    CLASS_PREP,     TRUST_IMMUTABLE, SECTOR_GENERAL,  PIN_ANY,                 PIN_NOUN|PIN_ARTICLE);
    add_concept("on",         TYPE_LITERAL,    CLASS_PREP,     TRUST_IMMUTABLE, SECTOR_GENERAL,  PIN_ANY,                 PIN_NOUN|PIN_ARTICLE);
    add_concept("by",         TYPE_LITERAL,    CLASS_PREP,     TRUST_IMMUTABLE, SECTOR_GENERAL,  PIN_ANY,                 PIN_NOUN|PIN_VERB|PIN_ARTICLE);
    add_concept("from",       TYPE_LITERAL,    CLASS_PREP,     TRUST_IMMUTABLE, SECTOR_GENERAL,  PIN_ANY,                 PIN_NOUN|PIN_ARTICLE);
    add_concept("into",       TYPE_LITERAL,    CLASS_PREP,     TRUST_IMMUTABLE, SECTOR_GENERAL,  PIN_ANY,                 PIN_NOUN|PIN_ARTICLE);

    /* PRONOUNS */
    add_concept("i",          TYPE_IDENTIFIER, CLASS_PRONOUN,  TRUST_IMMUTABLE, SECTOR_GENERAL,  PIN_ANY, PIN_VERB);
    add_concept("you",        TYPE_IDENTIFIER, CLASS_PRONOUN,  TRUST_IMMUTABLE, SECTOR_GENERAL,  PIN_ANY, PIN_VERB);
    add_concept("he",         TYPE_IDENTIFIER, CLASS_PRONOUN,  TRUST_IMMUTABLE, SECTOR_GENERAL,  PIN_ANY, PIN_VERB);
    add_concept("she",        TYPE_IDENTIFIER, CLASS_PRONOUN,  TRUST_IMMUTABLE, SECTOR_GENERAL,  PIN_ANY, PIN_VERB);
    add_concept("it",         TYPE_IDENTIFIER, CLASS_PRONOUN,  TRUST_IMMUTABLE, SECTOR_GENERAL,  PIN_ANY, PIN_VERB);
    add_concept("they",       TYPE_IDENTIFIER, CLASS_PRONOUN,  TRUST_IMMUTABLE, SECTOR_GENERAL,  PIN_ANY, PIN_VERB);
    add_concept("we",         TYPE_IDENTIFIER, CLASS_PRONOUN,  TRUST_IMMUTABLE, SECTOR_GENERAL,  PIN_ANY, PIN_VERB);
    add_concept("this",       TYPE_IDENTIFIER, CLASS_PRONOUN,  TRUST_IMMUTABLE, SECTOR_GENERAL,  PIN_ANY, PIN_NOUN|PIN_VERB);
    add_concept("that",       TYPE_IDENTIFIER, CLASS_PRONOUN,  TRUST_IMMUTABLE, SECTOR_GENERAL,  PIN_ANY, PIN_NOUN|PIN_VERB);

    /* GENERAL NOUNS */
    add_concept("system",     TYPE_IDENTIFIER, CLASS_NOUN,     TRUST_IMMUTABLE, SECTOR_GENERAL,  PIN_ARTICLE|PIN_ADJ|PIN_ANY, PIN_ANY);
    add_concept("data",       TYPE_IDENTIFIER, CLASS_NOUN,     TRUST_IMMUTABLE, SECTOR_GENERAL,  PIN_ARTICLE|PIN_ADJ|PIN_ANY, PIN_ANY);
    add_concept("code",       TYPE_IDENTIFIER, CLASS_NOUN,     TRUST_IMMUTABLE, SECTOR_GENERAL,  PIN_ARTICLE|PIN_ADJ|PIN_ANY, PIN_ANY);
    add_concept("file",       TYPE_IDENTIFIER, CLASS_NOUN,     TRUST_IMMUTABLE, SECTOR_GENERAL,  PIN_ARTICLE|PIN_ADJ|PIN_ANY, PIN_ANY);
    add_concept("word",       TYPE_IDENTIFIER, CLASS_NOUN,     TRUST_IMMUTABLE, SECTOR_GENERAL,  PIN_ARTICLE|PIN_ADJ|PIN_ANY, PIN_ANY);
    add_concept("bit",        TYPE_IDENTIFIER, CLASS_NOUN,     TRUST_IMMUTABLE, SECTOR_ICT,      PIN_ARTICLE|PIN_ADJ|PIN_ANY, PIN_ANY);
    add_concept("user",       TYPE_IDENTIFIER, CLASS_NOUN,     TRUST_IMMUTABLE, SECTOR_GENERAL,  PIN_ARTICLE|PIN_ADJ|PIN_ANY, PIN_ANY);
    add_concept("computer",   TYPE_IDENTIFIER, CLASS_NOUN,     TRUST_IMMUTABLE, SECTOR_ICT,      PIN_ARTICLE|PIN_ADJ|PIN_ANY, PIN_ANY);
    add_concept("memory",     TYPE_IDENTIFIER, CLASS_NOUN,     TRUST_IMMUTABLE, SECTOR_ICT,      PIN_ARTICLE|PIN_ADJ|PIN_ANY, PIN_ANY);
    add_concept("address",    TYPE_IDENTIFIER, CLASS_NOUN,     TRUST_IMMUTABLE, SECTOR_ICT,      PIN_ARTICLE|PIN_ADJ|PIN_ANY, PIN_ANY);
    add_concept("logic",      TYPE_IDENTIFIER, CLASS_NOUN,     TRUST_IMMUTABLE, SECTOR_GENERAL,  PIN_ARTICLE|PIN_ADJ|PIN_ANY, PIN_ANY);
    add_concept("input",      TYPE_IDENTIFIER, CLASS_NOUN,     TRUST_IMMUTABLE, SECTOR_ICT,      PIN_ARTICLE|PIN_ADJ|PIN_ANY, PIN_ANY);
    add_concept("output",     TYPE_IDENTIFIER, CLASS_NOUN,     TRUST_IMMUTABLE, SECTOR_ICT,      PIN_ARTICLE|PIN_ADJ|PIN_ANY, PIN_ANY);
    add_concept("error",      TYPE_IDENTIFIER, CLASS_NOUN,     TRUST_IMMUTABLE, SECTOR_ICT,      PIN_ARTICLE|PIN_ADJ|PIN_ANY, PIN_ANY);
    add_concept("result",     TYPE_IDENTIFIER, CLASS_NOUN,     TRUST_IMMUTABLE, SECTOR_GENERAL,  PIN_ARTICLE|PIN_ADJ|PIN_ANY, PIN_ANY);
    add_concept("block",      TYPE_IDENTIFIER, CLASS_NOUN,     TRUST_IMMUTABLE, SECTOR_ICT,      PIN_ARTICLE|PIN_ADJ|PIN_ANY, PIN_ANY);
    add_concept("token",      TYPE_IDENTIFIER, CLASS_NOUN,     TRUST_IMMUTABLE, SECTOR_ICT,      PIN_ARTICLE|PIN_ADJ|PIN_ANY, PIN_ANY);
    add_concept("node",       TYPE_IDENTIFIER, CLASS_NOUN,     TRUST_IMMUTABLE, SECTOR_ICT,      PIN_ARTICLE|PIN_ADJ|PIN_ANY, PIN_ANY);
    add_concept("key",        TYPE_IDENTIFIER, CLASS_NOUN,     TRUST_IMMUTABLE, SECTOR_SECURITY, PIN_ARTICLE|PIN_ADJ|PIN_ANY, PIN_ANY);
    add_concept("hash",       TYPE_IDENTIFIER, CLASS_NOUN,     TRUST_IMMUTABLE, SECTOR_ICT,      PIN_ARTICLE|PIN_ADJ|PIN_ANY, PIN_ANY);

    /* SECURITY / ICT / FINANCE */
    add_concept("bug",        TYPE_IDENTIFIER, CLASS_NOUN,     TRUST_IMMUTABLE, SECTOR_SECURITY, PIN_ARTICLE|PIN_ADJ|PIN_ANY, PIN_ANY);
    add_concept("virus",      TYPE_IDENTIFIER, CLASS_NOUN,     TRUST_IMMUTABLE, SECTOR_SECURITY, PIN_ARTICLE|PIN_ADJ|PIN_ANY, PIN_ANY);
    add_concept("virus",      TYPE_IDENTIFIER, CLASS_NOUN,     TRUST_IMMUTABLE, SECTOR_BIOLOGY,  PIN_ARTICLE|PIN_ADJ|PIN_ANY, PIN_ANY);
    add_concept("attack",     TYPE_IDENTIFIER, CLASS_NOUN,     TRUST_IMMUTABLE, SECTOR_SECURITY, PIN_ARTICLE|PIN_ADJ|PIN_ANY, PIN_ANY);
    add_concept("malware",    TYPE_IDENTIFIER, CLASS_NOUN,     TRUST_IMMUTABLE, SECTOR_SECURITY, PIN_ARTICLE|PIN_ADJ|PIN_ANY, PIN_ANY);
    add_concept("network",    TYPE_IDENTIFIER, CLASS_NOUN,     TRUST_IMMUTABLE, SECTOR_ICT,      PIN_ARTICLE|PIN_ADJ|PIN_ANY, PIN_ANY);
    add_concept("contract",   TYPE_IDENTIFIER, CLASS_NOUN,     TRUST_IMMUTABLE, SECTOR_FINANCE,  PIN_ARTICLE|PIN_ADJ|PIN_ANY, PIN_ANY);
    add_concept("blockchain", TYPE_IDENTIFIER, CLASS_NOUN,     TRUST_VERIFIED,  SECTOR_FINANCE,  PIN_ARTICLE|PIN_ADJ|PIN_ANY, PIN_ANY);
    add_concept("reentrancy", TYPE_IDENTIFIER, CLASS_NOUN,     TRUST_VERIFIED,  SECTOR_SECURITY, PIN_ARTICLE|PIN_ADJ|PIN_ANY, PIN_ANY);
    add_concept("wallet",     TYPE_IDENTIFIER, CLASS_NOUN,     TRUST_VERIFIED,  SECTOR_FINANCE,  PIN_ARTICLE|PIN_ADJ|PIN_ANY, PIN_ANY);
    add_concept("transaction",TYPE_IDENTIFIER, CLASS_NOUN,     TRUST_VERIFIED,  SECTOR_FINANCE,  PIN_ARTICLE|PIN_ADJ|PIN_ANY, PIN_ANY);

    /* VERBS */
    add_concept("run",        TYPE_IDENTIFIER, CLASS_VERB,     TRUST_IMMUTABLE, SECTOR_GENERAL,  PIN_NOUN|PIN_PRONOUN|PIN_ANY, PIN_NOUN|PIN_PREP|PIN_ADV);
    add_concept("scan",       TYPE_IDENTIFIER, CLASS_VERB,     TRUST_IMMUTABLE, SECTOR_ICT,      PIN_NOUN|PIN_PRONOUN|PIN_ANY, PIN_NOUN|PIN_PREP|PIN_ADV);
    add_concept("audit",      TYPE_IDENTIFIER, CLASS_VERB,     TRUST_IMMUTABLE, SECTOR_FINANCE,  PIN_NOUN|PIN_PRONOUN|PIN_ANY, PIN_NOUN|PIN_PREP|PIN_ADV);
    add_concept("detect",     TYPE_IDENTIFIER, CLASS_VERB,     TRUST_IMMUTABLE, SECTOR_SECURITY, PIN_NOUN|PIN_PRONOUN|PIN_ANY, PIN_NOUN|PIN_PREP|PIN_ADV);
    add_concept("process",    TYPE_IDENTIFIER, CLASS_VERB,     TRUST_IMMUTABLE, SECTOR_GENERAL,  PIN_NOUN|PIN_PRONOUN|PIN_ANY, PIN_NOUN|PIN_PREP|PIN_ADV);
    add_concept("store",      TYPE_IDENTIFIER, CLASS_VERB,     TRUST_IMMUTABLE, SECTOR_ICT,      PIN_NOUN|PIN_PRONOUN|PIN_ANY, PIN_NOUN|PIN_PREP|PIN_ADV);
    add_concept("read",       TYPE_IDENTIFIER, CLASS_VERB,     TRUST_IMMUTABLE, SECTOR_GENERAL,  PIN_NOUN|PIN_PRONOUN|PIN_ANY, PIN_NOUN|PIN_PREP|PIN_ADV);
    add_concept("write",      TYPE_IDENTIFIER, CLASS_VERB,     TRUST_IMMUTABLE, SECTOR_GENERAL,  PIN_NOUN|PIN_PRONOUN|PIN_ANY, PIN_NOUN|PIN_PREP|PIN_ADV);
    add_concept("map",        TYPE_IDENTIFIER, CLASS_VERB,     TRUST_IMMUTABLE, SECTOR_GENERAL,  PIN_NOUN|PIN_PRONOUN|PIN_ANY, PIN_NOUN|PIN_PREP|PIN_ADV);
    add_concept("load",       TYPE_IDENTIFIER, CLASS_VERB,     TRUST_IMMUTABLE, SECTOR_ICT,      PIN_NOUN|PIN_PRONOUN|PIN_ANY, PIN_NOUN|PIN_PREP|PIN_ADV);
    add_concept("attack",     TYPE_IDENTIFIER, CLASS_VERB,     TRUST_IMMUTABLE, SECTOR_SECURITY, PIN_NOUN|PIN_PRONOUN|PIN_ANY, PIN_NOUN|PIN_PREP|PIN_ADV);
    add_concept("infect",     TYPE_IDENTIFIER, CLASS_VERB,     TRUST_IMMUTABLE, SECTOR_BIOLOGY,  PIN_NOUN|PIN_PRONOUN|PIN_ANY, PIN_NOUN|PIN_PREP|PIN_ADV);
    add_concept("verify",     TYPE_IDENTIFIER, CLASS_VERB,     TRUST_IMMUTABLE, SECTOR_GENERAL,  PIN_NOUN|PIN_PRONOUN|PIN_ANY, PIN_NOUN|PIN_PREP|PIN_ADV);
    add_concept("send",       TYPE_IDENTIFIER, CLASS_VERB,     TRUST_IMMUTABLE, SECTOR_GENERAL,  PIN_NOUN|PIN_PRONOUN|PIN_ANY, PIN_NOUN|PIN_PREP|PIN_ADV);
    add_concept("receive",    TYPE_IDENTIFIER, CLASS_VERB,     TRUST_IMMUTABLE, SECTOR_GENERAL,  PIN_NOUN|PIN_PRONOUN|PIN_ANY, PIN_NOUN|PIN_PREP|PIN_ADV);
    add_concept("check",      TYPE_IDENTIFIER, CLASS_VERB,     TRUST_IMMUTABLE, SECTOR_GENERAL,  PIN_NOUN|PIN_PRONOUN|PIN_ANY, PIN_NOUN|PIN_PREP|PIN_ADV);
    add_concept("find",       TYPE_IDENTIFIER, CLASS_VERB,     TRUST_IMMUTABLE, SECTOR_GENERAL,  PIN_NOUN|PIN_PRONOUN|PIN_ANY, PIN_NOUN|PIN_PREP|PIN_ADV);
    add_concept("set",        TYPE_IDENTIFIER, CLASS_VERB,     TRUST_IMMUTABLE, SECTOR_GENERAL,  PIN_NOUN|PIN_PRONOUN|PIN_ANY, PIN_NOUN|PIN_PREP|PIN_ADV);
    add_concept("get",        TYPE_IDENTIFIER, CLASS_VERB,     TRUST_IMMUTABLE, SECTOR_GENERAL,  PIN_NOUN|PIN_PRONOUN|PIN_ANY, PIN_NOUN|PIN_PREP|PIN_ADV);
    add_concept("build",      TYPE_IDENTIFIER, CLASS_VERB,     TRUST_IMMUTABLE, SECTOR_GENERAL,  PIN_NOUN|PIN_PRONOUN|PIN_ANY, PIN_NOUN|PIN_PREP|PIN_ADV);

    /* ADJECTIVES */
    add_concept("malicious",  TYPE_IDENTIFIER, CLASS_ADJ,      TRUST_IMMUTABLE, SECTOR_SECURITY, PIN_ARTICLE|PIN_ADV|PIN_ANY, PIN_NOUN);
    add_concept("infected",   TYPE_IDENTIFIER, CLASS_ADJ,      TRUST_IMMUTABLE, SECTOR_BIOLOGY,  PIN_ARTICLE|PIN_ADV|PIN_ANY, PIN_NOUN);
    add_concept("secure",     TYPE_IDENTIFIER, CLASS_ADJ,      TRUST_IMMUTABLE, SECTOR_SECURITY, PIN_ARTICLE|PIN_ADV|PIN_ANY, PIN_NOUN);
    add_concept("fast",       TYPE_IDENTIFIER, CLASS_ADJ,      TRUST_IMMUTABLE, SECTOR_GENERAL,  PIN_ARTICLE|PIN_ADV|PIN_ANY, PIN_NOUN);
    add_concept("valid",      TYPE_IDENTIFIER, CLASS_ADJ,      TRUST_IMMUTABLE, SECTOR_GENERAL,  PIN_ARTICLE|PIN_ADV|PIN_ANY, PIN_NOUN);
    add_concept("invalid",    TYPE_IDENTIFIER, CLASS_ADJ,      TRUST_IMMUTABLE, SECTOR_GENERAL,  PIN_ARTICLE|PIN_ADV|PIN_ANY, PIN_NOUN);
    add_concept("active",     TYPE_IDENTIFIER, CLASS_ADJ,      TRUST_IMMUTABLE, SECTOR_GENERAL,  PIN_ARTICLE|PIN_ADV|PIN_ANY, PIN_NOUN);
    add_concept("new",        TYPE_IDENTIFIER, CLASS_ADJ,      TRUST_IMMUTABLE, SECTOR_GENERAL,  PIN_ARTICLE|PIN_ADV|PIN_ANY, PIN_NOUN);
    add_concept("old",        TYPE_IDENTIFIER, CLASS_ADJ,      TRUST_IMMUTABLE, SECTOR_GENERAL,  PIN_ARTICLE|PIN_ADV|PIN_ANY, PIN_NOUN);
    add_concept("critical",   TYPE_IDENTIFIER, CLASS_ADJ,      TRUST_IMMUTABLE, SECTOR_SECURITY, PIN_ARTICLE|PIN_ADV|PIN_ANY, PIN_NOUN);
    add_concept("encrypted",  TYPE_IDENTIFIER, CLASS_ADJ,      TRUST_IMMUTABLE, SECTOR_SECURITY, PIN_ARTICLE|PIN_ADV|PIN_ANY, PIN_NOUN);
    add_concept("digital",    TYPE_IDENTIFIER, CLASS_ADJ,      TRUST_IMMUTABLE, SECTOR_ICT,      PIN_ARTICLE|PIN_ADV|PIN_ANY, PIN_NOUN);
    add_concept("local",      TYPE_IDENTIFIER, CLASS_ADJ,      TRUST_IMMUTABLE, SECTOR_GENERAL,  PIN_ARTICLE|PIN_ADV|PIN_ANY, PIN_NOUN);
    add_concept("remote",     TYPE_IDENTIFIER, CLASS_ADJ,      TRUST_IMMUTABLE, SECTOR_ICT,      PIN_ARTICLE|PIN_ADV|PIN_ANY, PIN_NOUN);

    /* ADVERBS */
    add_concept("quickly",    TYPE_IDENTIFIER, CLASS_ADV,      TRUST_IMMUTABLE, SECTOR_GENERAL,  PIN_VERB|PIN_ADJ|PIN_ANY, PIN_VERB|PIN_ADJ);
    add_concept("always",     TYPE_IDENTIFIER, CLASS_ADV,      TRUST_IMMUTABLE, SECTOR_GENERAL,  PIN_VERB|PIN_ADJ|PIN_ANY, PIN_VERB|PIN_ADJ);
    add_concept("never",      TYPE_IDENTIFIER, CLASS_ADV,      TRUST_IMMUTABLE, SECTOR_GENERAL,  PIN_VERB|PIN_ADJ|PIN_ANY, PIN_VERB|PIN_ADJ);
    add_concept("now",        TYPE_IDENTIFIER, CLASS_ADV,      TRUST_IMMUTABLE, SECTOR_GENERAL,  PIN_ANY,                  PIN_VERB|PIN_ADJ);
    add_concept("only",       TYPE_IDENTIFIER, CLASS_ADV,      TRUST_IMMUTABLE, SECTOR_GENERAL,  PIN_ANY,                  PIN_VERB|PIN_ADJ|PIN_NOUN);
    add_concept("also",       TYPE_IDENTIFIER, CLASS_ADV,      TRUST_IMMUTABLE, SECTOR_GENERAL,  PIN_ANY,                  PIN_VERB|PIN_ADJ);
}

/* main() lives in main.c */
