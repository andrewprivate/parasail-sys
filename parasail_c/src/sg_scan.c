/**
 * @file
 *
 * @author jeffrey.daily@gmail.com
 *
 * Copyright (c) 2015 Battelle Memorial Institute.
 */
#include "config.h"

#include <stdint.h>
#include <stdlib.h>

#include "parasail.h"
#include "parasail/memory.h"

#define SG_SUFFIX _scan
#include "sg_helper.h"

#define NEG_INF_32 (INT32_MIN/2)
#define MAX(a,b) ((a)>(b)?(a):(b))

#ifdef PARASAIL_TABLE
#define FNAME parasail_sg_flags_table_scan
#else
#ifdef PARASAIL_ROWCOL
#define FNAME parasail_sg_flags_rowcol_scan
#else
#define FNAME parasail_sg_flags_scan
#endif
#endif

parasail_result_t* FNAME(
        const char * const restrict _s1, const int s1Len,
        const char * const restrict _s2, const int s2Len,
        const int open, const int gap, const parasail_matrix_t *matrix,
        int s1_beg, int s1_end, int s2_beg, int s2_end)
{
#ifdef PARASAIL_TABLE
    parasail_result_t *result = parasail_result_new_table1(s1Len, s2Len);
#else
#ifdef PARASAIL_ROWCOL
    parasail_result_t *result = parasail_result_new_rowcol1(s1Len, s2Len);
#else
    parasail_result_t *result = parasail_result_new();
#endif
#endif
    int * const restrict s1 = parasail_memalign_int(16, s1Len);
    int * const restrict s2 = parasail_memalign_int(16, s2Len);
    int * const restrict HB = parasail_memalign_int(16, s1Len+1);
    int * const restrict H  = HB+1;
    int * const restrict E  = parasail_memalign_int(16, s1Len);
    int * const restrict HtB= parasail_memalign_int(16, s1Len+1);
    int * const restrict Ht = HtB+1;
    int i = 0;
    int j = 0;
    int score = NEG_INF_32;
    int end_query = s1Len;
    int end_ref = s2Len;

    for (i=0; i<s1Len; ++i) {
        s1[i] = matrix->mapper[(unsigned char)_s1[i]];
    }
    for (j=0; j<s2Len; ++j) {
        s2[j] = matrix->mapper[(unsigned char)_s2[j]];
    }

    /* initialize H */
    H[-1] = 0;
    Ht[-1] = 0;
    if (s1_beg) {
        for (i=0; i<s1Len; ++i) {
            H[i] = 0;
        }
    }
    else {
        for (i=0; i<s1Len; ++i) {
            H[i] = -open -i*gap;
        }
    }

    /* initialize E */
    for (i=0; i<s1Len; ++i) {
        E[i] = NEG_INF_32;
    }

    /* iterate over database */
    for (j=0; j<s2Len-1; ++j) {
        int Ft = NEG_INF_32;
        const int * const restrict matcol = &matrix->matrix[matrix->size*s2[j]];
        /* calculate E */
        for (i=0; i<s1Len; ++i) {
            E[i] = MAX(E[i]-gap, H[i]-open);
        }
        /* calculate Ht */
        for (i=0; i<s1Len; ++i) {
            Ht[i] = MAX(H[i-1]+matcol[s1[i]], E[i]);
        }
        Ht[-1] = s2_beg ? 0 : (-open -j*gap);
        /* calculate H */
        for (i=0; i<s1Len; ++i) {
            int Ft_opn;
            int Ht_pre = Ht[i-1];
            int Ft_ext = Ft-gap;
            if (Ht_pre >= Ft_ext) {
                Ft = Ht_pre;
            }
            else {
                Ft = Ft_ext;
            }
            Ft_opn = Ft-open;
            H[i] = MAX(Ht[i], Ft_opn);
#ifdef PARASAIL_TABLE
            result->tables->score_table[i*s2Len + j] = H[i];
#endif
        }
        H[-1] = s2_beg ? 0 : (-open - j*gap);
#ifdef PARASAIL_ROWCOL
        result->rowcols->score_row[j] = H[s1Len-1];
#endif
        if (s2_end && H[s1Len-1] > score) {
            score = H[s1Len-1];
            end_query = s1Len-1;
            end_ref = j;
        }
    }
    j = s2Len - 1;
    {
        int Ft = NEG_INF_32;
        const int * const restrict matcol = &matrix->matrix[matrix->size*s2[j]];
        /* calculate E */
        for (i=0; i<s1Len; ++i) {
            E[i] = MAX(E[i]-gap, H[i]-open);
        }
        /* calculate Ht */
        for (i=0; i<s1Len; ++i) {
            Ht[i] = MAX(H[i-1]+matcol[s1[i]], E[i]);
        }
        Ht[-1] = s2_beg ? 0 : (-open -j*gap);
        /* calculate H */
        for (i=0; i<s1Len; ++i) {
            int Ft_opn;
            int Ht_pre = Ht[i-1];
            int Ft_ext = Ft-gap;
            if (Ht_pre >= Ft_ext) {
                Ft = Ht_pre;
            }
            else {
                Ft = Ft_ext;
            }
            Ft_opn = Ft-open;
            H[i] = MAX(Ht[i], Ft_opn);
#ifdef PARASAIL_TABLE
            result->tables->score_table[i*s2Len + j] = H[i];
#endif
#ifdef PARASAIL_ROWCOL
            result->rowcols->score_col[i] = H[i];
#endif
            if (s1_end && H[i] > score) {
                score = H[i];
                end_query = i;
                end_ref = j;
            }
        }
#ifdef PARASAIL_ROWCOL
        result->rowcols->score_row[j] = H[s1Len-1];
#endif
    }
    if (s2_end && H[s1Len-1] > score) {
        score = H[s1Len-1];
        end_query = s1Len-1;
        end_ref = s2Len-1;
    }
    if (!s1_end && !s2_end) {
        score = H[s1Len-1];
        end_query = s1Len-1;
        end_ref = s2Len-1;
    }

    result->score = score;
    result->end_query = end_query;
    result->end_ref = end_ref;
    result->flag |= PARASAIL_FLAG_SG | PARASAIL_FLAG_NOVEC_SCAN
        | PARASAIL_FLAG_BITS_INT | PARASAIL_FLAG_LANES_1;
    result->flag |= s1_beg ? PARASAIL_FLAG_SG_S1_BEG : 0;
    result->flag |= s1_end ? PARASAIL_FLAG_SG_S1_END : 0;
    result->flag |= s2_beg ? PARASAIL_FLAG_SG_S2_BEG : 0;
    result->flag |= s2_end ? PARASAIL_FLAG_SG_S2_END : 0;
#ifdef PARASAIL_TABLE
    result->flag |= PARASAIL_FLAG_TABLE;
#endif
#ifdef PARASAIL_ROWCOL
    result->flag |= PARASAIL_FLAG_ROWCOL;
#endif

    parasail_free(HtB);
    parasail_free(E);
    parasail_free(HB);
    parasail_free(s2);
    parasail_free(s1);

    return result;
}

SG_IMPL_ALL

