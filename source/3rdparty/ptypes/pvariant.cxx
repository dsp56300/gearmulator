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
#include <stdio.h>
#include <limits.h>

#include "ptypes.h"


namespace ptypes {


const variant nullvar;


struct _varitem
{
    string key;
    variant var;

    _varitem(const string& ikey, const variant& ivar): key(ikey), var(ivar) {}
};
typedef _varitem* pvaritem;


class ptpublic _varray: protected tobjlist<_varitem>
{
protected:
    int refcount;

    virtual int compare(const void* key, const void* item) const;

    friend class variant;

public:
    _varray();
    _varray(const _varray& a);
    virtual ~_varray();

    int get_count()                                     { return tobjlist<_varitem>::get_count(); }
    void clear()                                        { tobjlist<_varitem>::clear(); }
    void pack()                                         { tobjlist<_varitem>::pack(); }
    _varitem* doget(int index) const                    { return tobjlist<_varitem>::doget(index); }
    const variant& get(int index) const                 { if (unsigned(index) < unsigned(count)) return doget(index)->var; else return nullvar; }
    const string& getkey(int index) const               { if (unsigned(index) < unsigned(count)) return doget(index)->key; else return nullstring; }
    const variant& get(const char* key) const;
    int put(const string& key, const variant& var);
    void put(int index, const variant& var)             { if (unsigned(index) < unsigned(count)) doget(index)->var = var; }
    void ins(int index, const variant& var)             { if (unsigned(index) < unsigned(count)) doins(index, new _varitem(nullstring, var)); }
    int addvar(const variant& var);
    void del(int index)                                 { if (unsigned(index) < unsigned(count)) dodel(index); }
    void del(const string& key)                         { put(key, nullstring); }
};
typedef _varray* pvarray;


_varray::_varray()
    : tobjlist<_varitem>(true), refcount(0)
{
    config.sorted = true;
    config.casesens = true;
}


_varray::_varray(const _varray& a)
    : tobjlist<_varitem>(true), refcount(0)
{
    config.sorted = true;
    config.casesens = true;
    set_capacity(a.count);
    for (int i = 0; i < a.count; i++)
    {
        _varitem* v = a.doget(i);
        doins(i, new _varitem(v->key, v->var));
    }
}


_varray::~_varray()
{
}


int _varray::compare(const void* key, const void* item) const
{
   if (config.casesens)
        return strcmp(pconst(key), pvaritem(item)->key);
    else
        return strcasecmp(pconst(key), pvaritem(item)->key);
}


const variant& _varray::get(const char* key) const
{
    int index;
    if (search(key, index))
        return doget(index)->var;
    else
        return nullvar;
}


int _varray::put(const string& key, const variant& var)
{
    int index;
    if (search(pconst(key), index))
    {
        if (isnull(var))
            dodel(index);
        else
            doget(index)->var = var;
    }
    else if (!isnull(var))
        doins(index, new _varitem(key, var));
    return index;
}


int _varray::addvar(const variant& v)
{
    int i;
    if (count > 0 && isempty(doget(count - 1)->key))
        i = count;
    else
        i = 0;
    doins(i, new _varitem(nullstring, v));
    return i;
}


static void vconverr(large v);


static void vfatal()
{
    fatal(CRIT_FIRST + 60, "Variant data corrupt");
}


evariant::~evariant()
{
}


void variant::initialize(_varray* a)
{
    tag = VAR_ARRAY;
#ifdef PTYPES_ST
    a->refcount++;
#else
    pincrement(&a->refcount);
#endif
    value.a = a;
}


void variant::initialize(component* o)
{
    tag = VAR_OBJECT;
    value.o = addref(o);
}


void variant::initialize(const variant& v)
{
    switch (v.tag)
    {
    case VAR_NULL:
        tag = VAR_NULL;
        break;
    case VAR_INT:
    case VAR_BOOL:
    case VAR_FLOAT:
        tag = v.tag;
        value = v.value;
        break;
    case VAR_STRING:
        initialize(PTR_TO_STRING(v.value.s));
        break;
    case VAR_ARRAY:
        initialize(v.value.a);
        break;
    case VAR_OBJECT:
        initialize(v.value.o);
        break;
    default:
        vfatal();
    }
}


void variant::finalize()
{
    if (tag >= VAR_COMPOUND)
    {
        switch (tag)
        {
        case VAR_STRING:
            ptypes::finalize(PTR_TO_STRING(value.s));
            break;
        case VAR_ARRAY:
#ifdef PTYPES_ST
            if (--value.a->refcount == 0)
#else
            if (pdecrement(&value.a->refcount) == 0)
#endif
                delete value.a;
            break;
        case VAR_OBJECT:
            release(value.o);
            break;
        default:
            vfatal();
        }
    }
    tag = VAR_NULL;
}


void variant::assign(large v)           { finalize(); initialize(v); }
void variant::assign(bool v)            { finalize(); initialize(v); }
void variant::assign(double v)          { finalize(); initialize(v); }
void variant::assign(const char* v)     { finalize(); initialize(v); }


void variant::assign(const string& v)   
{ 
    if (tag == VAR_STRING)
        PTR_TO_STRING(value.s) = v;
    else
    {
        finalize();
        initialize(v);
    }
}


void variant::assign(_varray* a)
{
    if (tag == VAR_ARRAY && value.a == a)
        return;
    finalize();
    initialize(a);
}


void variant::assign(component* o)
{
    if (tag == VAR_OBJECT)
    {
        if (value.o == o)
            return;
        else
            release(value.o);
    }
    else
        finalize();
    initialize(o);
}


void variant::assign(const variant& v)
{
    switch (v.tag)
    {
    case VAR_NULL:
        finalize();
        tag = VAR_NULL;
        break;
    case VAR_INT:
    case VAR_BOOL:
    case VAR_FLOAT:
        finalize();
        tag = v.tag;
        value = v.value;
        break;
    case VAR_STRING:
        assign(PTR_TO_STRING(v.value.s));
        break;
    case VAR_ARRAY:
        assign(v.value.a);
        break;
    case VAR_OBJECT:
        assign(v.value.o);
        break;
    default:
        vfatal();
    }
}


void ptdecl clear(variant& v)
{
    v.finalize();
    v.initialize();
}


variant::operator int() const
{
    large t = operator large();
    if (t < INT_MIN || t > INT_MAX)
        vconverr(t);
    return int(t);
}


variant::operator unsigned int() const
{
    large t = operator large();
    if (t < 0 || t > UINT_MAX)
        vconverr(t);
    return uint(t);
}


variant::operator long() const
{
    large t = operator large();
    if (t < LONG_MIN || t > LONG_MAX)
        vconverr(t);
    return int(t);
}


variant::operator unsigned long() const
{
    large t = operator large();
    if (t < 0 || t > large(ULONG_MAX))
        vconverr(t);
    return uint(t);
}


variant::operator large() const
{
    switch(tag)
    {
    case VAR_NULL: return 0;
    case VAR_INT: return value.i;
    case VAR_BOOL: return int(value.b);
    case VAR_FLOAT: return int(value.f);
    case VAR_STRING: 
        {
            const char* p = PTR_TO_STRING(value.s);
            bool neg = *p == '-';
            if (neg)
                p++;
            large t = stringtoi(p);
            if (t < 0)
                return 0;
            else
                return neg ? -t : t;
        }
    case VAR_ARRAY: return value.a->count != 0;
    case VAR_OBJECT: return 0;
    default: vfatal();
    }
    return 0;
}


variant::operator bool() const
{
    switch(tag)
    {
    case VAR_NULL: return false;
    case VAR_INT: return value.i != 0;
    case VAR_BOOL: return value.b;
    case VAR_FLOAT: return value.f != 0;
    case VAR_STRING: return !isempty((PTR_TO_STRING(value.s)));
    case VAR_ARRAY: return value.a->count != 0;
    case VAR_OBJECT: return value.o != nil;
    default: vfatal();
    }
    return false;
}


variant::operator double() const
{
    switch(tag)
    {
    case VAR_NULL: return 0;
    case VAR_INT: return double(value.i);
    case VAR_BOOL: return int(value.b);
    case VAR_FLOAT: return value.f;
    case VAR_STRING: 
        {
            char* e;
            double t = strtod(PTR_TO_STRING(value.s), &e);
            if (*e != 0)
                return 0;
            else
                return t;
        }
    case VAR_ARRAY: return int(value.a->count != 0);
    case VAR_OBJECT: return 0;
    default: vfatal();
    }
    return 0;
}


void string::initialize(const variant& v)
{
    switch(v.tag)
    {
    case VAR_NULL: initialize(); break;
    case VAR_INT: initialize(itostring(v.value.i)); break;
    case VAR_BOOL: if (v.value.b) initialize('1'); else initialize('0'); break;
    case VAR_FLOAT:
        {
            char buf[256];
            sprintf(buf, "%g", v.value.f);
            initialize(buf);
        }
        break;
    case VAR_STRING: initialize(PTR_TO_STRING(v.value.s)); break;
    case VAR_ARRAY: initialize(); break;
    case VAR_OBJECT: initialize(); break;
    default: vfatal();
    }
}


variant::operator string() const
{
    // this is a 'dirty' solution to gcc 3.3 typecast problem. most compilers
    // handle variant::operator string() pretty well, while gcc 3.3 requires
    // to explicitly declare a constructor string::string(const variant&).
    // ironically, the presence of both the typecast operator and the constructor
    // confuses the MSVC compiler. so the only thing we can do to please all 
    // those compilers [that "move towards the c++ standard"] is to conditionally
    // exclude the constructor string(const variant&). and this is not the whole
    // story. i see you are bored with it and i let you go. nobody would ever care
    // about this. it just works, though i'm not happy with what i wrote here:
    string t;
    t.initialize(*this);
    return t;
}


variant::operator component*() const
{
    if (tag == VAR_OBJECT)
        return value.o;
    else
        return nil;
}


bool variant::equal(const variant& v) const
{
    if (tag != v.tag)
        return false;
    switch (tag)
    {
    case VAR_NULL: return true;
    case VAR_INT: return value.i == v.value.i;
    case VAR_BOOL: return value.b == v.value.b;
    case VAR_FLOAT: return value.f == v.value.f;
    case VAR_STRING: return strcmp(value.s, v.value.s) == 0;
    case VAR_ARRAY: return value.a == v.value.a;
    case VAR_OBJECT: return value.o == v.value.o;
    default: vfatal(); return false;
    }
}


static string numkey(large key)
{
    return itostring(key, 16, 16, '0');
}


void ptdecl aclear(variant& v)
{
    if (v.tag == VAR_ARRAY)
        v.value.a->clear();
    else
    {
        v.finalize();
        v.initialize(new _varray());
    }
}


void ptdecl apack(variant& v)
{
    if (v.tag == VAR_ARRAY)
        v.value.a->pack();
}


variant ptdecl aclone(const variant& v)
{
    if (v.tag == VAR_ARRAY)
        return variant(new _varray(*(v.value.a)));
    else
        return variant(new _varray());
}


int ptdecl alength(const variant& v)
{
    if (v.tag == VAR_ARRAY)
        return v.value.a->get_count();
    else
        return 0;
}


const variant& ptdecl get(const variant& v, const string& key)
{
    if (v.tag == VAR_ARRAY)
        return v.value.a->get(key);
    else
        return nullvar;
}


const variant& ptdecl get(const variant& v, large key)
{
    return get(v, numkey(key));
}


void ptdecl put(variant& v, const string& key, const variant& item)
{
    if (v.tag != VAR_ARRAY)
        aclear(v);
    v.value.a->put(key, item);
}


void ptdecl put(variant& v, large key, const variant& item)
{
    put(v, numkey(key), item);
}


void ptdecl del(variant& v, const string& key)
{
    if (v.tag == VAR_ARRAY)
        v.value.a->del(key);
}


void ptdecl del(variant& v, large key)
{
    del(v, numkey(key));
}


bool ptdecl anext(const variant& array, int& index, variant& item)
{
    string key;
    return anext(array, index, item, key);
}


bool ptdecl anext(const variant& array, int& index, variant& item, string& key)
{
    if (array.tag != VAR_ARRAY)
    {
        clear(item);
        return false;
    }
    if (index < 0 || index >= array.value.a->get_count())
    {
        clear(item);
        return false;
    }
    item = array.value.a->doget(index)->var;
    key = array.value.a->doget(index)->key;
    index++;
    return true;
}


int ptdecl aadd(variant& array, const variant& item)
{
    if (array.tag != VAR_ARRAY)
        aclear(array);
    return array.value.a->addvar(item);
}


const variant& ptdecl aget(const variant& array, int index)
{
    if (array.tag == VAR_ARRAY)
        return array.value.a->get(index);
    else
        return nullvar;
}


void ptdecl adel(variant& array, int index)
{
    if (array.tag == VAR_ARRAY)
        array.value.a->del(index);
}


void ptdecl aput(variant& array, int index, const variant& item)
{
    if (array.tag == VAR_ARRAY)
        array.value.a->put(index, item);
}


void ptdecl ains(variant& array, int index, const variant& item)
{
    if (array.tag == VAR_ARRAY)
        array.value.a->ins(index, item);
}


#ifdef _MSC_VER
// disable "unreachable code" warning for throw (known compiler bug)
#  pragma warning (disable: 4702)
#endif

static void vconverr(large v)
{
    throw new evariant("Value out of range: " + itostring(v));
}


}
