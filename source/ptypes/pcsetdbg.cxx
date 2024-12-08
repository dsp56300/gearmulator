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

#include "ptypes.h"


namespace ptypes {


static char hexchar(uchar c) 
{
    if (c < 10)
        return char(c + '0');
    else
        return char(c - 10 + 'a');
}


inline bool isprintable(uchar c) 
{
    return ((c >= ' ') && (c < 127));
}


static string showmember(uchar c) 
{
    if ((c == '-') || (c == '~'))
        return string('~') + string(c);
    else if (isprintable(c))
        return c;
    else 
    {
        string ret = "~  ";
        ret[1] = hexchar(uchar(c >> 4));
        ret[2] = hexchar(uchar(c & 0x0f));
        return ret;
    }
}


string ptdecl asstring(const cset& s)
{
    string ret;
    int l = -1, r = -1;
    for(int i = 0; i <= _csetbits; i++) 
    {
        if (i < _csetbits && uchar(i) & s) 
        {
            if (l == -1)
                l = i;
            else
                r = i;
        }
        else if (l != -1) 
        {
            concat(ret, showmember(uchar(l)));
            if (r != -1) {
                if (r > l + 1) 
                    concat(ret, '-');
                concat(ret, showmember(uchar(r)));
            }
            l = -1;
            r = -1;
        }
    }
    return ret;
}


}
