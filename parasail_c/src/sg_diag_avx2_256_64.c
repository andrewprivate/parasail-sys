/**
 * @file
 *
 * @author jeffrey.daily@gmail.com
 *
 * Copyright (c) 2015 Battelle Memorial Institute.
 */
#include "config.h"

#include <stdlib.h>

#include <immintrin.h>

#include "parasail.h"
#include "parasail/memory.h"
#include "parasail/internal_avx.h"

#define SG_SUFFIX _diag_avx2_256_64
#include "sg_helper.h"

#define NEG_INF (INT64_MIN/(int64_t)(2))

#if HAVE_AVX2_MM256_INSERT_EPI64
#define _mm256_insert_epi64_rpl _mm256_insert_epi64
#else
static inline __m256i _mm256_insert_epi64_rpl(__m256i a, int64_t i, int imm) {
    __m256i_64_t A;
    A.m = a;
    A.v[imm] = i;
    return A.m;
}
#endif

#if HAVE_AVX2_MM256_SET1_EPI64X
#define _mm256_set1_epi64x_rpl _mm256_set1_epi64x
#else
static inline __m256i _mm256_set1_epi64x_rpl(int64_t i) {
    __m256i_64_t A;
    A.v[0] = i;
    A.v[1] = i;
    A.v[2] = i;
    A.v[3] = i;
    return A.m;
}
#endif

static inline __m256i _mm256_max_epi64_rpl(__m256i a, __m256i b) {
    __m256i_64_t A;
    __m256i_64_t B;
    A.m = a;
    B.m = b;
    A.v[0] = (A.v[0]>B.v[0]) ? A.v[0] : B.v[0];
    A.v[1] = (A.v[1]>B.v[1]) ? A.v[1] : B.v[1];
    A.v[2] = (A.v[2]>B.v[2]) ? A.v[2] : B.v[2];
    A.v[3] = (A.v[3]>B.v[3]) ? A.v[3] : B.v[3];
    return A.m;
}

#if HAVE_AVX2_MM256_SET_EPI64X
#define _mm256_set_epi64x_rpl _mm256_set_epi64x
#else
static inline __m256i _mm256_set_epi64x_rpl(int64_t e3, int64_t e2, int64_t e1, int64_t e0) {
    __m256i_64_t A;
    A.v[0] = e0;
    A.v[1] = e1;
    A.v[2] = e2;
    A.v[3] = e3;
    return A.m;
}
#endif

#if HAVE_AVX2_MM256_EXTRACT_EPI64
#define _mm256_extract_epi64_rpl _mm256_extract_epi64
#else
static inline int64_t _mm256_extract_epi64_rpl(__m256i a, int imm) {
    __m256i_64_t A;
    A.m = a;
    return A.v[imm];
}
#endif

#define _mm256_cmplt_epi64_rpl(a,b) _mm256_cmpgt_epi64(b,a)

#define _mm256_srli_si256_rpl(a,imm) _mm256_or_si256(_mm256_slli_si256(_mm256_permute2x128_si256(a, a, _MM_SHUFFLE(3,0,0,1)), 16-imm), _mm256_srli_si256(a, imm))


#ifdef PARASAIL_TABLE
static inline void arr_store_si256(
        int *array,
        __m256i vWH,
        int32_t i,
        int32_t s1Len,
        int32_t j,
        int32_t s2Len)
{
    if (0 <= i+0 && i+0 < s1Len && 0 <= j-0 && j-0 < s2Len) {
        array[1LL*(i+0)*s2Len + (j-0)] = (int64_t)_mm256_extract_epi64_rpl(vWH, 3);
    }
    if (0 <= i+1 && i+1 < s1Len && 0 <= j-1 && j-1 < s2Len) {
        array[1LL*(i+1)*s2Len + (j-1)] = (int64_t)_mm256_extract_epi64_rpl(vWH, 2);
    }
    if (0 <= i+2 && i+2 < s1Len && 0 <= j-2 && j-2 < s2Len) {
        array[1LL*(i+2)*s2Len + (j-2)] = (int64_t)_mm256_extract_epi64_rpl(vWH, 1);
    }
    if (0 <= i+3 && i+3 < s1Len && 0 <= j-3 && j-3 < s2Len) {
        array[1LL*(i+3)*s2Len + (j-3)] = (int64_t)_mm256_extract_epi64_rpl(vWH, 0);
    }
}
#endif

