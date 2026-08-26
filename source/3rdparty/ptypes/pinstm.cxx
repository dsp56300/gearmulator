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

#include <errno.h>
#include <string.h>
#include <limits.h>

#ifdef WIN32
#  include <windows.h>
#else
#  include <unistd.h>
#endif

#include "pstreams.h"


namespace ptypes {


instm::instm(int ibufsize): iobase(ibufsize) 
{
}


instm::~instm() 
{
}


int instm::classid()
{
    return CLASS_INSTM;
}


int instm::dorawread(char* buf, int count)
{
    if (handle == invhandle)
	return -1;
#ifdef WIN32
    unsigned long ret;
    if (!ReadFile(HANDLE(handle), buf, count, &ret, nil)) 
#else
    int ret;
    if ((ret = ::read(handle, buf, count)) < 0)
#endif
    {
        int e = uerrno();
        if (e == EPIPE)
            ret = 0;
        else
            error(e, "Couldn't read");
    }
    return ret;
}


int instm::rawread(char* buf, int count) 
{
    requireactive();
    try 
    {
        int ret = dorawread(buf, count);
        if (ret <= 0) {
            ret = 0;
            eof = true;
            chstat(IO_EOF);
        }
        else 
        {
            abspos += ret;
            chstat(IO_READING);
        }
        return ret;
    }
    catch (estream*) 
    {
        eof = true;
        chstat(IO_EOF);
        throw;
    }
}


large instm::tellx()
{
    return abspos - bufend + bufpos;
}


void instm::bufvalidate() 
{
    requirebuf();
    bufclear();
    bufend = rawread(bufdata, bufsize);
}


large instm::seekx(large newpos, ioseekmode mode) 
{
    if (bufdata != 0 && mode != IO_END) 
    {
        if (mode == IO_CURRENT)
        {
            newpos += tellx();
            mode = IO_BEGIN;
        }

        // see if it is possible to seek within the buffer
        large newbufpos = newpos - (abspos - bufend);
        if (newbufpos >= 0 && newbufpos <= bufend) 
        {
            bufpos = (int)newbufpos;
            eof = false;
            return tellx();
        }
    }

    // if IO_END or if not possible to seek within the buffer
    return iobase::seekx(newpos, mode);
}


bool instm::get_eof() 
{
    if (!eof && bufdata != 0 && bufpos >= bufend)
        bufvalidate();
    return eof;
}


int instm::get_dataavail()
{
    get_eof();
    return bufend - bufpos;
}


char instm::preview() 
{
    if (!eof && bufpos >= bufend)
        bufvalidate();
    if (eof)
        return eofchar;
    return bufdata[bufpos];
}


void instm::putback()
{
    requireactive();
    if (bufpos == 0)
        fatal(CRIT_FIRST + 14, "putback() failed");
    bufpos--;
    eof = false;
}


bool instm::get_eol() 
{
    char c = preview();
    return (eof || c == 10 || c == 13);
}


void instm::skipeol() 
{
    switch (preview()) 
    {
    case 10: 
        get(); 
        break;
    case 13:
        get();
        if (preview() == 10)
            get();
        break;
    }
}


char instm::get() 
{
    char ret = preview();
    if (!eof)
        bufpos++;
    return ret;
}


string instm::token(const cset& chars, int limit) 
{
    requirebuf();
    string ret;
    while (!get_eof()) 
    {
        char* b = bufdata + bufpos;
        char* e = bufdata + bufend;
        char* p = b;
        while (p < e && (*p & chars))
            p++;
        int n = p - b;
        limit -= n;
        if (limit < 0)
        {
            bufpos += n + limit;
            error(ERANGE, "Token too long");
        }
        concat(ret, b, n);
        bufpos += n;
        if (p < e)
            break;
    }
    return ret;
}


string instm::token(const cset& chars) 
{
    return token(chars, INT_MAX);
}


static cset linechars = cset("*") - cset("~0a~0d");


string instm::line(int limit) 
{
    string ret = token(linechars, limit);
    skipeol();
    return ret;
}


string instm::line()
{
    string ret = token(linechars, INT_MAX);
    skipeol();
    return ret;
}


int instm::token(const cset& chars, char* buf, int count) 
{
    requirebuf();
    int ret = 0;
    while (count > 0 && !get_eof()) 
    {
        char* b = bufdata + bufpos;
        char* e = b + imin(count, bufend - bufpos);
        char* p = b;
        while (p < e && (*p & chars))
            p++;
        int n = p - b;
        memcpy(buf, b, n);
        buf += n;
        ret += n;
        count -= n;
        bufpos += n;
        if (p < e)
            break;
    }
    return ret;
}


int instm::line(char* buf, int size, bool eateol) 
{
    int ret = token(linechars, buf, size);
    if (eateol)
        skipeol();
    return ret;
}


int instm::read(void* buf, int count) 
{
    int ret = 0;
    if (bufdata == 0) 
        ret = rawread(pchar(buf), count);
    else 
    {
        while (count > 0 && !get_eof()) 
        {
            int n = imin(count, bufend - bufpos);
            memcpy(buf, bufdata + bufpos, n);
            buf = pchar(buf) + n;
            ret += n;
            count -= n;
            bufpos += n;
        }
    }
    return ret;
}


int instm::skip(int count) 
{
    int ret = 0;
    requirebuf();
    while (count > 0 && !get_eof()) 
    {
        int n = imin(count, bufend - bufpos);
        ret += n;
        count -= n;
        bufpos += n;
    }
    return ret;
}


int instm::skiptoken(const cset& chars) 
{
    int ret = 0;
    requirebuf();
    while (!get_eof())
    {
        char* b = bufdata + bufpos;
        char* e = bufdata + bufend;
        char* p = b;
        while (p < e && (*p & chars))
            p++;
        int n = p - b;
        bufpos += n;
        ret += n;
        if (p < e)
            break;
    }
    return ret;
}


void instm::skipline(bool eateol) 
{
    if (!get_eol())
        skiptoken(linechars);
    if (eateol)
        skipeol();
}


}
