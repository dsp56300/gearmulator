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

#include <string.h>

#include "ptypes.h"


namespace ptypes {


static const char* _itobase(large value, char* buf, int base, int& len, bool _signed)
{
    // internal conversion routine: converts the value to a string 
    // at the end of the buffer and returns a pointer to the first
    // character. this is to get rid of copying the string to the 
    // beginning of the buffer, since finally the string is supposed 
    // to be copied to a dynamic string in itostring(). the buffer 
    // must be at least 65 bytes long.

    static char digits[65] = 
        "./0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";

    char* pdigits;
    if (base > 36)
	pdigits = digits;       // start from '.'
    else
	pdigits = digits + 2;   // start from '0'
    
    int i = 64;
    buf[i] = 0;

    bool neg = false;
    ularge v = value;
    if (_signed && base == 10 && value < 0)
    {
        v = -value;
        // since we can't handle the lowest signed 64-bit value, we just
        // return a built-in string.
        if (large(v) < 0)   // the LLONG_MIN negated results in the same value
        {
            len = 20;
            return "-9223372036854775808";
        }
        neg = true;
    }

    do
    {
        buf[--i] = pdigits[uint(v % base)];
        v /= base;
    } while (v > 0);

    if (neg)
        buf[--i] = '-';

    len = 64 - i;
    return buf + i;
}


static void _itobase2(string& result, large value, int base, int width, char padchar, bool _signed)
{
    if (base < 2 || base > 64)
    {
        clear(result);
        return;
    }

    char buf[65];   // the longest possible string is when base=2
    int reslen;
    const char* p = _itobase(value, buf, base, reslen, _signed);

    if (width > reslen)
    {
        if (padchar == 0)
        {
            // default pad char
            if (base == 10)
                padchar = ' ';
            else if (base > 36)
                padchar = '.';
            else
                padchar = '0';
        }

        setlength(result, width);
        bool neg = *p == '-';
        width -= reslen;
        memset(pchar(pconst(result)) + neg, padchar, width);
        memcpy(pchar(pconst(result)) + width + neg, p + neg, reslen - neg);
        if (neg)
            *pchar(pconst(result)) = '-';
    }
    else 
        assign(result, p, reslen);
}


string ptdecl itostring(large value, int base, int width, char padchar) 
{
    string result;
    _itobase2(result, value, base, width, padchar, true);
    return result;
}


string ptdecl itostring(ularge value, int base, int width, char padchar) 
{
    string result;
    _itobase2(result, value, base, width, padchar, false);
    return result;
}


string ptdecl itostring(int value, int base, int width, char padchar) 
{
    string result;
    _itobase2(result, large(value), base, width, padchar, true);
    return result;
}


string ptdecl itostring(uint value, int base, int width, char padchar) 
{
    string result;
    _itobase2(result, ularge(value), base, width, padchar, false);
    return result;
}


string ptdecl itostring(large v)   { return itostring(v, 10, 0, ' '); }
string ptdecl itostring(ularge v)  { return itostring(v, 10, 0, ' '); }
string ptdecl itostring(int v)     { return itostring(large(v), 10, 0, ' '); }
string ptdecl itostring(uint v)    { return itostring(ularge(v), 10, 0, ' '); }


}

