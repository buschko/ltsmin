#include <hre/config.h>

#include <limits.h>

#include <dm/bitvector.h>

static inline       size_t
utrunc (size_t x, size_t m)
{
    return (x + (m - 1)) / m;
}

static const size_t WORD_BITS = sizeof (size_t) * 8;
static const size_t WORD_BITS_MASK = sizeof (size_t) * 8 - 1;
static const size_t WORD_SHIFT = sizeof (size_t) == 4 ? 5 : 6;

static inline size_t
bv_seg (size_t i) { return i >> WORD_SHIFT; }

static inline size_t
bv_ofs (size_t i) { return i & WORD_BITS_MASK; }

int
bitvector_create (bitvector_t *bv, size_t n_bits)
{
    bv->n_words = utrunc (n_bits, WORD_BITS);
    bv->data = calloc (bv->n_words, sizeof (size_t));
    if (bv->data == NULL) {
        bv->n_bits = 0;
        return -1;
    } else {
        bv->n_bits = n_bits;
        return 0;
    }
}

void
bitvector_clear (bitvector_t *bv)
{
    memset (bv->data, 0, sizeof (size_t[bv->n_words]));
}

void
bitvector_free (bitvector_t *bv)
{
    // free memory
    if (bv->data != NULL)
        free (bv->data);
    bv->n_bits = 0;
}

int
bitvector_copy (bitvector_t *bv_tgt, const bitvector_t *bv_src)
{
    // check validity src
    if (bv_src->data == NULL)
        return -1;

    // alloc memory for target
    if (bitvector_create (bv_tgt, bv_src->n_bits) != 0) {
        return -1;
    } else {
        // copy bitvector
        size_t              size =
            utrunc (bv_src->n_bits, WORD_BITS) * sizeof (size_t);
        memcpy (bv_tgt->data, bv_src->data, size);
        return 0;
    }
}

size_t
bitvector_size (const bitvector_t *bv)
{
    return bv->n_bits;
}

int
bitvector_isset_or_set (bitvector_t *bv, size_t idx)
{
    // isset_or_set
    size_t              mask = 1UL << bv_ofs (idx);
    size_t              word = bv_seg (idx);
    int                 res = (bv->data[word] & mask) != 0;
    bv->data[word] |= mask;
    return res;
}

void
bitvector_set2 (bitvector_t *bv, size_t idx, size_t v)
{
    // isset_or_set2
    size_t              mask = 3UL << bv_ofs (idx);
    size_t              value = v << bv_ofs (idx);
    size_t              word = bv_seg (idx);
    bv->data[word] &= ~mask;
    bv->data[word] |= value;
}

int
bitvector_isset_or_set2 (bitvector_t *bv, size_t idx, size_t v)
{
    // isset_or_set2
    size_t              mask = 3UL << bv_ofs (idx);
    size_t              value = v << bv_ofs (idx);
    size_t              word = bv_seg (idx);
    int                 res = (bv->data[word] & mask) == value;
    bv->data[word] &= ~mask;
    bv->data[word] |= value;
    return res;
}

int
bitvector_get2 (const bitvector_t *bv, size_t idx)
{
    size_t              mask = 3UL << bv_ofs (idx);
    return (bv->data[bv_seg (idx)] & mask) >> bv_ofs (idx);
}

void
bitvector_set_atomic (bitvector_t *bv, size_t idx)
{
    // set bit
    size_t              mask = 1UL << bv_ofs (idx);
    __sync_fetch_and_or (bv->data + bv_seg(idx), mask);
}

void
bitvector_set (bitvector_t *bv, size_t idx)
{
    // set bit
    size_t              mask = 1UL << bv_ofs (idx);
    bv->data[bv_seg (idx)] |= mask;
}

void
bitvector_unset (bitvector_t *bv, size_t idx)
{
    // set bit
    size_t              mask = ~(1UL << bv_ofs (idx));
    bv->data[bv_seg (idx)] &= mask;
}

int
bitvector_is_set (const bitvector_t *bv, size_t idx)
{
    size_t              mask = 1UL << bv_ofs (idx);
    return (bv->data[bv_seg (idx)] & mask) != 0;
}

void
bitvector_union(bitvector_t *bv, const bitvector_t *bv2)
{
    // check size
    if (bv->n_bits != bv2->n_bits) return;

    // calculate number of words in the bitvector, union wordwise
    for(size_t i=0; i < bv->n_words; ++i) {
        bv->data[i] |= bv2->data[i];
    }
}

void
bitvector_intersect(bitvector_t *bv, const bitvector_t *bv2)
{
    // check size
    if (bv->n_bits != bv2->n_bits) return;

    // calculate number of words in the bitvector, union wordwise
    for(size_t i=0; i < bv->n_words; ++i) {
        bv->data[i] &= bv2->data[i];
    }
}

int
bitvector_is_empty(const bitvector_t *bv)
{
    size_t result = 0;
    // bitvector of size 0
    if (bv->n_bits == 0) return 1;

    // calculate number of words in the bitvector, union wordwise
    for(size_t i=0; i < bv->n_words; ++i) {
        result |= bv->data[i];
    }
    return (result == 0);
}

int
bitvector_is_disjoint(const bitvector_t *bv1, const bitvector_t *bv2)
{
    size_t              result = 0;
    // check size
    if (bv1->n_bits != bv2->n_bits) return 0;

    // calculate number of words in the bitvector, union wordwise
    for(size_t i=0; i < bv1->n_words; ++i) {
        result |= (bv1->data[i] & bv2->data[i]);
    }
    return (result == 0);
}

void
bitvector_invert(bitvector_t *bv)
{
    // check size
    if (bv->n_bits == 0) return;

    // calculate number of words in the bitvector, union wordwise
    for(size_t i=0; i < bv->n_words; ++i) {
        bv->data[i] = ~bv->data[i];
    }

    // don't invert the unused bits!
    size_t              used_bits = WORD_BITS - (bv->n_words * WORD_BITS - bv->n_bits);
    size_t              mask = (1UL << (used_bits))-1;
    if (used_bits < WORD_BITS)
        bv->data[bv->n_words-1] &= mask;
}
