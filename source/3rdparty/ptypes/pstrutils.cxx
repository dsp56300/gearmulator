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


string ptdecl fill(int width, char pad)
{
    string res;
    if (width > 0) {
        setlength(res, width);
        memset(pchar(pconst(res)), pad, length(res));
    }
    return res;
}


string ptdecl pad(const string& s, int width, char c, bool left)
{
    int len = length(s);
    if (len < width && width > 0)
    {
        string res;
        setlength(res, width);
        if (left)
        {
            if (len > 0)
                memcpy(pchar(pconst(res)), pconst(s), len);
            memset(pchar(pconst(res)) + len, c, width - len);
        }
        else
        {
            memset(pchar(pconst(res)), c, width - len);
            if (len > 0)
                memcpy(pchar(pconst(res)) + width - len, pconst(s), len);
        }
        return res;
    }
    else
        return s;
}


}
