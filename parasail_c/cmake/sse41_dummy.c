/**
 * @file
 *
 * @author jeff.daily@pnnl.gov
 *
 * Copyright (c) 2015 Battelle Memorial Institute.
 */
#ifdef PARASAIL_TABLE
#define ENAME parasail_sse41_dummy_table
#else
#ifdef PARASAIL_ROWCOL
#define ENAME parasail_sse41_dummy_rowcol
#else
#define ENAME parasail_sse41_dummy
#endif
#endif

extern int ENAME(void);

int ENAME()
{
    return 0;
}