#ifdef PARASAIL_ROWCOL
static inline void arr_store_rowcol(
        int *row,
        int *col,
        __m256i vWH,
        int32_t i,
        int32_t s1Len,
        int32_t j,
        int32_t s2Len)
{
    if (i+0 == s1Len-1 && 0 <= j-0 && j-0 < s2Len) {
        row[j-0] = (int64_t)_mm256_extract_epi64_rpl(vWH, 3);
    }
    if (j-0 == s2Len-1 && 0 <= i+0 && i+0 < s1Len) {
        col[(i+0)] = (int64_t)_mm256_extract_epi64_rpl(vWH, 3);
    }
    if (i+1 == s1Len-1 && 0 <= j-1 && j-1 < s2Len) {
        row[j-1] = (int64_t)_mm256_extract_epi64_rpl(vWH, 2);
    }
    if (j-1 == s2Len-1 && 0 <= i+1 && i+1 < s1Len) {
        col[(i+1)] = (int64_t)_mm256_extract_epi64_rpl(vWH, 2);
    }
    if (i+2 == s1Len-1 && 0 <= j-2 && j-2 < s2Len) {
        row[j-2] = (int64_t)_mm256_extract_epi64_rpl(vWH, 1);
    }
    if (j-2 == s2Len-1 && 0 <= i+2 && i+2 < s1Len) {
        col[(i+2)] = (int64_t)_mm256_extract_epi64_rpl(vWH, 1);
    }
    if (i+3 == s1Len-1 && 0 <= j-3 && j-3 < s2Len) {
        row[j-3] = (int64_t)_mm256_extract_epi64_rpl(vWH, 0);
    }
    if (j-3 == s2Len-1 && 0 <= i+3 && i+3 < s1Len) {
        col[(i+3)] = (int64_t)_mm256_extract_epi64_rpl(vWH, 0);
    }
}
#endif

#ifdef PARASAIL_TABLE
#define FNAME parasail_sg_flags_table_diag_avx2_256_64
#else
#ifdef PARASAIL_ROWCOL
#define FNAME parasail_sg_flags_rowcol_diag_avx2_256_64
#else
#define FNAME parasail_sg_flags_diag_avx2_256_64
#endif
#endif

