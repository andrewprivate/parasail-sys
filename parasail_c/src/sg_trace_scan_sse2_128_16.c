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

#if defined(_MSC_VER)
#include <intrin.h>
#else
#include <emmintrin.h>
#endif

#include "parasail.h"
#include "parasail/memory.h"
#include "parasail/internal_sse.h"

#define SG_TRACE
#define SG_SUFFIX _scan_sse2_128_16
#define SG_SUFFIX_PROF _scan_profile_sse2_128_16
#include "sg_helper.h"


static inline __m128i _mm_blendv_epi8_rpl(__m128i a, __m128i b, __m128i mask) {
    a = _mm_andnot_si128(mask, a);
    a = _mm_or_si128(a, _mm_and_si128(mask, b));
    return a;
}


static inline void arr_store(
        __m128i *array,
        __m128i vH,
        int32_t t,
        int32_t seglen,
        int32_t d)
{
    _mm_store_si128(array + (1LL*d*seglen+t), vH);
}

static inline __m128i arr_load(
        __m128i *array,
        int32_t t,
        int32_t seglen,
        int32_t d)
{
    return _mm_load_si128(array + (1LL*d*seglen+t));
}

#define FNAME parasail_sg_flags_trace_scan_sse2_128_16
#define PNAME parasail_sg_flags_trace_scan_profile_sse2_128_16

parasail_result_t* FNAME(
        const char * const restrict s1, const int s1Len,
        const char * const restrict s2, const int s2Len,
        const int open, const int gap, const parasail_matrix_t *matrix,
        int s1_beg, int s1_end, int s2_beg, int s2_end)
{
    parasail_profile_t *profile = parasail_profile_create_sse_128_16(s1, s1Len, matrix);
    parasail_result_t *result = PNAME(profile, s2, s2Len, open, gap, s1_beg, s1_end, s2_beg, s2_end);
    parasail_profile_free(profile);
    return result;
}

