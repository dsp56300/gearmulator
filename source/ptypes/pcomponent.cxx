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


component::component()
    : unknown(), refcount(0), freelist(nil), typeinfo(nil)  {}


component::~component() 
{
    if (freelist != nil) 
    {
        for (int i = 0; i < freelist->get_count(); i++)
            (*freelist)[i]->freenotify(this);
        delete freelist;
        freelist = nil;
    }
}


void component::freenotify(component*) 
{
}


void component::addnotification(component* obj) 
{
    if (freelist == nil)
        freelist = new tobjlist<component>(false);
    freelist->add(obj);
}


void component::delnotification(component* obj) 
{
    int i = -1;
    if (freelist != nil) 
    {
        i = freelist->indexof(obj);
        if (i >= 0) {
            freelist->del(i);
            if (freelist->get_count() == 0) 
            {
                delete freelist;
                freelist = nil;
            }
        }
    }
    if (i == -1)
        fatal(CRIT_FIRST + 1, "delnotification() failed: no such object");
}


int component::classid()
{
    return CLASS_UNDEFINED;
}


component* ptdecl addref(component* c)
{
    if (c != nil)
#ifdef PTYPES_ST
        c->refcount++;
#else
        pincrement(&c->refcount);
#endif
    return c;
}


bool ptdecl release(component* c)
{
    if (c != nil)
    {
#ifdef PTYPES_ST
        if (--c->refcount == 0)
#else
        if (pdecrement(&c->refcount) == 0)
#endif
            delete c;
        else
            return false;
    }
    return true;
}


}
