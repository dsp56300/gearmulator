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

#include "pstreams.h"


namespace ptypes {


outmemory::outmemory(int ilimit)
    : outstm(false, 0), mem(), limit(ilimit)
{
}


outmemory::~outmemory()
{
    close();
}


int outmemory::classid()
{
    return CLASS2_OUTMEMORY;
}


void outmemory::doopen()
{
}


void outmemory::doclose()
{
    clear(mem);
}


large outmemory::doseek(large newpos, ioseekmode mode)
{
    large pos;

    switch (mode)
    {
    case IO_BEGIN:
        pos = newpos;
        break;
    case IO_CURRENT:
        pos = abspos + newpos;
        break;
    default: // case IO_END:
        pos = length(mem) + newpos;
        break;
    }

    if (limit >= 0 && pos > limit)
        pos = limit;
    
    return pos;
}


int outmemory::dorawwrite(const char* buf, int count)
{
    if (count <= 0)
        return 0;
    if (limit >= 0 && abspos + count > limit)
    {
        count = limit - (int)abspos;
        if (count <= 0)
            return 0;
    }

    // the string reallocator takes care of efficiency
    if ((int)abspos + count > length(mem))
        setlength(mem, (int)abspos + count);
    memcpy(pchar(pconst(mem)) + (int)abspos, buf, count);
    return count;
}


string outmemory::get_streamname() 
{
    return "mem";
}


string outmemory::get_strdata()
{
    if (!active)
        errstminactive();
    return mem;
}


}