parasail_result_t* FNAME(
        const char * const restrict _s1, const int s1Len,
        const char * const restrict _s2, const int s2Len,
        const int open, const int gap, const parasail_matrix_t *matrix,
        int s1_beg, int s1_end, int s2_beg, int s2_end)
{
    const int32_t N = 4; /* number of values in vector */
    const int32_t PAD = N-1;
    const int32_t PAD2 = PAD*2;
    const int32_t s1Len_PAD = s1Len+PAD;
    const int32_t s2Len_PAD = s2Len+PAD;
    int64_t * const restrict s1 = parasail_memalign_int64_t(32, s1Len+PAD);
    int64_t * const restrict s2B= parasail_memalign_int64_t(32, s2Len+PAD2);
    int64_t * const restrict _H_pr = parasail_memalign_int64_t(32, s2Len+PAD2);
    int64_t * const restrict _F_pr = parasail_memalign_int64_t(32, s2Len+PAD2);
    int64_t * const restrict s2 = s2B+PAD; /* will allow later for negative indices */
    int64_t * const restrict H_pr = _H_pr+PAD;
    int64_t * const restrict F_pr = _F_pr+PAD;
#ifdef PARASAIL_TABLE
    parasail_result_t *result = parasail_result_new_table1(s1Len, s2Len);
#else
#ifdef PARASAIL_ROWCOL
    parasail_result_t *result = parasail_result_new_rowcol1(s1Len, s2Len);
#else
    parasail_result_t *result = parasail_result_new();
#endif
#endif
    int32_t i = 0;
    int32_t j = 0;
    int32_t end_query = 0;
    int32_t end_ref = 0;
    int64_t score = NEG_INF;
    __m256i vNegInf = _mm256_set1_epi64x_rpl(NEG_INF);
    __m256i vOpen = _mm256_set1_epi64x_rpl(open);
    __m256i vGap  = _mm256_set1_epi64x_rpl(gap);
    __m256i vOne = _mm256_set1_epi64x_rpl(1);
    __m256i vN = _mm256_set1_epi64x_rpl(N);
    __m256i vGapN = s1_beg ? _mm256_set1_epi64x_rpl(0) : _mm256_set1_epi64x_rpl(gap*N);
    __m256i vNegOne = _mm256_set1_epi64x_rpl(-1);
    __m256i vI = _mm256_set_epi64x_rpl(0,1,2,3);
    __m256i vJreset = _mm256_set_epi64x_rpl(0,-1,-2,-3);
    __m256i vMaxHRow = vNegInf;
    __m256i vMaxHCol = vNegInf;
    __m256i vLastVal = vNegInf;
    __m256i vEndI = vNegInf;
    __m256i vEndJ = vNegInf;
    __m256i vILimit = _mm256_set1_epi64x_rpl(s1Len);
    __m256i vILimit1 = _mm256_sub_epi64(vILimit, vOne);
    __m256i vJLimit = _mm256_set1_epi64x_rpl(s2Len);
    __m256i vJLimit1 = _mm256_sub_epi64(vJLimit, vOne);
    __m256i vIBoundary = s1_beg ? _mm256_set1_epi64x_rpl(0) : _mm256_set_epi64x_rpl(
            -open-0*gap,
            -open-1*gap,
            -open-2*gap,
            -open-3*gap
            );
    

    /* convert _s1 from char to int in range 0-23 */
    for (i=0; i<s1Len; ++i) {
        s1[i] = matrix->mapper[(unsigned char)_s1[i]];
    }
    /* pad back of s1 with dummy values */
    for (i=s1Len; i<s1Len_PAD; ++i) {
        s1[i] = 0; /* point to first matrix row because we don't care */
    }

    /* convert _s2 from char to int in range 0-23 */
    for (j=0; j<s2Len; ++j) {
        s2[j] = matrix->mapper[(unsigned char)_s2[j]];
    }
    /* pad front of s2 with dummy values */
    for (j=-PAD; j<0; ++j) {
        s2[j] = 0; /* point to first matrix row because we don't care */
    }
    /* pad back of s2 with dummy values */
    for (j=s2Len; j<s2Len_PAD; ++j) {
        s2[j] = 0; /* point to first matrix row because we don't care */
    }

    /* set initial values for stored row */
    if (s2_beg) {
        for (j=0; j<s2Len; ++j) {
            H_pr[j] = 0;
            F_pr[j] = NEG_INF;
        }
    }
    else {
        for (j=0; j<s2Len; ++j) {
            H_pr[j] = -open - j*gap;
            F_pr[j] = NEG_INF;
        }
    }
    /* pad front of stored row values */
    for (j=-PAD; j<0; ++j) {
        H_pr[j] = NEG_INF;
        F_pr[j] = NEG_INF;
    }
    /* pad back of stored row values */
    for (j=s2Len; j<s2Len+PAD; ++j) {
        H_pr[j] = NEG_INF;
        F_pr[j] = NEG_INF;
    }
    H_pr[-1] = 0; /* upper left corner */

    /* iterate over query sequence */
    for (i=0; i<s1Len; i+=N) {
        __m256i vNH = vNegInf;
        __m256i vWH = vNegInf;
        __m256i vE = vNegInf;
        __m256i vF = vNegInf;
        __m256i vJ = vJreset;
        const int * const restrict matrow0 = &matrix->matrix[matrix->size*s1[i+0]];
        const int * const restrict matrow1 = &matrix->matrix[matrix->size*s1[i+1]];
        const int * const restrict matrow2 = &matrix->matrix[matrix->size*s1[i+2]];
        const int * const restrict matrow3 = &matrix->matrix[matrix->size*s1[i+3]];
        __m256i vIltLimit = _mm256_cmplt_epi64_rpl(vI, vILimit);
        __m256i vIeqLimit1 = _mm256_cmpeq_epi64(vI, vILimit1);
        vNH = _mm256_srli_si256_rpl(vNH, 8);
        vNH = _mm256_insert_epi64_rpl(vNH, H_pr[-1], 3);
        vWH = _mm256_srli_si256_rpl(vWH, 8);
        vWH = _mm256_insert_epi64_rpl(vWH, s1_beg ? 0 : (-open - i*gap), 3);
        H_pr[-1] = -open - (i+N)*gap;
        /* iterate over database sequence */
        for (j=0; j<s2Len+PAD; ++j) {
            __m256i vMat;
            __m256i vNWH = vNH;
            vNH = _mm256_srli_si256_rpl(vWH, 8);
            vNH = _mm256_insert_epi64_rpl(vNH, H_pr[j], 3);
            vF = _mm256_srli_si256_rpl(vF, 8);
            vF = _mm256_insert_epi64_rpl(vF, F_pr[j], 3);
            vF = _mm256_max_epi64_rpl(
                    _mm256_sub_epi64(vNH, vOpen),
                    _mm256_sub_epi64(vF, vGap));
            vE = _mm256_max_epi64_rpl(
                    _mm256_sub_epi64(vWH, vOpen),
                    _mm256_sub_epi64(vE, vGap));
            vMat = _mm256_set_epi64x_rpl(
                    matrow0[s2[j-0]],
                    matrow1[s2[j-1]],
                    matrow2[s2[j-2]],
                    matrow3[s2[j-3]]
                    );
            vNWH = _mm256_add_epi64(vNWH, vMat);
            vWH = _mm256_max_epi64_rpl(vNWH, vE);
            vWH = _mm256_max_epi64_rpl(vWH, vF);
            /* as minor diagonal vector passes across the j=-1 boundary,
             * assign the appropriate boundary conditions */
            {
                __m256i cond = _mm256_cmpeq_epi64(vJ,vNegOne);
                vWH = _mm256_blendv_epi8(vWH, vIBoundary, cond);
                vF = _mm256_blendv_epi8(vF, vNegInf, cond);
                vE = _mm256_blendv_epi8(vE, vNegInf, cond);
            }
            
#ifdef PARASAIL_TABLE
            arr_store_si256(result->tables->score_table, vWH, i, s1Len, j, s2Len);
#endif
#ifdef PARASAIL_ROWCOL
            arr_store_rowcol(result->rowcols->score_row, result->rowcols->score_col, vWH, i, s1Len, j, s2Len);
#endif
            H_pr[j-3] = (int64_t)_mm256_extract_epi64_rpl(vWH,0);
            F_pr[j-3] = (int64_t)_mm256_extract_epi64_rpl(vF,0);
            /* as minor diagonal vector passes across the i or j limit
             * boundary, extract the last value of the column or row */
            {
                __m256i vJeqLimit1 = _mm256_cmpeq_epi64(vJ, vJLimit1);
                __m256i vJgtNegOne = _mm256_cmpgt_epi64(vJ, vNegOne);
                __m256i vJltLimit = _mm256_cmplt_epi64_rpl(vJ, vJLimit);
                __m256i cond_j = _mm256_and_si256(vIltLimit, vJeqLimit1);
                __m256i cond_i = _mm256_and_si256(vIeqLimit1,
                        _mm256_and_si256(vJgtNegOne, vJltLimit));
                __m256i cond_max_row = _mm256_cmpgt_epi64(vWH, vMaxHRow);
                __m256i cond_max_col = _mm256_cmpgt_epi64(vWH, vMaxHCol);
                __m256i cond_last_val = _mm256_and_si256(vIeqLimit1, vJeqLimit1);
                __m256i cond_all_row = _mm256_and_si256(cond_max_row, cond_i);
                __m256i cond_all_col = _mm256_and_si256(cond_max_col, cond_j);
                vMaxHRow = _mm256_blendv_epi8(vMaxHRow, vWH, cond_all_row);
                vMaxHCol = _mm256_blendv_epi8(vMaxHCol, vWH, cond_all_col);
                vLastVal = _mm256_blendv_epi8(vLastVal, vWH, cond_last_val);
                vEndI = _mm256_blendv_epi8(vEndI, vI, cond_all_col);
                vEndJ = _mm256_blendv_epi8(vEndJ, vJ, cond_all_row);
            }
            vJ = _mm256_add_epi64(vJ, vOne);
        }
        vI = _mm256_add_epi64(vI, vN);
        vIBoundary = _mm256_sub_epi64(vIBoundary, vGapN);
    }

    /* alignment ending position */
    {
        int64_t max_row = NEG_INF;
        int64_t max_col = NEG_INF;
        int64_t last_val = NEG_INF;
        int64_t *s = (int64_t*)&vMaxHRow;
        int64_t *t = (int64_t*)&vMaxHCol;
        int64_t *u = (int64_t*)&vLastVal;
        int64_t *i = (int64_t*)&vEndI;
        int64_t *j = (int64_t*)&vEndJ;
        int32_t k;
        for (k=0; k<N; ++k, ++s, ++t, ++u, ++i, ++j) {
            if (*t > max_col || (*t == max_col && *i < end_query)) {
                max_col = *t;
                end_query = *i;
            }
            if (*s > max_row) {
                max_row = *s;
                end_ref = *j;
            }
            if (*u > last_val) {
                last_val = *u;
            }
        }
        if (s1_end && s2_end) {
            if (max_col > max_row || (max_col == max_row && end_ref == s2Len-1)) {
                score = max_col;
                end_ref = s2Len-1;
            }
            else {
                score = max_row;
                end_query = s1Len-1;
            }
        }
        else if (s1_end) {
            score = max_col;
            end_ref = s2Len-1;
        }
        else if (s2_end) {
            score = max_row;
            end_query = s1Len-1;
        }
        else {
            score = last_val;
            end_query = s1Len-1;
            end_ref = s2Len-1;
        }
    }

    

    result->score = score;
    result->end_query = end_query;
    result->end_ref = end_ref;
    result->flag |= PARASAIL_FLAG_SG | PARASAIL_FLAG_DIAG
        | PARASAIL_FLAG_BITS_64 | PARASAIL_FLAG_LANES_4;
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

    parasail_free(_F_pr);
    parasail_free(_H_pr);
    parasail_free(s2B);
    parasail_free(s1);

    return result;
}

SG_IMPL_ALL


