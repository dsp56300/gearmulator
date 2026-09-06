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


typedef int* pint;


static uchar lbitmask[8] = {0xff, 0xfe, 0xfc, 0xf8, 0xf0, 0xe0, 0xc0, 0x80};
static uchar rbitmask[8] = {0x01, 0x03, 0x07, 0x0f, 0x1f, 0x3f, 0x7f, 0xff};


void cset::include(char min, char max)
{
    if (uchar(min) > uchar(max))
        return;
    int lidx = uchar(min) / 8;
    int ridx = uchar(max) / 8;
    uchar lbits = lbitmask[uchar(min) % 8];
    uchar rbits = rbitmask[uchar(max) % 8];

    if (lidx == ridx) 
    {
        data[lidx] |= lbits & rbits;
    }
    else 
    {
        data[lidx] |= lbits;
        for (int i = lidx + 1; i < ridx; i++)
            data[i] = -1;
        data[ridx] |= rbits;
    }

}


char hex4(char c) 
{
    if (c >= 'a') 
        return uchar(c - 'a' + 10);
    else if (c >= 'A') 
        return uchar(c - 'A' + 10);
    else 
        return char(c - '0');
}


static uchar parsechar(const char*& p) 
{
    uchar ret = *p;
    if (ret == _csetesc) {
        p++;
        ret = *p;
        if ((ret >= '0' && ret <= '9') || (ret >= 'a' && ret <= 'f') || (ret >= 'A' && ret <= 'F')) {
            ret = hex4(ret);
            p++;
            if (*p != 0)
                ret = uchar((ret << 4) | hex4(*p));
        }
    }
    return ret;
}


void cset::assign(const char* p) 
{
    if (*p == '*' && *(p + 1) == 0)
        fill();
    else 
    {
        clear();
        for (; *p != 0; p++) {
            uchar left = parsechar(p);
            if (*(p + 1) == '-') {
                p += 2;
                uchar right = parsechar(p);
                include(left, right);
            }
            else
                include(left);
        }
    }
}


void cset::unite(const cset& s) 
{
    for(int i = 0; i < _csetwords; i++) 
        *(pint(data) + i) |= *(pint(s.data) + i);
}


void cset::subtract(const cset& s) 
{
    for(int i = 0; i < _csetwords; i++) 
        *(pint(data) + i) &= ~(*(pint(s.data) + i));
}


void cset::intersect(const cset& s) 
{
    for(int i = 0; i < _csetwords; i++) 
        *(pint(data) + i) &= *(pint(s.data) + i);
}


void cset::invert() 
{
    for(int i = 0; i < _csetwords; i++) 
        *(pint(data) + i) = ~(*(pint(data) + i));
}


bool cset::le(const cset& s) const 
{
    for (int i = 0; i < _csetwords; i++) 
    {
        int w1 = *(pint(data) + i);
        int w2 = *(pint(s.data) + i);
        if ((w2 | w1) != w2)
            return false;
    }
    return true;
}


}
