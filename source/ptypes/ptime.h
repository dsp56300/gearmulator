/*
 *
 *  C++ Portable Types Library (PTypes)
 *  Version 2.1.1  Released 27-Jun-2007
 *
 *  Copyright (C) 2001-2007 Hovik Melikyan
 *
 *  http://www.melikyan.com/ptypes/
 *
 */

#ifndef __PTIME_H__
#define __PTIME_H__

#ifndef __PPORT_H__
#include "pport.h"
#endif

#ifndef __PTYPES_H__
#include "ptypes.h"
#endif


#include <time.h>


namespace ptypes {

// datetime type: 64-bit, number of milliseconds since midnight 01/01/0001
typedef large datetime;

#define invdatetime LLCONST(-1)

#define _msecsmax 86400000                    // number of milliseconds in one day
#define _daysmax  3652059                     // number of days between 01/01/0001 and 12/31/9999
#define _datetimemax LLCONST(315537897600000) // max. allowed number for datetime type
#define _unixepoch LLCONST(62135596800000)    // difference between time_t and datetime in milliseconds


// datetime general utilities
inline int days(datetime d)            { return int(d / _msecsmax); }
inline int msecs(datetime d)           { return int(d % _msecsmax); }

ptpublic datetime ptdecl mkdt(int days, int msecs);
ptpublic bool     ptdecl isvalid(datetime);
ptpublic datetime ptdecl now(bool utc = true);
ptpublic void     ptdecl tzupdate();
ptpublic int      ptdecl tzoffset();
ptpublic string   ptdecl dttostring(datetime, const char* fmt);
ptpublic string   ptdecl nowstring(const char* fmt, bool utc = true);
ptpublic datetime ptdecl utodatetime(time_t u);
ptpublic struct tm* ptdecl dttotm(datetime dt, struct tm& t);

// date/calendar manipulation
ptpublic bool     ptdecl isleapyear(int year);
ptpublic int      ptdecl daysinmonth(int year, int month);
ptpublic int      ptdecl daysinyear(int year, int month);
ptpublic int      ptdecl dayofweek(datetime);
ptpublic bool     ptdecl isdatevalid(int year, int month, int day);
ptpublic datetime ptdecl encodedate(int year, int month, int day);
ptpublic bool     ptdecl decodedate(datetime, int& year, int& month, int& day);

// time manipulation
ptpublic bool     ptdecl istimevalid(int hour, int min, int sec, int msec = 0);
ptpublic datetime ptdecl encodetime(int hour, int min, int sec, int msec = 0);
ptpublic bool     ptdecl decodetime(datetime, int& hour, int& min, int& sec, int& msec);
ptpublic bool     ptdecl decodetime(datetime, int& hour, int& min, int& sec);


}

#endif // __PTIME_H__