parasail_result_t* PNAME(
        const parasail_profile_t * const restrict profile,
        const char * const restrict s2, const int s2Len,
        const int open, const int gap,
        int s1_beg, int s1_end, int s2_beg, int s2_end)
{
    int32_t i = 0;
    int32_t j = 0;
    int32_t k = 0;
    const int s1Len = profile->s1Len;
    int32_t end_query = s1Len-1;
    int32_t end_ref = s2Len-1;
    const parasail_matrix_t *matrix = profile->matrix;
    const int32_t segWidth = 8; /* number of values in vector unit */
    const int32_t segLen = (s1Len + segWidth - 1) / segWidth;
    const int32_t offset = (s1Len - 1) % segLen;
    const int32_t position = (segWidth - 1) - (s1Len - 1) / segLen;
    __m128i* const restrict pvP  = (__m128i*)profile->profile16.score;
    __m128i* const restrict pvE  = parasail_memalign___m128i(16, segLen);
    int16_t* const restrict boundary = parasail_memalign_int16_t(16, s2Len+1);
    __m128i* const restrict pvHt = parasail_memalign___m128i(16, segLen);
    __m128i* const restrict pvH  = parasail_memalign___m128i(16, segLen);
    __m128i* const restrict pvGapper = parasail_memalign___m128i(16, segLen);
    __m128i vGapO = _mm_set1_epi16(open);
    __m128i vGapE = _mm_set1_epi16(gap);
    const int16_t NEG_LIMIT = (-open < matrix->min ?
        INT16_MIN + open : INT16_MIN - matrix->min) + 1;
    const int16_t POS_LIMIT = INT16_MAX - matrix->max - 1;
    __m128i vZero = _mm_setzero_si128();
    int16_t score = NEG_LIMIT;
    __m128i vNegLimit = _mm_set1_epi16(NEG_LIMIT);
    __m128i vPosLimit = _mm_set1_epi16(POS_LIMIT);
    __m128i vSaturationCheckMin = vPosLimit;
    __m128i vSaturationCheckMax = vNegLimit;
    __m128i vMaxH = vNegLimit;
    __m128i vPosMask = _mm_cmpeq_epi16(_mm_set1_epi16(position),
            _mm_set_epi16(0,1,2,3,4,5,6,7));
    __m128i vNegInfFront = vZero;
    __m128i vSegLenXgap;
    parasail_result_t *result = parasail_result_new_trace(segLen, s2Len, 16, sizeof(__m128i));
    __m128i vTIns  = _mm_set1_epi16(PARASAIL_INS);
    __m128i vTDel  = _mm_set1_epi16(PARASAIL_DEL);
    __m128i vTDiag = _mm_set1_epi16(PARASAIL_DIAG);
    __m128i vTDiagE = _mm_set1_epi16(PARASAIL_DIAG_E);
    __m128i vTInsE = _mm_set1_epi16(PARASAIL_INS_E);
    __m128i vTDiagF = _mm_set1_epi16(PARASAIL_DIAG_F);
    __m128i vTDelF = _mm_set1_epi16(PARASAIL_DEL_F);

    vNegInfFront = _mm_insert_epi16(vNegInfFront, NEG_LIMIT, 0);
    vSegLenXgap = _mm_add_epi16(vNegInfFront,
            _mm_slli_si128(_mm_set1_epi16(-segLen*gap), 2));

    /* initialize H and E */
    {
        int32_t index = 0;
        for (i=0; i<segLen; ++i) {
            int32_t segNum = 0;
            __m128i_16_t h;
            __m128i_16_t e;
            for (segNum=0; segNum<segWidth; ++segNum) {
                int64_t tmp = s1_beg ? 0 : (-open-gap*(segNum*segLen+i));
                h.v[segNum] = tmp < INT16_MIN ? INT16_MIN : tmp;
                tmp = tmp - open;
                e.v[segNum] = tmp < INT16_MIN ? INT16_MIN : tmp;
            }
            _mm_store_si128(&pvH[index], h.m);
            _mm_store_si128(&pvE[index], e.m);
            ++index;
        }
    }

    /* initialize uppder boundary */
    {
        boundary[0] = 0;
        for (i=1; i<=s2Len; ++i) {
            int64_t tmp = s2_beg ? 0 : (-open-gap*(i-1));
            boundary[i] = tmp < INT16_MIN ? INT16_MIN : tmp;
        }
    }

    {
        __m128i vGapper = _mm_sub_epi16(vZero,vGapO);
        for (i=segLen-1; i>=0; --i) {
            _mm_store_si128(pvGapper+i, vGapper);
            vGapper = _mm_sub_epi16(vGapper, vGapE);
        }
    }

    /* outer loop over database sequence */
    for (j=0; j<s2Len; ++j) {
        __m128i vE;
        __m128i vE_ext;
        __m128i vE_opn;
        __m128i vHt;
        __m128i vF;
        __m128i vF_ext;
        __m128i vF_opn;
        __m128i vH;
        __m128i vHp;
        __m128i *pvW;
        __m128i vW;
        __m128i case1;
        __m128i case2;
        __m128i vGapper;
        __m128i vT;
        __m128i vET;
        __m128i vFT;

        /* calculate E */
        /* calculate Ht */
        /* calculate F and H first pass */
        vHp = _mm_load_si128(pvH+(segLen-1));
        vHp = _mm_slli_si128(vHp, 2);
        vHp = _mm_insert_epi16(vHp, boundary[j], 0);
        pvW = pvP + matrix->mapper[(unsigned char)s2[j]]*segLen;
        vHt = _mm_sub_epi16(vNegLimit, pvGapper[0]);
        vF = vNegLimit;
        for (i=0; i<segLen; ++i) {
            vH = _mm_load_si128(pvH+i);
            vE = _mm_load_si128(pvE+i);
            vW = _mm_load_si128(pvW+i);
            vGapper = _mm_load_si128(pvGapper+i);
            vE_opn = _mm_sub_epi16(vH, vGapO);
            vE_ext = _mm_sub_epi16(vE, vGapE);
            case1 = _mm_cmpgt_epi16(vE_opn, vE_ext);
            vET = _mm_blendv_epi8_rpl(vTInsE, vTDiagE, case1);
            arr_store(result->trace->trace_table, vET, i, segLen, j);
            vE = _mm_max_epi16(vE_opn, vE_ext);
            vSaturationCheckMin = _mm_min_epi16(vSaturationCheckMin, vE);
            vGapper = _mm_add_epi16(vHt, vGapper);
            vF = _mm_max_epi16(vF, vGapper);
            vHp = _mm_add_epi16(vHp, vW);
            vHt = _mm_max_epi16(vE, vHp);
            _mm_store_si128(pvE+i, vE);
            _mm_store_si128(pvHt+i, vHt);
            _mm_store_si128(pvH+i, vHp);
            vHp = vH;
        }

        /* pseudo prefix scan on F and H */
        vHt = _mm_slli_si128(vHt, 2);
        vHt = _mm_insert_epi16(vHt, boundary[j+1], 0);
        vGapper = _mm_load_si128(pvGapper);
        vGapper = _mm_add_epi16(vHt, vGapper);
        vF = _mm_max_epi16(vF, vGapper);
        for (i=0; i<segWidth-2; ++i) {
            __m128i vFt = _mm_slli_si128(vF, 2);
            vFt = _mm_add_epi16(vFt, vSegLenXgap);
            vF = _mm_max_epi16(vF, vFt);
        }

        /* calculate final H */
        vF = _mm_slli_si128(vF, 2);
        vF = _mm_add_epi16(vF, vNegInfFront);
        vH = _mm_max_epi16(vF, vHt);
        for (i=0; i<segLen; ++i) {
            vET = arr_load(result->trace->trace_table, i, segLen, j);
            vHp = _mm_load_si128(pvH+i);
            vHt = _mm_load_si128(pvHt+i);
            vF_opn = _mm_sub_epi16(vH, vGapO);
            vF_ext = _mm_sub_epi16(vF, vGapE);
            vF = _mm_max_epi16(vF_opn, vF_ext);
            case1 = _mm_cmpgt_epi16(vF_opn, vF_ext);
            vFT = _mm_blendv_epi8_rpl(vTDelF, vTDiagF, case1);
            vH = _mm_max_epi16(vHt, vF);
            case1 = _mm_cmpeq_epi16(vH, vHp);
            case2 = _mm_cmpeq_epi16(vH, vF);
            vT = _mm_blendv_epi8_rpl(
                    _mm_blendv_epi8_rpl(vTIns, vTDel, case2),
                    vTDiag, case1);
            vT = _mm_or_si128(vT, vET);
            vT = _mm_or_si128(vT, vFT);
            arr_store(result->trace->trace_table, vT, i, segLen, j);
            _mm_store_si128(pvH+i, vH);
            vSaturationCheckMin = _mm_min_epi16(vSaturationCheckMin, vH);
            vSaturationCheckMin = _mm_min_epi16(vSaturationCheckMin, vF);
            vSaturationCheckMax = _mm_max_epi16(vSaturationCheckMax, vH);
        }

        /* extract vector containing last value from column */
        {
            __m128i vCompare;
            vH = _mm_load_si128(pvH + offset);
            vCompare = _mm_and_si128(vPosMask, _mm_cmpgt_epi16(vH, vMaxH));
            vMaxH = _mm_max_epi16(vH, vMaxH);
            if (_mm_movemask_epi8(vCompare)) {
                end_ref = j;
            }
        }
    } 

    /* max last value from all columns */
    if (s2_end)
    {
        for (k=0; k<position; ++k) {
            vMaxH = _mm_slli_si128(vMaxH, 2);
        }
        score = (int16_t) _mm_extract_epi16(vMaxH, 7);
        end_query = s1Len-1;
    }

    /* max of last column */
    if (s1_end)
    {
        /* Trace the alignment ending position on read. */
        int16_t *t = (int16_t*)pvH;
        int32_t column_len = segLen * segWidth;
        for (i = 0; i<column_len; ++i, ++t) {
            int32_t temp = i / segWidth + i % segWidth * segLen;
            if (temp >= s1Len) continue;
            if (*t > score) {
                score = *t;
                end_query = temp;
                end_ref = s2Len-1;
            }
            else if (*t == score && end_ref == s2Len-1 && temp < end_query) {
                end_query = temp;
            }
        }
    }

    if (!s1_end && !s2_end) {
        /* extract last value from the last column */
        {
            __m128i vH = _mm_load_si128(pvH + offset);
            for (k=0; k<position; ++k) {
                vH = _mm_slli_si128(vH, 2);
            }
            score = (int16_t) _mm_extract_epi16 (vH, 7);
            end_ref = s2Len - 1;
            end_query = s1Len - 1;
        }
    }

    if (_mm_movemask_epi8(_mm_or_si128(
            _mm_cmplt_epi16(vSaturationCheckMin, vNegLimit),
            _mm_cmpgt_epi16(vSaturationCheckMax, vPosLimit)))) {
        result->flag |= PARASAIL_FLAG_SATURATED;
        score = 0;
        end_query = 0;
        end_ref = 0;
    }

    result->score = score;
    result->end_query = end_query;
    result->end_ref = end_ref;
    result->flag |= PARASAIL_FLAG_SG | PARASAIL_FLAG_SCAN
        | PARASAIL_FLAG_TRACE
        | PARASAIL_FLAG_BITS_16 | PARASAIL_FLAG_LANES_8;
    result->flag |= s1_beg ? PARASAIL_FLAG_SG_S1_BEG : 0;
    result->flag |= s1_end ? PARASAIL_FLAG_SG_S1_END : 0;
    result->flag |= s2_beg ? PARASAIL_FLAG_SG_S2_BEG : 0;
    result->flag |= s2_end ? PARASAIL_FLAG_SG_S2_END : 0;

    parasail_free(pvGapper);
    parasail_free(pvH);
    parasail_free(pvHt);
    parasail_free(boundary);
    parasail_free(pvE);

    return result;
}

SG_IMPL_ALL
SG_IMPL_PROF_ALL

