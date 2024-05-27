#include "tmzip.h"
#include <string.h>

uint32_t zip64local_TmzDateToDosDate(const tm_zip* ptm)
{
    uint32_t year;
#define zip64local_in_range(min, max, value) ((min) <= (value) && (value) <= (max))
    /* Years supported:
       * [00, 79] (assumed to be between 2000 and 2079)
       * [80, 207] (assumed to be between 1980 and 2107, typical output of old
         software that does 'year-1900' to get a double digit year)
       * [1980, 2107]
       Due to the date format limitations, only years between 1980 and 2107 can be stored.
    */
    if (!(zip64local_in_range(1980, 2107, ptm->tm_year) || zip64local_in_range(0, 207, ptm->tm_year)) ||
        !zip64local_in_range(0, 11, ptm->tm_mon) ||
        !zip64local_in_range(1, 31, ptm->tm_mday) ||
        !zip64local_in_range(0, 23, ptm->tm_hour) ||
        !zip64local_in_range(0, 59, ptm->tm_min) ||
        !zip64local_in_range(0, 59, ptm->tm_sec))
      return 0;
#undef zip64local_in_range

    year = (uint32_t)ptm->tm_year;
    if (year >= 1980) /* range [1980, 2107] */
        year -= 1980;
    else if (year >= 80) /* range [80, 99] */
        year -= 80;
    else /* range [00, 79] */
        year += 20;

    return (uint32_t)(((ptm->tm_mday) + (32 * (ptm->tm_mon+1)) + (512 * year)) << 16) |
        ((ptm->tm_sec / 2) + (32 * ptm->tm_min) + (2048 * (uint32_t)ptm->tm_hour));
}

/* Translate date/time from Dos format to tm_zip (readable more easily) */
void unz64local_DosDateToTmuDate(uint32_t ulDosDate, tm_zip* ptm)
{
    uint32_t uDate = (uint32_t)(ulDosDate>>16);

    ptm->tm_mday = (uint32_t)(uDate&0x1f);
    ptm->tm_mon  = (uint32_t)((((uDate)&0x1E0)/0x20)-1);
    ptm->tm_year = (uint32_t)(((uDate&0x0FE00)/0x0200)+1980);
    ptm->tm_hour = (uint32_t)((ulDosDate &0xF800)/0x800);
    ptm->tm_min  = (uint32_t)((ulDosDate&0x7E0)/0x20);
    ptm->tm_sec  = (uint32_t)(2*(ulDosDate&0x1f));

#define unz64local_in_range(min, max, value) ((min) <= (value) && (value) <= (max))
    if (!unz64local_in_range(0, 11, ptm->tm_mon) ||
        !unz64local_in_range(1, 31, ptm->tm_mday) ||
        !unz64local_in_range(0, 23, ptm->tm_hour) ||
        !unz64local_in_range(0, 59, ptm->tm_min) ||
        !unz64local_in_range(0, 59, ptm->tm_sec))
      /* Invalid date stored, so don't return it. */
      memset(ptm, 0, sizeof(tm_zip));
#undef unz64local_in_range
}
