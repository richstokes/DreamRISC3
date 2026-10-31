/* mkdcdisc - Sega Dreamcast Disc Image Generator
 *
 * SPDX-License-Identifier: MIT
 * Copyright (C) 2022-2026 Simulant and contributors
 * (https://www.simulant.dev/) */

#ifndef _WINPATCH_LANGINFO_H
#define _WINPATCH_LANGINFO_H

#include <stdio.h>
#include <windows.h>
#include <locale.h>

// Common langinfo constants
#define CODESET     49156
#define D_T_FMT     49157
#define D_FMT       49158
#define T_FMT       49159
#define T_FMT_AMPM  49160
#define AM_STR      49161
#define PM_STR      49162

// Day names
#define DAY_1       49163  // Sunday
#define DAY_2       49164  // Monday
#define DAY_3       49165  // Tuesday
#define DAY_4       49166  // Wednesday
#define DAY_5       49167  // Thursday
#define DAY_6       49168  // Friday
#define DAY_7       49169  // Saturday

// Abbreviated day names
#define ABDAY_1     49170
#define ABDAY_2     49171
#define ABDAY_3     49172
#define ABDAY_4     49173
#define ABDAY_5     49174
#define ABDAY_6     49175
#define ABDAY_7     49176

// Month names
#define MON_1       49177  // January
#define MON_2       49178
#define MON_3       49179
#define MON_4       49180
#define MON_5       49181
#define MON_6       49182
#define MON_7       49183
#define MON_8       49184
#define MON_9       49185
#define MON_10      49186
#define MON_11      49187
#define MON_12      49188

// Abbreviated month names
#define ABMON_1     49189
#define ABMON_2     49190
#define ABMON_3     49191
#define ABMON_4     49192
#define ABMON_5     49193
#define ABMON_6     49194
#define ABMON_7     49195
#define ABMON_8     49196
#define ABMON_9     49197
#define ABMON_10    49198
#define ABMON_11    49199
#define ABMON_12    49200

// Radix character
#define RADIXCHAR   49201
#define THOUSEP     49202

// Currency
#define CRNCYSTR    49203

// Stub function for nl_langinfo
static inline
char* nl_langinfo(int item) {
    static char codeset_buf[32];
    
    switch (item) {
        case CODESET:
            // Try to get Windows codepage
            {
                UINT cp = GetACP();
                if (cp == 1252) return "ISO-8859-1";
                if (cp == 65001) return "UTF-8";
                if (cp == 1251) return "windows-1251";
                sprintf(codeset_buf, "CP%u", cp);
                return codeset_buf;
            }
            
        case D_T_FMT:
            return "%a %b %e %H:%M:%S %Y";
            
        case D_FMT:
            return "%m/%d/%Y";
            
        case T_FMT:
            return "%H:%M:%S";
            
        case T_FMT_AMPM:
            return "%I:%M:%S %p";
            
        case AM_STR:
            return "AM";
            
        case PM_STR:
            return "PM";
            
        case RADIXCHAR:
            return ".";
            
        case THOUSEP:
            return ",";
            
        // Day names
        case DAY_1: return "Sunday";
        case DAY_2: return "Monday";
        case DAY_3: return "Tuesday";
        case DAY_4: return "Wednesday";
        case DAY_5: return "Thursday";
        case DAY_6: return "Friday";
        case DAY_7: return "Saturday";
        
        // Abbreviated day names
        case ABDAY_1: return "Sun";
        case ABDAY_2: return "Mon";
        case ABDAY_3: return "Tue";
        case ABDAY_4: return "Wed";
        case ABDAY_5: return "Thu";
        case ABDAY_6: return "Fri";
        case ABDAY_7: return "Sat";
        
        // Month names
        case MON_1: return "January";
        case MON_2: return "February";
        case MON_3: return "March";
        case MON_4: return "April";
        case MON_5: return "May";
        case MON_6: return "June";
        case MON_7: return "July";
        case MON_8: return "August";
        case MON_9: return "September";
        case MON_10: return "October";
        case MON_11: return "November";
        case MON_12: return "December";
        
        // Abbreviated month names
        case ABMON_1: return "Jan";
        case ABMON_2: return "Feb";
        case ABMON_3: return "Mar";
        case ABMON_4: return "Apr";
        case ABMON_5: return "May";
        case ABMON_6: return "Jun";
        case ABMON_7: return "Jul";
        case ABMON_8: return "Aug";
        case ABMON_9: return "Sep";
        case ABMON_10: return "Oct";
        case ABMON_11: return "Nov";
        case ABMON_12: return "Dec";
        
        default:
            return "";
    }
}

#endif /* _WINPATCH_LANGINFO_H */
