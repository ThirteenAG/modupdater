#ifndef TM_ZIP_H
#define TM_ZIP_H

#include <stdint.h>

typedef struct tm_zip_s
{
    uint32_t tm_sec;                /* seconds after the minute - [0,59] */
    uint32_t tm_min;                /* minutes after the hour - [0,59] */
    uint32_t tm_hour;               /* hours since midnight - [0,23] */
    uint32_t tm_mday;               /* day of the month - [1,31] */
    uint32_t tm_mon;                /* months since January - [0,11] */
    uint32_t tm_year;               /* years - [1980..2044] */
} tm_zip;

uint32_t zip64local_TmzDateToDosDate(const tm_zip* ptm);
void unz64local_DosDateToTmuDate(uint32_t ulDosDate, tm_zip* ptm);

#endif
