/*
 * ╔══════════════════════════════════════════════════════════════╗
 * ║  BLAF — blaf_vectors.c                                      ║
 * ╚══════════════════════════════════════════════════════════════╝
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <ctype.h>

#include "blaf_vectors.h"

/* ═══════════════════════════════════════════════════════════════
   HASH TABLE STORAGE
   Open addressing, linear probe.
   ═══════════════════════════════════════════════════════════════ */

static VecEntry *g_table   = NULL;
static int       g_loaded  = 0;
static int       g_dims    = VEC_DIMS;
static int       g_is_init = 0;

/* djb2 hash */
static uint32_t hash_word(const char *s) {
    uint32_t h = 5381;
    while (*s) h = ((h << 5) + h) + (unsigned char)*s++;
    return h & (VEC_HASH_SIZE - 1);
}

static void lower_str(char *dst, const char *src, int n) {
    int i;
    for (i = 0; i < n-1 && src[i]; i++) dst[i] = tolower((unsigned char)src[i]);
    dst[i] = '\0';
}

/* Insert into hash table — returns slot index */
static int ht_insert(const char *word, const float *vec) {
    uint32_t slot = hash_word(word);
    for (int probe = 0; probe < VEC_HASH_SIZE; probe++) {
        uint32_t idx = (slot + probe) & (VEC_HASH_SIZE - 1);
        if (!g_table[idx].used) {
            strncpy(g_table[idx].word, word, VEC_MAX_WORD - 1);
            memcpy(g_table[idx].v, vec, g_dims * sizeof(float));
            g_table[idx].used = 1;
            return idx;
        }
        if (strcmp(g_table[idx].word, word) == 0) return idx; /* already exists */
    }
    return -1; /* table full */
}

/* Lookup — returns pointer to entry or NULL */
static VecEntry* ht_get(const char *word) {
    if (!g_table) return NULL;
    char l[VEC_MAX_WORD];
    lower_str(l, word, VEC_MAX_WORD);
    uint32_t slot = hash_word(l);
    for (int probe = 0; probe < VEC_HASH_SIZE; probe++) {
        uint32_t idx = (slot + probe) & (VEC_HASH_SIZE - 1);
        if (!g_table[idx].used) return NULL;
        if (strcmp(g_table[idx].word, l) == 0) return &g_table[idx];
    }
    return NULL;
}

/* ═══════════════════════════════════════════════════════════════
   INIT — Load GloVe text file
   Format: "word f1 f2 f3 ... fN\n"
   ═══════════════════════════════════════════════════════════════ */

int vec_init(const char *filepath, int max_words) {
    if (g_is_init) {
        printf("[VECTORS] Already loaded (%d words).\n", g_loaded);
        return g_loaded;
    }

    /* Allocate hash table */
    g_table = (VecEntry*)calloc(VEC_HASH_SIZE, sizeof(VecEntry));
    if (!g_table) {
        fprintf(stderr, "[VECTORS] Out of memory.\n");
        return -1;
    }

    FILE *f = fopen(filepath, "r");
    if (!f) {
        fprintf(stderr, "[VECTORS] Cannot open: %s\n", filepath);
        fprintf(stderr, "[VECTORS] Download from: "
                "https://nlp.stanford.edu/data/glove.6B.zip\n");
        free(g_table);
        g_table = NULL;
        return -1;
    }

    printf("[VECTORS] Loading %s ...\n", filepath);

    char   line[32768];
    float  vec[VEC_DIMS];
    int    count = 0;
    int    limit = (max_words > 0) ? max_words : VEC_MAX_LOADED;

    while (fgets(line, sizeof(line), f) && count < limit) {
        /* Parse word */
        char *p   = line;
        char *end = strchr(p, ' ');
        if (!end) continue;
        *end = '\0';

        char word[VEC_MAX_WORD];
        lower_str(word, p, VEC_MAX_WORD);
        p = end + 1;

        /* Parse floats */
        int dim = 0;
        while (*p && dim < VEC_DIMS) {
            vec[dim++] = strtof(p, &p);
            while (*p == ' ') p++;
        }
        if (dim < VEC_DIMS) continue; /* incomplete line */

        /* Detect actual dims from first line */
        if (count == 0) {
            g_dims = dim;
            printf("[VECTORS] Detected %d dimensions.\n", g_dims);
        }

        if (ht_insert(word, vec) >= 0) count++;
    }

    fclose(f);
    g_loaded  = count;
    g_is_init = 1;

    printf("[VECTORS] Loaded %d words (%.1fMB table).\n",
           g_loaded,
           (float)(VEC_HASH_SIZE * sizeof(VecEntry)) / (1024*1024));
    return g_loaded;
}

