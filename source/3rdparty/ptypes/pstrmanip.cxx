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
#include <limits.h>  // for INT_MAX

#include "ptypes.h"


namespace ptypes {


void string::initialize(const char* s1, int len1, const char* s2, int len2)
{
    if (len1 <= 0)
        initialize(s2, len2);
    else if (len2 <= 0)
        initialize(s1, len1);
    else
    {
        _alloc(len1 + len2);
        memcpy(data, s1, len1);
        memcpy(data + len1, s2, len2);
    }
}


void ptdecl concat(string& s, const char* sc, int catlen)
{
    if (length(s) == 0)
        s.assign(sc, catlen);
    else if (catlen > 0) 
    {
        int oldlen = length(s);
        
        // we must check this before calling setlength(), since
        // the buffer pointer may be changed during reallocation
        if (s.data == sc)
        {
            setlength(s, oldlen + catlen);
            memmove(s.data + oldlen, s.data, catlen);
        }
        else
        {
            setlength(s, oldlen + catlen);
            memmove(s.data + oldlen, sc, catlen);
        }
    }
}


void ptdecl concat(string& s, const char* sc)
{
    concat(s, sc, hstrlen(sc));
}


void ptdecl concat(string& s, char c)
{
    if (length(s) == 0)
        s.assign(c);
    else 
    {
        setlength(s, length(s) + 1);
        s.data[length(s) - 1] = c;
    }
}


void ptdecl concat(string& s, const string& s1)
{
    if (length(s) == 0)
        s = s1;
    else if (length(s1) > 0)
        concat(s, s1.data, length(s1));
}


bool ptdecl contains(const char* s1, int s1len, const string& s, int at)
{
    return (s1len >= 0) && (at >= 0) && (at + s1len <= length(s))
        && (s1len == 0 || memcmp(s.data + at, s1, s1len) == 0);
}


bool ptdecl contains(const char* s1, const string& s, int at)
{
    return contains(s1, hstrlen(s1), s, at);
}


bool ptdecl contains(char s1, const string& s, int at)
{
    return (at >= 0) && (at < length(s)) && (s.data[at] == s1);
}


bool ptdecl contains(const string& s1, const string& s, int at)
{
    return contains(s1.data, length(s1), s, at);
}


string string::operator+ (const char* sc) const  
{
    if (length(*this) == 0)
        return string(sc);
    else
        return string(data, length(*this), sc, hstrlen(sc));
}


string string::operator+ (char c) const
{ 
    if (length(*this) == 0)
        return string(c);
    else
        return string(data, length(*this), &c, 1);
}


string string::operator+ (const string& s) const
{
    if (length(*this) == 0)
        return s;
    else if (length(s) == 0)
        return *this;
    else
        return string(data, length(*this), s.data, length(s));
}


string ptdecl operator+ (const char* sc, const string& s)
{
    if (length(s) == 0)
        return string(sc);
    else
        return string(sc, hstrlen(sc), s.data, length(s));
}


string ptdecl operator+ (char c, const string& s)
{
    if (length(s) == 0)
        return string(c);
    else
        return string(&c, 1, s.data, length(s));
}


bool string::operator== (const string& s) const 
{
    return (length(*this) == length(s))
        && ((length(*this) == 0) || (memcmp(data, s.data, length(*this)) == 0));
}


bool string::operator== (char c) const 
{
    return (length(*this) == 1) && (data[0] == c);
}


string ptdecl copy(const string& s, int from, int cnt)
{
    string t;
    if (length(s) > 0 && from >= 0 && from < length(s)) 
    {
        int l = imin(cnt, length(s) - from);
        if (from == 0 && l == length(s))
            t = s;
        else if (l > 0) 
        {
            t._alloc(l);
            memmove(t.data, s.data + from, l);
            t.data[l] = 0;
        }
    }
    return t;
}


string ptdecl copy(const string& s, int from)
{
    return copy(s, from, INT_MAX);
}


void ptdecl ins(const char* s1, int s1len, string& s, int at)
{
    int curlen = length(s);
    if (s1len > 0 && at >= 0 && at <= curlen) 
    {
        if (curlen == 0)
            s.assign(s1, s1len);
        else 
        {
            setlength(s, curlen + s1len);
            int t = length(s) - at - s1len;
            char* p = s.data + at;
            if (t > 0) 
                memmove(p + s1len, p, t);
            memmove(p, s1, s1len);
        }
    }
}


void ptdecl ins(const char* sc, string& s, int at)
{
    ins(sc, hstrlen(sc), s, at);
}


void ptdecl ins(char c, string& s, int at)
{
    ins(&c, 1, s, at);
}


void ptdecl ins(const string& s1, string& s, int at)
{
    ins(s1.data, length(s1), s, at);
}


void ptdecl del(string& s, int from, int cnt)
{
    int l = length(s);
    int d = l - from;
    if (from >= 0 && d > 0 && cnt > 0) 
    {
        if (cnt < d)
        {
            unique(s);
            memmove(s.data + from, s.data + from + cnt, d - cnt);
        }
        else
            cnt = d;
        setlength(s, l - cnt);
    }
}


void ptdecl del(string& s, int from)
{
    setlength(s, from);
}


int ptdecl pos(const char* sc, const string& s)
{
    const char* t = (char*)strstr(s.data, sc);
    return (t == NULL ? (-1) : int(t - s.data));
}


int ptdecl pos(char c, const string& s)
{
    const char* t = (char*)strchr(s.data, c);
    return (t == NULL ? (-1) : int(t - s.data));
}


int ptdecl rpos(char c, const string& s)
{
    const char* t = (char*)strrchr(s.data, c);
    return (t == NULL ? (-1) : int(t - s.data));
}


}
