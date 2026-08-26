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
#include <string.h>
#include <time.h>

#include "ptypes.h"
#include "ptime.h"      // nowstring() is defined in this module


namespace ptypes {


const int strrecsize = sizeof(_strrec);


static void stringoverflow() 
{
    fatal(CRIT_FIRST + 21, "String overflow");
}


void string::idxerror()
{
    fatal(CRIT_FIRST + 20, "String index overflow");
}


int stralloc;

char   emptystrbuf[strrecsize + 4];
char*  emptystr = emptystrbuf + strrecsize;


string nullstring;


inline int quantize(int numchars) 
{
	return memquantize(numchars + 1 + strrecsize);
}


void string::_alloc(int numchars) 
{
    if (numchars <= 0)
        stringoverflow();
    size_t a = quantize(numchars);
#ifdef DEBUG
    stralloc += a;
#endif
    data = (char*)(memalloc(a)) + strrecsize;
    STR_LENGTH(data) = numchars;
    STR_REFCOUNT(data) = 1;
    data[numchars] = 0;
}


void string::_realloc(int numchars) 
{
    if (numchars <= 0 || STR_LENGTH(data) <= 0)
        stringoverflow();
    int a = quantize(numchars);
    int b = quantize(STR_LENGTH(data));
    if (a != b)
    {
#ifdef DEBUG
        stralloc += a - b;
#endif
        data = (char*)(memrealloc(data - strrecsize, a)) + strrecsize;
    }
    STR_LENGTH(data) = numchars;
    data[numchars] = 0;
}


inline void _freestrbuf(char* data)
{
#ifdef DEBUG
    stralloc -= quantize(STR_LENGTH(data));
#endif
    memfree((char*)(STR_BASE(data)));
}


void string::_free() 
{
    _freestrbuf(data);
    data = emptystr;
}


void string::initialize(const char* sc, int initlen) 
{
    if (initlen <= 0 || sc == nil)
        data = emptystr; 
    else 
    {
        _alloc(initlen);
        memmove(data, sc, initlen);
    }
}


void string::initialize(const char* sc) 
{
    initialize(sc, hstrlen(sc));
}


void string::initialize(char c) 
{
    _alloc(1);
    data[0] = c;
}


void string::initialize(const string& s)
{
    data = s.data;
#ifdef PTYPES_ST
    STR_REFCOUNT(data)++;
#else
    pincrement(&STR_REFCOUNT(data));
#endif
}


void string::finalize() 
{
    if (STR_LENGTH(data) != 0)
    {

#ifdef PTYPES_ST
        if (--STR_REFCOUNT(data) == 0)
#else
        if (pdecrement(&STR_REFCOUNT(data)) == 0)
#endif
            _freestrbuf(data);

        data = emptystr;
    }
}


char* ptdecl unique(string& s)
{
    if (STR_LENGTH(s.data) > 0 && STR_REFCOUNT(s.data) > 1)
    {
        char* odata = s.data;
        s._alloc(STR_LENGTH(s.data));
        memcpy(s.data, odata, STR_LENGTH(s.data));
#ifdef PTYPES_ST
        STR_REFCOUNT(odata)--;
#else
        if (pdecrement(&STR_REFCOUNT(odata)) == 0)
            _freestrbuf(odata);
#endif
    }
    return s.data;
}


char* ptdecl setlength(string& s, int newlen)
{
    if (newlen < 0)
        return nil;

    int curlen = STR_LENGTH(s.data);

    // if becoming empty
    if (newlen == 0)
        s.finalize();

    // if otherwise s was empty before
    else if (curlen == 0)
        s._alloc(newlen);

    // if length is not changing, return a unique string
    else if (newlen == curlen)
        unique(s);

    // non-unique reallocation
    else if (STR_REFCOUNT(s.data) > 1)
    {
        char* odata = s.data;
        s._alloc(newlen);
        memcpy(s.data, odata, imin(curlen, newlen));
#ifdef PTYPES_ST
        STR_REFCOUNT(odata)--;
#else
        if (pdecrement(&STR_REFCOUNT(odata)) == 0)
            _freestrbuf(odata);
#endif
    }

    // unique reallocation
    else
        s._realloc(newlen);

    return s.data;
}


void string::assign(const char* sc, int initlen) 
{
    if (STR_LENGTH(data) > 0 && initlen > 0 && STR_REFCOUNT(data) == 1)
    {
        // reuse data buffer if unique
        _realloc(initlen);
        memmove(data, sc, initlen);
    }
    else
    {
        finalize();
        if (initlen == 1)
            initialize(sc[0]);
        else if (initlen > 1)
            initialize(sc, initlen);
    }
}


void string::assign(const char* sc) 
{
    assign(sc, hstrlen(sc));
}


void string::assign(char c) 
{
    assign(&c, 1);
}


void string::assign(const string& s) 
{
    if (data != s.data)
    {
        finalize();
        initialize(s);
    }
}


string ptdecl dup(const string& s)
{
    // dup() only reads the data pointer so it is thread-safe
    return string(s.data);
}


string ptdecl nowstring(const char* fmt, bool utc)
{
    char buf[128];
    time_t longtime;
    time(&longtime);

#if defined(PTYPES_ST) || defined(WIN32)
    tm* t;
    if (utc)
        t = gmtime(&longtime);
    else
        t = localtime(&longtime);
    int r = strftime(buf, sizeof(buf), fmt, t);
#else
    tm t;
    if (utc)
        gmtime_r(&longtime, &t);
    else
        localtime_r(&longtime, &t);
    int r = strftime(buf, sizeof(buf), fmt, &t);
#endif

    buf[r] = 0;
    return string(buf);
}


}