int vec_loaded(void) { return g_is_init && g_loaded > 0; }

/* ═══════════════════════════════════════════════════════════════
   LOOKUP
   ═══════════════════════════════════════════════════════════════ */

const float* vec_get(const char *word) {
    VecEntry *e = ht_get(word);
    return e ? e->v : NULL;
}

/* ═══════════════════════════════════════════════════════════════
   SIMILARITY
   ═══════════════════════════════════════════════════════════════ */

float vec_similarity_raw(const float *a, const float *b, int dims) {
    if (!a || !b) return 0.0f;
    double dot = 0.0, na = 0.0, nb = 0.0;
    for (int i = 0; i < dims; i++) {
        dot += a[i] * b[i];
        na  += a[i] * a[i];
        nb  += b[i] * b[i];
    }
    if (na == 0.0 || nb == 0.0) return 0.0f;
    return (float)(dot / (sqrt(na) * sqrt(nb)));
}

float vec_similarity(const char *word_a, const char *word_b) {
    const float *a = vec_get(word_a);
    const float *b = vec_get(word_b);
    return vec_similarity_raw(a, b, g_dims);
}

/* ═══════════════════════════════════════════════════════════════
   NEAREST WORD SEARCH
   Linear scan — O(N). For production use an HNSW index.
   ═══════════════════════════════════════════════════════════════ */

int vec_nearest_raw(const float *vec, VecMatch *results, int n,
                    const char *exclude) {
    if (!g_table || !vec || n <= 0) return 0;

    /* Init results with -1 similarity */
    for (int i = 0; i < n; i++) {
        results[i].similarity = -1.0f;
        results[i].word[0]    = '\0';
    }

    for (int i = 0; i < VEC_HASH_SIZE; i++) {
        if (!g_table[i].used) continue;
        if (exclude && strcmp(g_table[i].word, exclude) == 0) continue;

        float sim = vec_similarity_raw(vec, g_table[i].v, g_dims);

        /* Insert into sorted results (insertion sort on small n) */
        if (sim > results[n-1].similarity) {
            results[n-1].similarity = sim;
            strncpy(results[n-1].word, g_table[i].word, VEC_MAX_WORD-1);
            /* Bubble up */
            for (int j = n-1; j > 0 && results[j].similarity > results[j-1].similarity; j--) {
                VecMatch tmp  = results[j];
                results[j]   = results[j-1];
                results[j-1] = tmp;
            }
        }
    }

    int count = 0;
    for (int i = 0; i < n; i++)
        if (results[i].similarity > -1.0f) count++;
    return count;
}

int vec_nearest(const char *word, VecMatch *results, int n) {
    const float *v = vec_get(word);
    if (!v) return 0;
    return vec_nearest_raw(v, results, n, word);
}

/* ═══════════════════════════════════════════════════════════════
   VECTOR ARITHMETIC
   ═══════════════════════════════════════════════════════════════ */

void vec_add(const float *a, const float *b, float *out) {
    if (!a || !b || !out) return;
    for (int i = 0; i < g_dims; i++) out[i] = a[i] + b[i];
}

void vec_sub(const float *a, const float *b, float *out) {
    if (!a || !b || !out) return;
    for (int i = 0; i < g_dims; i++) out[i] = a[i] - b[i];
}

void vec_scale(const float *a, float s, float *out) {
    if (!a || !out) return;
    for (int i = 0; i < g_dims; i++) out[i] = a[i] * s;
}

void vec_mean(const float **vecs, int count, float *out) {
    if (!vecs || count == 0 || !out) return;
    memset(out, 0, g_dims * sizeof(float));
    int valid = 0;
    for (int i = 0; i < count; i++) {
        if (!vecs[i]) continue;
        for (int j = 0; j < g_dims; j++) out[j] += vecs[i][j];
        valid++;
    }
    if (valid > 1)
        for (int j = 0; j < g_dims; j++) out[j] /= (float)valid;
}

/* ═══════════════════════════════════════════════════════════════
   STATUS / FREE
   ═══════════════════════════════════════════════════════════════ */

void vec_status(void) {
    printf("\n[VECTORS] Status:\n");
    printf("  Loaded : %s\n",  g_is_init ? "YES" : "NO");
    printf("  Words  : %d\n",  g_loaded);
    printf("  Dims   : %d\n",  g_dims);
    printf("  Table  : %.1fMB\n",
           g_table ? (float)(VEC_HASH_SIZE * sizeof(VecEntry))/(1024*1024) : 0);
    printf("\n");
}

void vec_free(void) {
    if (g_table) { free(g_table); g_table = NULL; }
    g_loaded  = 0;
    g_is_init = 0;
}
