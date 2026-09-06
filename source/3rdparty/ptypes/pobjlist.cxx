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


_objlist::_objlist()
    : tpodlist<void*,true>()
{
    memset(&config, 0, sizeof(config));
}


_objlist::_objlist(bool ownobjects)
    : tpodlist<void*,true>()
{
    memset(&config, 0, sizeof(config));
    config.ownobjects = ownobjects;
}


_objlist::~_objlist()
{
}


void _objlist::dofree(void*)
{
    fatal(CRIT_FIRST + 38, "ptrlist::dofree() not defined");
}


int _objlist::compare(const void*, const void*) const
{
    fatal(CRIT_FIRST + 38, "ptrlist::compare() not defined");
    return 0;
}


void _objlist::dofree(int index, int num)
{
    void** p = (void**)list + index;
    while (--num >= 0)
        dofree(*p++);
}


void _objlist::doput(int index, void* obj)
{
    void** p = (void**)list + index;
    if (config.ownobjects)
        dofree(*p);
    *p = obj;
}


void _objlist::dodel(int index)
{
    if (config.ownobjects)
        dofree(doget(index));
    tpodlist<void*, true>::dodel(index);
}


void _objlist::dodel(int index, int delcount)
{
    if (config.ownobjects)
    {
        if (index + delcount > count)
            delcount = count - index;
        dofree(index, delcount);
    }
    tpodlist<void*, true>::dodel(index, delcount);
}


void _objlist::set_count(int newcount)
{
    if (newcount < count && config.ownobjects)
    {
        if (newcount < 0)
            newcount = 0;
        dofree(newcount, count - newcount);
    }
    _podlist::set_count(newcount, true);
}


void* _objlist::dopop()
{
    void* t = doget(--count);
    if (count == 0)
        set_capacity(0);
    return t;
}


bool _objlist::search(const void* key, int& index) const
{
    int l, h, i, c;
    bool ret = false;
    l = 0;
    h = count - 1;
    while (l <= h) 
    {
        i = (l + h) / 2;
        c = compare(key, doget(i));
        if (c > 0)
            l = i + 1;
        else 
        {
            h = i - 1;
            if (c == 0) 
            {
                ret = true;
                if (!config.duplicates)
                    l = i;
            }
        }
    }
    index = l;
    return ret;
}


int _objlist::indexof(void* obj) const
{
    for (int i = 0; i < count; i++)
        if (doget(i) == obj)
            return i;
    return -1;
}


#ifdef PTYPES19_COMPAT

objlist::objlist(bool ownobjects): tobjlist<unknown>(ownobjects)  {}

objlist::~objlist()  {}

#endif


}

