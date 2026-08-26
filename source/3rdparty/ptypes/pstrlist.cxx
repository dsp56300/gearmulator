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


typedef _stritem* pstritem;


void _strlist::sortederror()
{
    fatal(CRIT_FIRST + 32, "Operation not allowed on sorted string lists");
}


void _strlist::notsortederror()
{
    fatal(CRIT_FIRST + 33, "Search only allowed on sorted string lists");
}


void _strlist::duperror()
{
    fatal(CRIT_FIRST + 34, "Duplicate items not allowed in this string list");
}


_strlist::_strlist(int flags)
    : tobjlist<_stritem>(true)
{
    if ((flags & SL_SORTED) != 0)
        config.sorted = 1;
    if ((flags & SL_DUPLICATES) != 0)
        config.duplicates = 1;
    if ((flags & SL_CASESENS) != 0)
        config.casesens = 1;
    if ((flags & SL_OWNOBJECTS) != 0)
        config.ownslobjects = 1;
}


_strlist::~_strlist()
{
}


void _strlist::dofree(void* item)
{
    if (config.ownslobjects)
        dofreeobj(pstritem(item)->obj);
    delete pstritem(item);
}


void _strlist::dofreeobj(void*)
{
    fatal(CRIT_FIRST + 38, "strlist::dofree() not defined");
}


int _strlist::compare(const void* key, const void* item) const
{
   if (config.casesens)
        return strcmp(pconst(key), pstritem(item)->key);
    else
        return strcasecmp(pconst(key), pstritem(item)->key);
}


void _strlist::doins(int index, const string& key, void* obj)
{
    tobjlist<_stritem>::ins(index, new _stritem(key, obj));
}


void _strlist::doput(int index, const string& key, void* obj)
{
    if (config.sorted)
        sortederror();
    _stritem* p = doget(index);
    if (config.ownslobjects)
        dofreeobj(p->obj);
    p->key = key;
    p->obj = obj;
}


void _strlist::doput(int index, void* obj)
{
    _stritem* p = doget(index);
    if (config.ownslobjects)
        dofreeobj(p->obj);
    p->obj = obj;
}


int _strlist::put(const string& key, void* obj)
{
    if (!config.sorted)
        notsortederror();
    if (config.duplicates)
        duperror();
    int index;
    if (search(key, index))
    {
        if (obj == nil)
            dodel(index);
        else
            doput(index, obj);
    }
    else if (obj != nil)
        doins(index, key, obj);
    return index;
}


int _strlist::add(const string& key, void* obj)
{
    int index;
    if (config.sorted) 
    {
        if (search(key, index) && !config.duplicates)
            duperror();
    }
    else
        index = count;
    doins(index, key, obj);
    return index;
}


void* _strlist::operator [](const char* key) const
{
    if (!config.sorted)
        notsortederror();
    int index;
    if (search(key, index))
        return dogetobj(index);
    else
        return nil;
}


int _strlist::indexof(const char* key) const
{
    if (config.sorted) 
    {
        int index;
        if (search(key, index))
            return index;
    }
    else 
    {
        for (int i = 0; i < count; i++)
            if (compare(key, doget(i)) == 0)
                return i;
    }
    return -1;
}


int _strlist::indexof(void* obj) const
{
    for (int i = 0; i < count; i++)
        if (pstritem(doget(i))->obj == obj)
            return i;
    return -1;
}


//
// strmap
//

#ifdef PTYPES19_COMPAT

strlist::strlist(int flags): tstrlist<unknown>(flags)  {}

strlist::~strlist()  {}

strmap::strmap(int flags)
    : tstrlist<unknown>((flags | SL_SORTED) & ~SL_DUPLICATES)
{
}

strmap::~strmap()
{
}

#endif


}
