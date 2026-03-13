/*
 * ╔══════════════════════════════════════════════════════════════╗
 * ║  BLAF — blaf_vectors.h                                      ║
 * ║  Word Vector Table (GloVe / FastText)                       ║
 * ║                                                              ║
 * ║  Loads pre-trained word vectors and provides:               ║
 * ║    - O(1) word → vector lookup via hash table               ║
 * ║    - Cosine similarity between any two words                ║
 * ║    - Nearest-concept search (find closest known word)       ║
 * ║    - Vector arithmetic (king - man + woman = queen)         ║
 * ║                                                              ║
 * ║  Download vectors:                                           ║
 * ║    GloVe 300d: https://nlp.stanford.edu/data/glove.6B.zip  ║
 * ║    Use: glove.6B.300d.txt (~1GB, 400k words)                ║
 * ║    Or:  glove.6B.50d.txt  (~170MB, faster, less accurate)   ║
 * ╚══════════════════════════════════════════════════════════════╝
 */

#ifndef BLAF_VECTORS_H
#define BLAF_VECTORS_H

#include <stdint.h>

/* ═══════════════════════════════════════════════════════════════
   CONFIGURATION
   ═══════════════════════════════════════════════════════════════ */

#define VEC_DIMS         300     /* 50, 100, 200, or 300             */
#define VEC_HASH_SIZE    131072  /* must be power of 2               */
#define VEC_MAX_WORD     64
#define VEC_MAX_LOADED   500000  /* max words to load into RAM       */

/* ═══════════════════════════════════════════════════════════════
   VECTOR ENTRY
   ═══════════════════════════════════════════════════════════════ */

typedef struct {
    char  word[VEC_MAX_WORD];
    float v[VEC_DIMS];
    int   used;
} VecEntry;

/* ═══════════════════════════════════════════════════════════════
   RESULT TYPE for nearest-word search
   ═══════════════════════════════════════════════════════════════ */

typedef struct {
    char  word[VEC_MAX_WORD];
    float similarity;  /* 0.0 to 1.0 */
} VecMatch;

/* ═══════════════════════════════════════════════════════════════
   API
   ═══════════════════════════════════════════════════════════════ */

/*
 * vec_init()
 * Loads a GloVe text file into the hash table.
 * max_words = cap on how many to load (0 = load all).
 * Returns number of words loaded, -1 on error.
 *
 * Example:
 *   vec_init("glove.6B.300d.txt", 100000);
 */
int vec_init(const char *filepath, int max_words);

/*
 * vec_loaded()
 * Returns 1 if vectors are loaded, 0 if not.
 * Use to guard vector operations gracefully.
 */
int vec_loaded(void);

/*
 * vec_get()
 * Get the vector for a word. Returns NULL if not found.
 *
 * Example:
 *   float *v = vec_get("london");
 */
const float* vec_get(const char *word);

/*
 * vec_similarity()
 * Cosine similarity between two words. Returns -1.0 to 1.0.
 * 1.0 = identical direction, 0.0 = unrelated, -1.0 = opposite.
 * Returns 0.0 if either word not found.
 *
 * Example:
 *   float s = vec_similarity("london", "paris");  // ~0.85
 *   float s = vec_similarity("london", "banana"); // ~0.1
 */
float vec_similarity(const char *word_a, const char *word_b);

/*
 * vec_similarity_raw()
 * Cosine similarity between two raw float vectors.
 */
float vec_similarity_raw(const float *a, const float *b, int dims);

/*
 * vec_nearest()
 * Find the N nearest words to a given word.
 * Results are sorted by similarity descending.
 * Skips the query word itself.
 *
 * results  = caller-provided array of VecMatch
 * n        = how many results to return
 *
 * Returns number of results filled.
 *
 * Example:
 *   VecMatch results[5];
 *   vec_nearest("london", results, 5);
 *   // results[0].word = "paris", .similarity = 0.88
 */
int vec_nearest(const char *word, VecMatch *results, int n);

/*
 * vec_nearest_raw()
 * Find N nearest words to a raw vector (e.g. computed result).
 */
int vec_nearest_raw(const float *vec, VecMatch *results, int n,
                    const char *exclude);

/*
 * vec_add()  vec_sub()  vec_scale()
 * Vector arithmetic — result written to out[VEC_DIMS].
 * out must be caller-allocated float[VEC_DIMS].
 *
 * Example (king - man + woman):
 *   float result[VEC_DIMS];
 *   vec_add(vec_get("king"), vec_get("woman"), result);
 *   vec_sub(result, vec_get("man"), result);
 *   VecMatch m[1];
 *   vec_nearest_raw(result, m, 1, "king");
 *   // m[0].word = "queen"
 */
void vec_add  (const float *a, const float *b, float *out);
void vec_sub  (const float *a, const float *b, float *out);
void vec_scale(const float *a, float scalar,   float *out);

/*
 * vec_mean()
 * Average N vectors into one. Used to represent a phrase.
 *
 * Example:
 *   const float *vecs[2] = {vec_get("how"), vec_get("far")};
 *   float mean[VEC_DIMS];
 *   vec_mean(vecs, 2, mean);
 */
void vec_mean(const float **vecs, int count, float *out);

/*
 * vec_status()
 * Print load stats.
 */
void vec_status(void);

/*
 * vec_free()
 * Release all loaded vector memory.
 */
void vec_free(void);

#endif /* BLAF_VECTORS_H */
