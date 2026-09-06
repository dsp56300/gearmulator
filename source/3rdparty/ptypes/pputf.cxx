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

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>

#include "ptypes.h"
#include "pstreams.h"
#include "pinet.h"      // for ipaddress type
#include "ptime.h"      // for dttotm()

#ifndef PTYPES_ST
#include "pasync.h"     // for mutex locking in logfile::vputf()
#endif


namespace ptypes {


// %t and %T formats
const char* const shorttimefmt = "%d-%b-%Y %X";
const char* const longtimefmt = "%a %b %d %X %Y";


static cset fmtopts = " #+~-0-9.";


enum fmt_type_t
{
    FMT_NONE,
    FMT_CHAR,
    FMT_SHORT,
    FMT_INT,
    FMT_LONG,
    FMT_LARGE,
    FMT_STR,
    FMT_PTR,
    FMT_DOUBLE,
    FMT_LONG_DOUBLE,
    FMT_IPADDR,
    FMT_TIME,
    FMT_LONGTIME
};


void outstm::vputf(const char* fmt, va_list va)
{
    const char* p = fmt;
    while (*p != 0)
    {
        // write out raw data between format specifiers
        const char* e = strchr(p, '%');
        if (e == 0)
            e = p + strlen(p);
        if (e > p)
            write(p, e - p);

        if (*e != '%')
            break;

        e++;
        if (*e == '%')
        {
            // write out a single '%'
            put('%');
            p = e + 1;
            continue;
        }

        // build a temporary buffer for the conversion specification
        char fbuf[128];
        fbuf[0] = '%';
        char* f = fbuf + 1;
        bool modif = false;

        // formatting flags and width specifiers
        while (*e & fmtopts && uint(f - fbuf) < sizeof(fbuf) - 5)
        {
            *f++ = *e++;
            modif = true;
        }

        // prefixes
        fmt_type_t fmt_type = FMT_NONE;
        switch(*e)
        {
        case 'h': 
            fmt_type = FMT_SHORT; 
            *f++ = *e++;
            break;
        case 'L': 
            fmt_type = FMT_LONG_DOUBLE; 
            *f++ = *e++;
            break;
        case 'l':
            e++;
            if (*e == 'l')
            {
#if defined(_MSC_VER) || defined(__BORLANDC__)
                *f++ = 'I';
                *f++ = '6';
                *f++ = '4';
#else
                *f++ = 'l';
                *f++ = 'l';
#endif
                e++;
                fmt_type = FMT_LARGE;
            }
            else
            {
                *f++ = 'l';
                fmt_type = FMT_LONG;
            }
            break;
        }

        // format specifier
        switch(*e)
        {
        case 'c':
            fmt_type = FMT_CHAR;
            *f++ = *e++;
            break;
        case 'd':
        case 'i':
        case 'o':
        case 'u':
        case 'x':
        case 'X':
            if (fmt_type < FMT_SHORT || fmt_type > FMT_LARGE)
                fmt_type = FMT_INT;
            *f++ = *e++;
            break;
        case 'e':
        case 'E':
        case 'f':
        case 'g':
        case 'G':
            if (fmt_type != FMT_LONG_DOUBLE)
                fmt_type = FMT_DOUBLE;
            *f++ = *e++;
            break;
        case 's':
            fmt_type = FMT_STR;
            *f++ = *e++;
            break;
        case 'p':
            fmt_type = FMT_PTR;
            *f++ = *e++;
            break;
        case 'a':
            fmt_type = FMT_IPADDR;
            *f++ = *e++;
            break;
        case 't':
            fmt_type = FMT_TIME;
            *f++ = *e++;
            break;
        case 'T':
            fmt_type = FMT_LONGTIME;
            *f++ = *e++;
            break;
        }

        if (fmt_type == FMT_NONE)
            break;

        *f = 0;

        // some formatters are processed here 'manually',
        // while others are passed to snprintf
        char buf[4096];
        int s = 0;
        switch(fmt_type)
        {
        case FMT_NONE: 
            break; // to avoid compiler warning
        case FMT_CHAR:
            if (modif)
                s = snprintf(buf, sizeof(buf), fbuf, va_arg(va,int)); 
            else
                put(char(va_arg(va,int)));
            break;
        case FMT_SHORT:
            s = snprintf(buf, sizeof(buf), fbuf, va_arg(va,int)); 
            break;
        case FMT_INT:
            s = snprintf(buf, sizeof(buf), fbuf, va_arg(va,int)); 
            break;
        case FMT_LONG:
            s = snprintf(buf, sizeof(buf), fbuf, va_arg(va,long)); 
            break;
        case FMT_LARGE:
            s = snprintf(buf, sizeof(buf), fbuf, va_arg(va,large)); 
            break;
        case FMT_STR:
            if (modif)
                s = snprintf(buf, sizeof(buf), fbuf, va_arg(va,char*));
            else
                put(va_arg(va,const char*));
            break;
        case FMT_PTR:
            s = snprintf(buf, sizeof(buf), fbuf, va_arg(va,void*)); 
            break;
        case FMT_DOUBLE:
            s = snprintf(buf, sizeof(buf), fbuf, va_arg(va,double)); 
            break;
        case FMT_LONG_DOUBLE:
            s = snprintf(buf, sizeof(buf), fbuf, va_arg(va,long double)); 
            break;

        case FMT_IPADDR:
            {
                ipaddress ip = va_arg(va,long);
                s = snprintf(buf, sizeof(buf), "%d.%d.%d.%d", 
                    uint(ip[0]), uint(ip[1]), uint(ip[2]), uint(ip[3]));
            }
            break;

        case FMT_TIME:
        case FMT_LONGTIME:
            {
                const char* const fmt = (fmt_type == FMT_TIME) ? shorttimefmt : longtimefmt;
                struct tm t;
                datetime dt = va_arg(va,large);
                if (dt < 0)
                    dt = 0;
                s = strftime(buf, sizeof(buf), fmt, dttotm(dt, t));
            }
            break;
        }
        if (s > 0)
            write(buf, s);
        
        p = e;
    }
}


void outstm::putf(const char* fmt, ...)
{
    va_list va;
    va_start(va, fmt);
    vputf(fmt, va);
    va_end(va);
}


void fdxstm::putf(const char* fmt, ...)
{
    va_list va;
    va_start(va, fmt);
    out.vputf(fmt, va);
    va_end(va);
}


void logfile::vputf(const char* fmt, va_list va)
{
#ifndef PTYPES_ST
    scopelock sl(lock);
#endif
    outfile::vputf(fmt, va);
}


void logfile::putf(const char* fmt, ...)
{
    va_list va;
    va_start(va, fmt);
    vputf(fmt, va);
    va_end(va);
}


}
