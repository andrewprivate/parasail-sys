/**
 * @file
 *
 * @author jeffrey.daily@gmail.com
 *
 * Copyright (c) 2015 Battelle Memorial Institute.
 */
#include "config.h"

#include <stdlib.h>



#include "parasail.h"
#include "parasail/memory.h"
#include "parasail/internal_neon.h"

#define SG_STATS
#define SG_SUFFIX _diag_neon_128_64
#include "sg_helper.h"

#define NEG_INF (INT64_MIN/(int64_t)(2))


#ifdef PARASAIL_TABLE
static inline void arr_store_si128(
        int *array,
        simde__m128i vWH,
        int32_t i,
        int32_t s1Len,
        int32_t j,
        int32_t s2Len)
{
    if (0 <= i+0 && i+0 < s1Len && 0 <= j-0 && j-0 < s2Len) {
        array[1LL*(i+0)*s2Len + (j-0)] = (int64_t)simde_mm_extract_epi64(vWH, 1);
    }
    if (0 <= i+1 && i+1 < s1Len && 0 <= j-1 && j-1 < s2Len) {
        array[1LL*(i+1)*s2Len + (j-1)] = (int64_t)simde_mm_extract_epi64(vWH, 0);
    }
}
#endif

#ifdef PARASAIL_ROWCOL
static inline void arr_store_rowcol(
        int *row,
        int *col,
        simde__m128i vWH,
        int32_t i,
        int32_t s1Len,
        int32_t j,
        int32_t s2Len)
{
    if (i+0 == s1Len-1 && 0 <= j-0 && j-0 < s2Len) {
        row[j-0] = (int64_t)simde_mm_extract_epi64(vWH, 1);
    }
    if (j-0 == s2Len-1 && 0 <= i+0 && i+0 < s1Len) {
        col[(i+0)] = (int64_t)simde_mm_extract_epi64(vWH, 1);
    }
    if (i+1 == s1Len-1 && 0 <= j-1 && j-1 < s2Len) {
        row[j-1] = (int64_t)simde_mm_extract_epi64(vWH, 0);
    }
    if (j-1 == s2Len-1 && 0 <= i+1 && i+1 < s1Len) {
        col[(i+1)] = (int64_t)simde_mm_extract_epi64(vWH, 0);
    }
}
#endif

#ifdef PARASAIL_TABLE
#define FNAME parasail_sg_flags_stats_table_diag_neon_128_64
#else
#ifdef PARASAIL_ROWCOL
#define FNAME parasail_sg_flags_stats_rowcol_diag_neon_128_64
#else
#define FNAME parasail_sg_flags_stats_diag_neon_128_64
#endif
#endif

