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


void _podlist::idxerror()
{
    fatal(CRIT_FIRST + 30, "List index out of bounds");
}


_podlist::_podlist(int iitemsize)
    : list(0), count(0), capacity(0), itemsize(iitemsize)
{
    if (itemsize <= 0 || itemsize > 255)
        fatal(CRIT_FIRST + 37, "Invalid item size for podlist");
}


_podlist::~_podlist()
{
    set_count(0);
}


void _podlist::set_capacity(int newcap)
{
    if (newcap != capacity)
    {
        if (newcap < count)
            fatal(CRIT_FIRST + 36, "List capacity can't be smaller than count");
        list = memrealloc(list, newcap * itemsize);
        capacity = newcap;
    }
}


void _podlist::grow()
{
    if (capacity > count)
        return;
    set_capacity(capacity == 0 ? 4 : ((capacity + 1) / 2) * 3);
}


void _podlist::set_count(int newcount, bool zero)
{
    if (newcount > count)
    {
        if (newcount > capacity)
            set_capacity(newcount);
        if (zero)
            memset(doget(count), 0, (newcount - count) * itemsize);
        count = newcount;
    }
    else if (newcount < count)
    {
        if (newcount < 0)
            // wrong newcount: we don't want to generate an error here.
            // instead, we'll set count to 0 and wait until some other 
            // operation raises an error
            newcount = 0;
        count = newcount;
        if (count == 0)
            set_capacity(0);
    }
}


// doXXX() methods do not perform bounds checking; used internally
// where index is guaranteed to be valid

void* _podlist::doins(int index)
{
    grow();
    pchar s = pchar(doget(index));
    if (index < count)
        memmove(s + itemsize, s, (count - index) * itemsize);
    count++;
    return s;
}


void _podlist::doins(int index, const _podlist& t)
{
    if (&t == this)
        return;
    if (index == count)
        add(t);
    else
    {
        if (itemsize != t.itemsize)
            fatal(CRIT_FIRST + 35, "Incompatible list");
        if (t.count == 0)
            return;
        int ocnt = count;
        set_count(ocnt + t.count);
        pchar s = pchar(doget(index));
        memmove(s + t.count * itemsize, s, (ocnt - index) * itemsize);
        memcpy(s, t.list, t.count * itemsize);
    }
}


void* _podlist::add()
{
    grow();
    return doget(count++);
}


void _podlist::add(const _podlist& t)
{
    if (count == 0)
        operator =(t);
    else
    {
        if (itemsize != t.itemsize)
            fatal(CRIT_FIRST + 35, "Incompatible list");
        int ocnt = count;
        int tcnt = t.count;
        set_count(ocnt + tcnt);
        memcpy(doget(ocnt), t.list, tcnt * itemsize);
    }
}


_podlist& _podlist::operator =(const _podlist& t)
{
    if (&t != this)
    {
        if (itemsize != t.itemsize)
            fatal(CRIT_FIRST + 35, "Incompatible list");
        set_count(t.count);
        pack();
        memcpy(list, t.list, count * itemsize);
    }
    return *this;
}


void _podlist::dodel(int index)
{
    count--;
    if (index < count)
    {
        pchar s = pchar(doget(index));
        memmove(s, s + itemsize, (count - index) * itemsize);
    }
    else if (count == 0)
        set_capacity(0);
}


void _podlist::dodel(int index, int delcount)
{
    if (delcount <= 0)
        return;
    if (index + delcount > count)
        delcount = count - index;
    count -= delcount;
    if (index < count)
    {
        pchar s = pchar(doget(index));
        memmove(s, s + delcount * itemsize, (count - index) * itemsize);
    }
    else if (count == 0)
        set_capacity(0);
}


void _podlist::dopop()
{
    if (--count == 0)
        set_capacity(0);
}


}