parasail_result_t* FNAME(
        const char * const restrict _s1, const int s1Len,
        const char * const restrict _s2, const int s2Len,
        const int open, const int gap, const parasail_matrix_t *matrix,
        int s1_beg, int s1_end, int s2_beg, int s2_end)
{
    const int32_t N = 2; /* number of values in vector */
    const int32_t PAD = N-1;
    const int32_t PAD2 = PAD*2;
    const int32_t s1Len_PAD = s1Len+PAD;
    const int32_t s2Len_PAD = s2Len+PAD;
    int64_t * const restrict s1      = parasail_memalign_int64_t(16, s1Len+PAD);
    int64_t * const restrict s2B     = parasail_memalign_int64_t(16, s2Len+PAD2);
    int64_t * const restrict _H_pr = parasail_memalign_int64_t(16, s2Len+PAD2);
    int64_t * const restrict _HM_pr = parasail_memalign_int64_t(16, s2Len+PAD2);
    int64_t * const restrict _HS_pr = parasail_memalign_int64_t(16, s2Len+PAD2);
    int64_t * const restrict _HL_pr = parasail_memalign_int64_t(16, s2Len+PAD2);
    int64_t * const restrict _F_pr = parasail_memalign_int64_t(16, s2Len+PAD2);
    int64_t * const restrict _FM_pr = parasail_memalign_int64_t(16, s2Len+PAD2);
    int64_t * const restrict _FS_pr = parasail_memalign_int64_t(16, s2Len+PAD2);
    int64_t * const restrict _FL_pr = parasail_memalign_int64_t(16, s2Len+PAD2);
    int64_t * const restrict s2 = s2B+PAD; /* will allow later for negative indices */
    int64_t * const restrict H_pr = _H_pr+PAD;
    int64_t * const restrict HM_pr = _HM_pr+PAD;
    int64_t * const restrict HS_pr = _HS_pr+PAD;
    int64_t * const restrict HL_pr = _HL_pr+PAD;
    int64_t * const restrict F_pr = _F_pr+PAD;
    int64_t * const restrict FM_pr = _FM_pr+PAD;
    int64_t * const restrict FS_pr = _FS_pr+PAD;
    int64_t * const restrict FL_pr = _FL_pr+PAD;
#ifdef PARASAIL_TABLE
    parasail_result_t *result = parasail_result_new_table3(s1Len, s2Len);
#else
#ifdef PARASAIL_ROWCOL
    parasail_result_t *result = parasail_result_new_rowcol3(s1Len, s2Len);
#else
    parasail_result_t *result = parasail_result_new_stats();
#endif
#endif
    int32_t i = 0;
    int32_t j = 0;
    int32_t end_query = s1Len-1;
    int32_t end_ref = s2Len-1;
    const int64_t NEG_LIMIT = (-open < matrix->min ?
        INT64_MIN + open : INT64_MIN - matrix->min) + 1;
    const int64_t POS_LIMIT = INT64_MAX - matrix->max - 1;
    int64_t score = NEG_LIMIT;
    int64_t matches = NEG_LIMIT;
    int64_t similar = NEG_LIMIT;
    int64_t length = NEG_LIMIT;
    simde__m128i vNegLimit = simde_mm_set1_epi64x(NEG_LIMIT);
    simde__m128i vPosLimit = simde_mm_set1_epi64x(POS_LIMIT);
    simde__m128i vSaturationCheckMin = vPosLimit;
    simde__m128i vSaturationCheckMax = vNegLimit;
    simde__m128i vNegInf = simde_mm_set1_epi64x(NEG_LIMIT);
    simde__m128i vOpen = simde_mm_set1_epi64x(open);
    simde__m128i vGap  = simde_mm_set1_epi64x(gap);
    simde__m128i vZero = simde_mm_set1_epi64x(0);
    simde__m128i vOne = simde_mm_set1_epi64x(1);
    simde__m128i vN = simde_mm_set1_epi64x(N);
    simde__m128i vGapN = s1_beg ? simde_mm_set1_epi64x(0) : simde_mm_set1_epi64x(gap*N);
    simde__m128i vNegOne = simde_mm_set1_epi64x(-1);
    simde__m128i vI = simde_mm_set_epi64x(0,1);
    simde__m128i vJreset = simde_mm_set_epi64x(0,-1);
    simde__m128i vMaxHRow = vNegInf;
    simde__m128i vMaxMRow = vNegInf;
    simde__m128i vMaxSRow = vNegInf;
    simde__m128i vMaxLRow = vNegInf;
    simde__m128i vMaxHCol = vNegInf;
    simde__m128i vMaxMCol = vNegInf;
    simde__m128i vMaxSCol = vNegInf;
    simde__m128i vMaxLCol = vNegInf;
    simde__m128i vLastValH = vNegInf;
    simde__m128i vLastValM = vNegInf;
    simde__m128i vLastValS = vNegInf;
    simde__m128i vLastValL = vNegInf;
    simde__m128i vEndI = vNegInf;
    simde__m128i vEndJ = vNegInf;
    simde__m128i vILimit = simde_mm_set1_epi64x(s1Len);
    simde__m128i vILimit1 = simde_mm_sub_epi64(vILimit, vOne);
    simde__m128i vJLimit = simde_mm_set1_epi64x(s2Len);
    simde__m128i vJLimit1 = simde_mm_sub_epi64(vJLimit, vOne);
    simde__m128i vIBoundary = s1_beg ? simde_mm_set1_epi64x(0) : simde_mm_set_epi64x(
            -open-0*gap,
            -open-1*gap);

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
            HM_pr[j] = 0;
            HS_pr[j] = 0;
            HL_pr[j] = 0;
            F_pr[j] = NEG_INF;
            FM_pr[j] = 0;
            FS_pr[j] = 0;
            FL_pr[j] = 0;
        }
    }
    else {
        for (j=0; j<s2Len; ++j) {
            H_pr[j] = -open - j*gap;
            HM_pr[j] = 0;
            HS_pr[j] = 0;
            HL_pr[j] = 0;
            F_pr[j] = NEG_INF;
            FM_pr[j] = 0;
            FS_pr[j] = 0;
            FL_pr[j] = 0;
        }
    }
    /* pad front of stored row values */
    for (j=-PAD; j<0; ++j) {
        H_pr[j] = 0;
        HM_pr[j] = 0;
        HS_pr[j] = 0;
        HL_pr[j] = 0;
        F_pr[j] = 0;
        FM_pr[j] = 0;
        FS_pr[j] = 0;
        FL_pr[j] = 0;
    }
    /* pad back of stored row values */
    for (j=s2Len; j<s2Len+PAD; ++j) {
        H_pr[j] = 0;
        HM_pr[j] = 0;
        HS_pr[j] = 0;
        HL_pr[j] = 0;
        F_pr[j] = 0;
        FM_pr[j] = 0;
        FS_pr[j] = 0;
        FL_pr[j] = 0;
    }
    H_pr[-1] = 0; /* upper left corner */

    /* iterate over query sequence */
    for (i=0; i<s1Len; i+=N) {
        simde__m128i case1 = vZero;
        simde__m128i case2 = vZero;
        simde__m128i vNH = vNegInf;
        simde__m128i vNM = vZero;
        simde__m128i vNS = vZero;
        simde__m128i vNL = vZero;
        simde__m128i vWH = vNegInf;
        simde__m128i vWM = vZero;
        simde__m128i vWS = vZero;
        simde__m128i vWL = vZero;
        simde__m128i vE = vNegInf;
        simde__m128i vE_opn = vNegInf;
        simde__m128i vE_ext = vNegInf;
        simde__m128i vEM = vZero;
        simde__m128i vES = vZero;
        simde__m128i vEL = vZero;
        simde__m128i vF = vNegInf;
        simde__m128i vF_opn = vNegInf;
        simde__m128i vF_ext = vNegInf;
        simde__m128i vFM = vZero;
        simde__m128i vFS = vZero;
        simde__m128i vFL = vZero;
        simde__m128i vJ = vJreset;
        simde__m128i vs1 = simde_mm_set_epi64x(
                s1[i+0],
                s1[i+1]);
        simde__m128i vs2 = vNegInf;
        const int * const restrict matrow0 = &matrix->matrix[matrix->size*s1[i+0]];
        const int * const restrict matrow1 = &matrix->matrix[matrix->size*s1[i+1]];
        simde__m128i vIltLimit = simde_mm_cmplt_epi64(vI, vILimit);
        simde__m128i vIeqLimit1 = simde_mm_cmpeq_epi64(vI, vILimit1);
        vNH = simde_mm_srli_si128(vNH, 8);
        vNH = simde_mm_insert_epi64(vNH, H_pr[-1], 1);
        vWH = simde_mm_srli_si128(vWH, 8);
        vWH = simde_mm_insert_epi64(vWH, s1_beg ? 0 : (-open - i*gap), 1);
        H_pr[-1] = -open - (i+N)*gap;
        /* iterate over database sequence */
        for (j=0; j<s2Len+PAD; ++j) {
            simde__m128i vMat;
            simde__m128i vNWH = vNH;
            simde__m128i vNWM = vNM;
            simde__m128i vNWS = vNS;
            simde__m128i vNWL = vNL;
            vNH = simde_mm_srli_si128(vWH, 8);
            vNH = simde_mm_insert_epi64(vNH, H_pr[j], 1);
            vNM = simde_mm_srli_si128(vWM, 8);
            vNM = simde_mm_insert_epi64(vNM, HM_pr[j], 1);
            vNS = simde_mm_srli_si128(vWS, 8);
            vNS = simde_mm_insert_epi64(vNS, HS_pr[j], 1);
            vNL = simde_mm_srli_si128(vWL, 8);
            vNL = simde_mm_insert_epi64(vNL, HL_pr[j], 1);
            vF = simde_mm_srli_si128(vF, 8);
            vF = simde_mm_insert_epi64(vF, F_pr[j], 1);
            vFM = simde_mm_srli_si128(vFM, 8);
            vFM = simde_mm_insert_epi64(vFM, FM_pr[j], 1);
            vFS = simde_mm_srli_si128(vFS, 8);
            vFS = simde_mm_insert_epi64(vFS, FS_pr[j], 1);
            vFL = simde_mm_srli_si128(vFL, 8);
            vFL = simde_mm_insert_epi64(vFL, FL_pr[j], 1);
            vF_opn = simde_mm_sub_epi64(vNH, vOpen);
            vF_ext = simde_mm_sub_epi64(vF, vGap);
            vF = simde_mm_max_epi64(vF_opn, vF_ext);
            case1 = simde_mm_cmpgt_epi64(vF_opn, vF_ext);
            vFM = simde_mm_blendv_epi8(vFM, vNM, case1);
            vFS = simde_mm_blendv_epi8(vFS, vNS, case1);
            vFL = simde_mm_blendv_epi8(vFL, vNL, case1);
            vFL = simde_mm_add_epi64(vFL, vOne);
            vE_opn = simde_mm_sub_epi64(vWH, vOpen);
            vE_ext = simde_mm_sub_epi64(vE, vGap);
            vE = simde_mm_max_epi64(vE_opn, vE_ext);
            case1 = simde_mm_cmpgt_epi64(vE_opn, vE_ext);
            vEM = simde_mm_blendv_epi8(vEM, vWM, case1);
            vES = simde_mm_blendv_epi8(vES, vWS, case1);
            vEL = simde_mm_blendv_epi8(vEL, vWL, case1);
            vEL = simde_mm_add_epi64(vEL, vOne);
            vs2 = simde_mm_srli_si128(vs2, 8);
            vs2 = simde_mm_insert_epi64(vs2, s2[j], 1);
            vMat = simde_mm_set_epi64x(
                    matrow0[s2[j-0]],
                    matrow1[s2[j-1]]
                    );
            vNWH = simde_mm_add_epi64(vNWH, vMat);
            vWH = simde_mm_max_epi64(vNWH, vE);
            vWH = simde_mm_max_epi64(vWH, vF);
            case1 = simde_mm_cmpeq_epi64(vWH, vNWH);
            case2 = simde_mm_cmpeq_epi64(vWH, vF);
            vWM = simde_mm_blendv_epi8(
                    simde_mm_blendv_epi8(vEM, vFM, case2),
                    simde_mm_add_epi64(vNWM,
                        simde_mm_and_si128(
                            simde_mm_cmpeq_epi64(vs1,vs2),
                            vOne)),
                    case1);
            vWS = simde_mm_blendv_epi8(
                    simde_mm_blendv_epi8(vES, vFS, case2),
                    simde_mm_add_epi64(vNWS,
                        simde_mm_and_si128(
                            simde_mm_cmpgt_epi64(vMat,vZero),
                            vOne)),
                    case1);
            vWL = simde_mm_blendv_epi8(
                    simde_mm_blendv_epi8(vEL, vFL, case2),
                    simde_mm_add_epi64(vNWL, vOne), case1);
            /* as minor diagonal vector passes across the j=-1 boundary,
             * assign the appropriate boundary conditions */
            {
                simde__m128i cond = simde_mm_cmpeq_epi64(vJ,vNegOne);
                vWH = simde_mm_blendv_epi8(vWH, vIBoundary, cond);
                vWM = simde_mm_andnot_si128(cond, vWM);
                vWS = simde_mm_andnot_si128(cond, vWS);
                vWL = simde_mm_andnot_si128(cond, vWL);
                vE = simde_mm_blendv_epi8(vE, vNegInf, cond);
                vEM = simde_mm_andnot_si128(cond, vEM);
                vES = simde_mm_andnot_si128(cond, vES);
                vEL = simde_mm_andnot_si128(cond, vEL);
            }
            vSaturationCheckMin = simde_mm_min_epi64(vSaturationCheckMin, vWH);
            vSaturationCheckMax = simde_mm_max_epi64(vSaturationCheckMax, vWH);
            vSaturationCheckMax = simde_mm_max_epi64(vSaturationCheckMax, vWM);
            vSaturationCheckMax = simde_mm_max_epi64(vSaturationCheckMax, vWS);
            vSaturationCheckMax = simde_mm_max_epi64(vSaturationCheckMax, vWL);
            vSaturationCheckMax = simde_mm_max_epi64(vSaturationCheckMax, vWL);
            vSaturationCheckMax = simde_mm_max_epi64(vSaturationCheckMax, vJ);
#ifdef PARASAIL_TABLE
            arr_store_si128(result->stats->tables->score_table, vWH, i, s1Len, j, s2Len);
            arr_store_si128(result->stats->tables->matches_table, vWM, i, s1Len, j, s2Len);
            arr_store_si128(result->stats->tables->similar_table, vWS, i, s1Len, j, s2Len);
            arr_store_si128(result->stats->tables->length_table, vWL, i, s1Len, j, s2Len);
#endif
#ifdef PARASAIL_ROWCOL
            arr_store_rowcol(result->stats->rowcols->score_row,   result->stats->rowcols->score_col, vWH, i, s1Len, j, s2Len);
            arr_store_rowcol(result->stats->rowcols->matches_row, result->stats->rowcols->matches_col, vWM, i, s1Len, j, s2Len);
            arr_store_rowcol(result->stats->rowcols->similar_row, result->stats->rowcols->similar_col, vWS, i, s1Len, j, s2Len);
            arr_store_rowcol(result->stats->rowcols->length_row,  result->stats->rowcols->length_col, vWL, i, s1Len, j, s2Len);
#endif
            H_pr[j-1] = (int64_t)simde_mm_extract_epi64(vWH,0);
            HM_pr[j-1] = (int64_t)simde_mm_extract_epi64(vWM,0);
            HS_pr[j-1] = (int64_t)simde_mm_extract_epi64(vWS,0);
            HL_pr[j-1] = (int64_t)simde_mm_extract_epi64(vWL,0);
            F_pr[j-1] = (int64_t)simde_mm_extract_epi64(vF,0);
            FM_pr[j-1] = (int64_t)simde_mm_extract_epi64(vFM,0);
            FS_pr[j-1] = (int64_t)simde_mm_extract_epi64(vFS,0);
            FL_pr[j-1] = (int64_t)simde_mm_extract_epi64(vFL,0);
            /* as minor diagonal vector passes across the i or j limit
             * boundary, extract the last value of the column or row */
            {
                simde__m128i vJeqLimit1 = simde_mm_cmpeq_epi64(vJ, vJLimit1);
                simde__m128i vJgtNegOne = simde_mm_cmpgt_epi64(vJ, vNegOne);
                simde__m128i vJltLimit = simde_mm_cmplt_epi64(vJ, vJLimit);
                simde__m128i cond_j = simde_mm_and_si128(vIltLimit, vJeqLimit1);
                simde__m128i cond_i = simde_mm_and_si128(vIeqLimit1,
                        simde_mm_and_si128(vJgtNegOne, vJltLimit));
                simde__m128i cond_max_row = simde_mm_cmpgt_epi64(vWH, vMaxHRow);
                simde__m128i cond_max_col = simde_mm_cmpgt_epi64(vWH, vMaxHCol);
                simde__m128i cond_last_val = simde_mm_and_si128(vIeqLimit1, vJeqLimit1);
                simde__m128i cond_all_row = simde_mm_and_si128(cond_max_row, cond_i);
                simde__m128i cond_all_col = simde_mm_and_si128(cond_max_col, cond_j);
                vMaxHRow = simde_mm_blendv_epi8(vMaxHRow, vWH, cond_all_row);
                vMaxMRow = simde_mm_blendv_epi8(vMaxMRow, vWM, cond_all_row);
                vMaxSRow = simde_mm_blendv_epi8(vMaxSRow, vWS, cond_all_row);
                vMaxLRow = simde_mm_blendv_epi8(vMaxLRow, vWL, cond_all_row);
                vMaxHCol = simde_mm_blendv_epi8(vMaxHCol, vWH, cond_all_col);
                vMaxMCol = simde_mm_blendv_epi8(vMaxMCol, vWM, cond_all_col);
                vMaxSCol = simde_mm_blendv_epi8(vMaxSCol, vWS, cond_all_col);
                vMaxLCol = simde_mm_blendv_epi8(vMaxLCol, vWL, cond_all_col);
                vLastValH = simde_mm_blendv_epi8(vLastValH, vWH, cond_last_val);
                vLastValM = simde_mm_blendv_epi8(vLastValM, vWM, cond_last_val);
                vLastValS = simde_mm_blendv_epi8(vLastValS, vWS, cond_last_val);
                vLastValL = simde_mm_blendv_epi8(vLastValL, vWL, cond_last_val);
                vEndI = simde_mm_blendv_epi8(vEndI, vI, cond_all_col);
                vEndJ = simde_mm_blendv_epi8(vEndJ, vJ, cond_all_row);
            }
            vJ = simde_mm_add_epi64(vJ, vOne);
        }
        vI = simde_mm_add_epi64(vI, vN);
        vIBoundary = simde_mm_sub_epi64(vIBoundary, vGapN);
        vSaturationCheckMax = simde_mm_max_epi64(vSaturationCheckMax, vI);
    }

    /* alignment ending position */
    {
        int64_t max_rowh = NEG_INF;
        int64_t max_rowm = NEG_INF;
        int64_t max_rows = NEG_INF;
        int64_t max_rowl = NEG_INF;
        int64_t max_colh = NEG_INF;
        int64_t max_colm = NEG_INF;
        int64_t max_cols = NEG_INF;
        int64_t max_coll = NEG_INF;
        int64_t last_valh = NEG_INF;
        int64_t last_valm = NEG_INF;
        int64_t last_vals = NEG_INF;
        int64_t last_vall = NEG_INF;
        int64_t *rh = (int64_t*)&vMaxHRow;
        int64_t *rm = (int64_t*)&vMaxMRow;
        int64_t *rs = (int64_t*)&vMaxSRow;
        int64_t *rl = (int64_t*)&vMaxLRow;
        int64_t *ch = (int64_t*)&vMaxHCol;
        int64_t *cm = (int64_t*)&vMaxMCol;
        int64_t *cs = (int64_t*)&vMaxSCol;
        int64_t *cl = (int64_t*)&vMaxLCol;
        int64_t *lh = (int64_t*)&vLastValH;
        int64_t *lm = (int64_t*)&vLastValM;
        int64_t *ls = (int64_t*)&vLastValS;
        int64_t *ll = (int64_t*)&vLastValL;
        int64_t *i = (int64_t*)&vEndI;
        int64_t *j = (int64_t*)&vEndJ;
        int32_t k;
        for (k=0; k<N; ++k, ++rh, ++rm, ++rs, ++rl, ++ch, ++cm, ++cs, ++cl, ++lh, ++lm, ++ls, ++ll, ++i, ++j) {
            if (*ch > max_colh || (*ch == max_colh && *i < end_query)) {
                max_colh = *ch;
                end_query = *i;
                max_colm = *cm;
                max_cols = *cs;
                max_coll = *cl;
            }
            if (*rh > max_rowh) {
                max_rowh = *rh;
                end_ref = *j;
                max_rowm = *rm;
                max_rows = *rs;
                max_rowl = *rl;
            }
            if (*lh > last_valh) {
                last_valh = *lh;
                last_valm = *lm;
                last_vals = *ls;
                last_vall = *ll;
            }
        }
        if (s1_end && s2_end) {
            if (max_colh > max_rowh || (max_colh == max_rowh && end_ref == s2Len-1)) {
                score = max_colh;
                end_ref = s2Len-1;
                matches = max_colm;
                similar = max_cols;
                length = max_coll;
            }
            else {
                score = max_rowh;
                end_query = s1Len-1;
                matches = max_rowm;
                similar = max_rows;
                length = max_rowl;
            }
        }
        else if (s1_end) {
            score = max_colh;
            end_ref = s2Len-1;
            matches = max_colm;
            similar = max_cols;
            length = max_coll;
        }
        else if (s2_end) {
            score = max_rowh;
            end_query = s1Len-1;
            matches = max_rowm;
            similar = max_rows;
            length = max_rowl;
        }
        else {
            score = last_valh;
            end_query = s1Len-1;
            end_ref = s2Len-1;
            matches = last_valm;
            similar = last_vals;
            length = last_vall;
        }
    }

    if (simde_mm_movemask_epi8(simde_mm_or_si128(
            simde_mm_cmplt_epi64(vSaturationCheckMin, vNegLimit),
            simde_mm_cmpgt_epi64(vSaturationCheckMax, vPosLimit)))) {
        result->flag |= PARASAIL_FLAG_SATURATED;
        score = 0;
        matches = 0;
        similar = 0;
        length = 0;
        end_query = 0;
        end_ref = 0;
    }

    result->score = score;
    result->end_query = end_query;
    result->end_ref = end_ref;
    result->stats->matches = matches;
    result->stats->similar = similar;
    result->stats->length = length;
    result->flag |= PARASAIL_FLAG_SG | PARASAIL_FLAG_DIAG
        | PARASAIL_FLAG_STATS
        | PARASAIL_FLAG_BITS_64 | PARASAIL_FLAG_LANES_2;
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

    parasail_free(_FL_pr);
    parasail_free(_FS_pr);
    parasail_free(_FM_pr);
    parasail_free(_F_pr);
    parasail_free(_HL_pr);
    parasail_free(_HS_pr);
    parasail_free(_HM_pr);
    parasail_free(_H_pr);
    parasail_free(s2B);
    parasail_free(s1);

    return result;
}

SG_IMPL_ALL

