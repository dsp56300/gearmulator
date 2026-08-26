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

#ifndef __PTYPES_H__
#define __PTYPES_H__


#ifndef __PPORT_H__
#include "pport.h"
#endif

#include <string.h>


namespace ptypes {


#ifdef _MSC_VER
#pragma pack(push, 4)
// disable "non dll-interface class '...' used as base for dll-interface class '...'" warning
#pragma warning(disable : 4275)
// disable "conditional expression constant" warning
#pragma warning(push)
#pragma warning(disable : 4127)
#endif


ptpublic int   __PFASTCALL pincrement(int* target);
ptpublic int   __PFASTCALL pdecrement(int* target);
ptpublic int   __PFASTCALL pexchange(int* target, int value);
ptpublic void* __PFASTCALL pexchange(void** target, void* value);

template <class T> inline T* tpexchange(T** target, T* value)
    { return (T*)pexchange((void**)target, (void*)value); }


#if ((__GNUC__ == 3) && (__GNUC_MINOR__ >= 3)) || (__GNUC__ == 4) || defined(__hpux)
#  define VARIANT_TYPECAST_HACK
#endif


// -------------------------------------------------------------------- //
// --- string class --------------------------------------------------- //
// -------------------------------------------------------------------- //

// dynamic string class with thread-safe ref-counted buffer

struct _strrec
{
    int refcount;
    int length;
#ifdef __EMSCRIPTEN__
	int _padding0;
	int _padding1;
#endif
};
typedef _strrec* _pstrrec;


#define STR_BASE(x)      (_pstrrec(x)-1)
#define STR_REFCOUNT(x)  (STR_BASE(x)->refcount)
#define STR_LENGTH(x)    (STR_BASE(x)->length)

#define PTR_TO_PSTRING(p)   (pstring(&(p)))
#define PTR_TO_STRING(p)    (*PTR_TO_PSTRING(p))


ptpublic extern char* emptystr;

class ptpublic variant;


class ptpublic string
{
    friend class variant;

protected:
    char* data;

    static void idxerror();

    void _alloc(int);
    void _realloc(int);
    void _free();

    void initialize()  { data = emptystr; }
    void initialize(const char*, int);
    void initialize(const char*);
    void initialize(char);
    void initialize(const string& s);
    void initialize(const char*, int, const char*, int);
    void initialize(const variant&);
    void finalize();

    void assign(const char*, int);
    void assign(const char*);
    void assign(const string&);
    void assign(char);

#ifdef CHECK_BOUNDS
    void idx(int index) const  { if (unsigned(index) >= unsigned(STR_LENGTH(data))) idxerror(); }
#else
    void idx(int) const        { }
#endif

    string(const char* s1, int len1, const char* s2, int len2)  { initialize(s1, len1, s2, len2); }

public:
    friend int    length(const string& s);
    friend int    refcount(const string& s);
    friend void   assign(string& s, const char* buf, int len);
    friend void   clear(string& s);
    friend bool   isempty(const string& s);
    ptpublic friend char*  ptdecl setlength(string&, int);
    ptpublic friend char*  ptdecl unique(string&);
    ptpublic friend void   ptdecl concat(string& s, const char* sc, int catlen);
    ptpublic friend void   ptdecl concat(string& s, const char* s1);
    ptpublic friend void   ptdecl concat(string& s, char s1);
    ptpublic friend void   ptdecl concat(string& s, const string& s1);
    ptpublic friend string ptdecl copy(const string& s, int from, int cnt);
    ptpublic friend string ptdecl copy(const string& s, int from);
    ptpublic friend void   ptdecl ins(const char* s1, int s1len, string& s, int at);
    ptpublic friend void   ptdecl ins(const char* s1, string& s, int at);
    ptpublic friend void   ptdecl ins(char s1, string& s, int at);
    ptpublic friend void   ptdecl ins(const string& s1, string& s, int at);
    ptpublic friend void   ptdecl del(string& s, int at, int cnt);
    ptpublic friend void   ptdecl del(string& s, int at);
    ptpublic friend int    ptdecl pos(const char* s1, const string& s);
    ptpublic friend int    ptdecl pos(char s1, const string& s);
    friend int    pos(const string& s1, const string& s);
    ptpublic friend int    ptdecl rpos(char s1, const string& s);
    ptpublic friend bool   ptdecl contains(const char* s1, int len, const string& s, int at);
    ptpublic friend bool   ptdecl contains(const char* s1, const string& s, int at);
    ptpublic friend bool   ptdecl contains(char s1, const string& s, int at);
    ptpublic friend bool   ptdecl contains(const string& s1, const string& s, int at);
    ptpublic friend string ptdecl dup(const string& s);

    string()                                      { initialize(); }
    string(const char* sc, int initlen)           { initialize(sc, initlen); }
    string(const char* sc)                        { initialize(sc); }
    string(char c)                                { initialize(c); }
    string(const string& s)                       { initialize(s); }
    ~string()                                     { finalize(); }

#ifdef VARIANT_TYPECAST_HACK
    string(const variant& v)                      { initialize(v); }
#endif

    string& operator=  (const char* sc)           { assign(sc); return *this; }
    string& operator=  (char c)                   { assign(c); return *this; }
    string& operator=  (const string& s)          { assign(s); return *this; }
    string& operator+= (const char* sc)           { concat(*this, sc); return *this; }
    string& operator+= (char c)                   { concat(*this, c); return *this; }
    string& operator+= (const string& s)          { concat(*this, s); return *this; }

    string  operator+  (const char* sc) const;
    string  operator+  (char c) const;
    string  operator+  (const string& s) const;

    ptpublic friend string ptdecl operator+ (const char* sc, const string& s);
    ptpublic friend string ptdecl operator+ (char c, const string& s);

    bool operator== (const char* sc) const        { return strcmp(data, sc) == 0; }
    bool operator== (char) const;
    bool operator== (const string&) const;
    bool operator!= (const char* sc) const        { return !(*this == sc); }
    bool operator!= (char c) const                { return !(*this == c); }
    bool operator!= (const string& s) const       { return !(*this == s); }

    friend bool operator== (const char*, const string&);
    friend bool operator== (char, const string&);
    friend bool operator!= (const char*, const string&);
    friend bool operator!= (char, const string&);

    operator const char*() const                  { return data; }
    operator const uchar*() const                 { return (uchar*)data; }

    char&       operator[] (int i)                { idx(i); return unique(*this)[i]; }
    const char& operator[] (int i) const          { idx(i); return data[i]; }

    friend void initialize(string& s);
    friend void initialize(string& s, const string& s1);
    friend void initialize(string& s, const char* s1);
    friend void finalize(string& s);
};


typedef string* pstring;

inline int  length(const string& s)                     { return STR_LENGTH(s.data); }
inline int  refcount(const string& s)                   { return STR_REFCOUNT(s.data); }
inline void assign(string& s, const char* buf, int len) { s.assign(buf, len); }
inline void clear(string& s)                            { s.finalize(); }
inline bool isempty(const string& s)                    { return length(s) == 0; }
inline int  pos(const string& s1, const string& s)      { return pos(s1.data, s); }
inline bool operator== (const char* sc, const string& s){ return s == sc; }
inline bool operator== (char c, const string& s)        { return s == c; }
inline bool operator!= (const char* sc, const string& s){ return s != sc; }
inline bool operator!= (char c, const string& s)        { return s != c; }
inline void initialize(string& s)                       { s.initialize(); }
inline void initialize(string& s, const string& s1)     { s.initialize(s1); }
inline void initialize(string& s, const char* s1)       { s.initialize(s1); }
inline void finalize(string& s)                         { s.finalize(); }


ptpublic extern int stralloc;

ptpublic extern string nullstring;


// -------------------------------------------------------------------- //
// --- string utilities ----------------------------------------------- //
// -------------------------------------------------------------------- //


ptpublic string ptdecl fill(int width, char pad);
ptpublic string ptdecl pad(const string& s, int width, char c, bool left = true);

ptpublic string ptdecl itostring(large value, int base, int width = 0, char pad = 0);
ptpublic string ptdecl itostring(ularge value, int base, int width = 0, char pad = 0);
ptpublic string ptdecl itostring(int value, int base, int width = 0, char pad = 0);
ptpublic string ptdecl itostring(unsigned value, int base, int width = 0, char pad = 0);
ptpublic string ptdecl itostring(large v);
ptpublic string ptdecl itostring(ularge v);
ptpublic string ptdecl itostring(int v);
ptpublic string ptdecl itostring(unsigned v);

ptpublic large  ptdecl stringtoi(const char*);
ptpublic large  ptdecl stringtoie(const char*);
ptpublic ularge ptdecl stringtoue(const char*, int base);

ptpublic string ptdecl lowercase(const char* s);
ptpublic string ptdecl lowercase(const string& s);

char hex4(char c);

inline char locase(char c)
    { if (c >= 'A' && c <= 'Z') return char(c + 32); return c; }

inline char upcase(char c)
    { if (c >= 'a' && c <= 'z') return char(c - 32); return c; }

inline int hstrlen(const char* p) // some Unix systems do not accept NULL
    { return p == nil ? 0 : (int)strlen(p); }




// -------------------------------------------------------------------- //
// --- character set -------------------------------------------------- //
// -------------------------------------------------------------------- //


const int  _csetbits = 256;
const int  _csetbytes = _csetbits / 8;
const int  _csetwords = _csetbytes / sizeof(int);
const char _csetesc = '~';


class ptpublic cset
{
protected:
    char data[_csetbytes];

    void assign(const cset& s)                  { memcpy(data, s.data, _csetbytes); }
    void assign(const char* setinit);
    void clear()                                { memset(data, 0, _csetbytes); }
    void fill()                                 { memset(data, -1, _csetbytes); }
    void include(char b)                        { data[uchar(b) / 8] |= uchar(1 << (uchar(b) % 8)); }
    void include(char min, char max);
    void exclude(char b)                        { data[uchar(b) / 8] &= uchar(~(1 << (uchar(b) % 8))); }
    void unite(const cset& s);
    void subtract(const cset& s);
    void intersect(const cset& s);
    void invert();
    bool contains(char b) const                 { return (data[uchar(b) / 8] & (1 << (uchar(b) % 8))) != 0; }
    bool eq(const cset& s) const                { return memcmp(data, s.data, _csetbytes) == 0; }
    bool le(const cset& s) const;

public:
    cset()                                      { clear(); }
    cset(const cset& s)                         { assign(s); }
    cset(const char* setinit)                   { assign(setinit); }

    cset& operator=  (const cset& s)            { assign(s); return *this; }
    cset& operator+= (const cset& s)            { unite(s); return *this; }
    cset& operator+= (char b)                   { include(b); return *this; }
    cset  operator+  (const cset& s) const      { cset t = *this; return t += s; }
    cset  operator+  (char b) const             { cset t = *this; return t += b; }
    cset& operator-= (const cset& s)            { subtract(s); return *this; }
    cset& operator-= (char b)                   { exclude(b); return *this; }
    cset  operator-  (const cset& s) const      { cset t = *this; return t -= s; }
    cset  operator-  (char b) const             { cset t = *this; return t -= b; }
    cset& operator*= (const cset& s)            { intersect(s); return *this; }
    cset  operator*  (const cset& s) const      { cset t = *this; return t *= s; }
    cset  operator!  () const                   { cset t = *this; t.invert(); return t; }
    bool  operator== (const cset& s) const      { return eq(s); }
    bool  operator!= (const cset& s) const      { return !eq(s); }
    bool  operator<= (const cset& s) const      { return le(s); }
    bool  operator>= (const cset& s) const      { return s.le(*this); }

    friend cset operator+ (char b, const cset& s);
    friend bool operator& (char b, const cset& s);
    friend void assign(cset& s, const char* setinit);
    friend void clear(cset& s);
    friend void fill(cset& s);
    friend void include(cset& s, char b);
    friend void include(cset& s, char min, char max);
    friend void exclude(cset& s, char b);

    ptpublic friend string ptdecl asstring(const cset& s);
};


inline cset operator+ (char b, const cset& s)     { return s + b; }
inline bool operator& (char b, const cset& s)     { return s.contains(b); }
inline void assign(cset& s, const char* setinit)  { s.assign(setinit); }
inline void clear(cset& s)                        { s.clear(); }
inline void fill(cset& s)                         { s.fill(); }
inline void include(cset& s, char b)              { s.include(b); }
inline void include(cset& s, char min, char max)  { s.include(min, max); }
inline void exclude(cset& s, char b)              { s.exclude(b); }


// -------------------------------------------------------------------- //
// --- basic abstract classes ----------------------------------------- //
// -------------------------------------------------------------------- //

// basic class with virtual destructor; historically was used as a base
// for all list items. also helps to count the number of created and
// destroyed objects in a program (objalloc global) in DEBUG mode, to
// detect memory leaks. most classes in ptypes are derived from unknown.

ptpublic extern int objalloc;

class ptpublic unknown
{
private:
    // make all classes non-copyable by default
    unknown(const unknown&);
    const unknown& operator= (const unknown&);
public:
#ifdef COUNT_OBJALLOC
    unknown()           { pincrement(&objalloc); }
    virtual ~unknown()  { pdecrement(&objalloc); }
#else
    unknown()           { }
    virtual ~unknown()  { }
#endif
};

typedef unknown* punknown;


// provide non-copyable base for all classes that are
// not derived from 'unknown'

class ptpublic noncopyable
{
private:
    noncopyable(const noncopyable&);
    const noncopyable& operator= (const noncopyable&);
public:
    noncopyable() {}
    ~noncopyable() {}
};



// -------------------------------------------------------------------- //
// --- exception ------------------------------------------------------ //
// -------------------------------------------------------------------- //

// the basic exception class. NOTE: the library always throws dynamically
// allocated exception objects.

class ptpublic exception: public unknown
{
protected:
    string message;
public:
    exception(const char* imsg);
    exception(const string& imsg);
    virtual ~exception();
    virtual const string& get_message() const { return message; }
};


// conversion exception class for stringtoie() and stringtoue()

class ptpublic econv: public exception
{
public:
    econv(const char* msg): exception(msg)  {}
    econv(const string& msg): exception(msg)  {}
    virtual ~econv();
};


// -------------------------------------------------------------------- //
// --- tpodlist ------------------------------------------------------- //
// -------------------------------------------------------------------- //

// _podlist implements dynamic array of small POD structures; it serves
// as a basis for all list types in the library. this class is undocumented.
// tpodlist template must be used instead.

class ptpublic _podlist: public noncopyable
{
protected:
    void* list;                   // pointer to the array
    int   count;                  // number of items in the list
    int   capacity;               // allocated for the list
    int   itemsize;               // list item size

    static void idxerror();

    _podlist& operator =(const _podlist& t);

    void  grow();
    void* doins(int index);
    void  doins(int index, const _podlist&);
    void* doget(int index) const            { return (char*)list + index * itemsize; }
    void  dodel(int index);
    void  dodel(int index, int count);
    void  dopop();

#ifdef CHECK_BOUNDS
    void idx(int index) const               { if (unsigned(index) >= unsigned(count)) idxerror(); }
    void idxa(int index) const              { if (unsigned(index) > unsigned(count)) idxerror(); }
#else
    void idx(int) const                     { }
    void idxa(int) const                    { }
#endif

public:
    _podlist(int itemsize);
    ~_podlist();

    int   get_count() const                 { return count; }
    void  set_count(int newcount, bool zero = false);
    int   get_capacity() const              { return capacity; }
    void  set_capacity(int newcap);
    void  clear()                           { set_count(0); }
    void  pack()                            { set_capacity(count); }
    void* ins(int index)                    { idxa(index); return doins(index); }
    void  ins(int index, const _podlist& t) { idxa(index); doins(index, t); }
    void* add();
    void  add(const _podlist& t);
    void* operator [](int index)            { idx(index); return doget(index); }
    void* top()                             { return operator [](count - 1); }
    void  del(int index)                    { idx(index); dodel(index); }
    void  del(int index, int count)         { idx(index); dodel(index, count); }
    void  pop()                             { idx(0); dopop(); }
};


// tpodlist is a fully-inlined template based on _podlist

template <class X, bool initzero = false> class tpodlist: public _podlist
{
protected:
    X&   dozero(X& t)                       { if (initzero) memset(&t, 0, sizeof(X)); return t; }
    X&   doget(int index) const             { return ((X*)list)[index]; }
    X&   doins(int index)                   { X& t = *(X*)_podlist::doins(index); return dozero(t); }
    void doins(int index, const X& item)    { *(X*)_podlist::doins(index) = item; }

public:
    tpodlist(): _podlist(sizeof(X))         {}
    tpodlist<X, initzero>& operator =(const tpodlist<X, initzero>& t)
                                            { _podlist::operator =(t); return *this; }

    void set_count(int newcount)            { _podlist::set_count(newcount, initzero); }
    X&   ins(int index)                     { idxa(index); return doins(index); }
    void ins(int index, const X& item)      { idxa(index); doins(index, item); }
    void ins(int index, const tpodlist<X, initzero>& t)
                                            { _podlist::ins(index, t); }
    X&   add()                              { grow(); return dozero(doget(count++)); }
    void add(const X& item)                 { grow(); doget(count++) = item; }
    void add(const tpodlist<X, initzero>& t)
					    { _podlist::add(t); }
    X&   operator [](int index)             { idx(index); return doget(index); }
    const X& operator [](int index) const   { idx(index); return doget(index); }
    X&   top()                              { idx(0); return doget(count - 1); }
};


// -------------------------------------------------------------------- //
// --- tobjlist ------------------------------------------------------- //
// -------------------------------------------------------------------- //

// _objlist is a base for the tobjlist template, don't use it directly.
// also, _objlist is a base for _strlist and derivatives.

class ptpublic _objlist: public unknown, protected tpodlist<void*, true>
{
protected:
    struct
    {
        unsigned ownobjects :1;   // list is responsible for destroying the items; used in _objlist
        unsigned ownslobjects :1; // same but for _strlist items (in _stritem structure)
        unsigned sorted :1;       // sorted list (_objlist+)
        unsigned duplicates :1;   // sorted: allows duplicate keys (_objlist+)
        unsigned casesens :1;     // sorted: string comparison is case sensitive (_strlist+)
        unsigned _reserved :27;
    } config;

    _objlist(bool ownobjects);	  // we hide this ctor, since _objlist actually can't free objects

    void* doget(int index) const            { return ((void**)list)[index]; }
    void  doput(int index, void* obj);
    void  dodel(int index);
    void  dodel(int index, int count);
    void* dopop();
    void  dofree(int index, int count);

    virtual void dofree(void* obj);        // pure method; defined in tobjlist instances
    virtual int compare(const void* key, const void* obj) const;  // pure method; defined in _strlist

public:
    _objlist();
    virtual ~_objlist();

    int   get_count() const                 { return count; }
    void  set_count(int newcount);
    int   get_capacity() const              { return capacity; }
    void  set_capacity(int newcap)          { tpodlist<void*,true>::set_capacity(newcap); }
    void  clear()                           { set_count(0); }
    void  pack()                            { tpodlist<void*,true>::pack(); }
    void  ins(int index, void* obj)         { tpodlist<void*,true>::ins(index, obj); }
    void  add(void* obj)                    { tpodlist<void*,true>::add(obj); }
    void  put(int index, void* obj)         { idx(index); doput(index, obj); }
    void* operator [](int index) const      { idx(index); return doget(index); }
    void* top() const                       { idx(0); return doget(count - 1); }
    void* pop()                             { idx(0); return dopop(); }
    void  del(int index)                    { idx(index); dodel(index); }
    void  del(int index, int count)         { idx(index); dodel(index, count); }
    int   indexof(void* obj) const;
    bool  search(const void* key, int& index) const;
};


// the tobjlist template implements a list of pointers to arbitrary
// structures. optionally can automatically free objects (ownobjects)
// when removed from a list. only 2 virtual functions are being
// instantiated by this template, the rest is static code in _objlist.

template <class X> class tobjlist: public _objlist
{
protected:
    X* doget(int index) const               { return (X*)_objlist::doget(index); }
    virtual void dofree(void* obj);

public:
    tobjlist(bool ownobjects = false): _objlist(ownobjects)  {}
    virtual ~tobjlist();

    bool  get_ownobjects() const            { return config.ownobjects; }
    void  set_ownobjects(bool newval)       { config.ownobjects = newval; }
    void  ins(int index, X* obj)            { _objlist::ins(index, obj); }
    void  add(X* obj)                       { _objlist::add(obj); }
    void  put(int index, X* obj)            { _objlist::put(index, obj); }
    X*    operator [](int index) const      { idx(index); return (X*)doget(index); }
    X*    top() const                       { return (X*)_objlist::top(); }
    X*    pop()                             { return (X*)_objlist::pop(); }
    int   indexof(X* obj) const             { return _objlist::indexof(obj); }

#ifdef PTYPES19_COMPAT
    friend inline void ins(tobjlist& s, int i, X* obj)          { s.ins(i, obj); }
    friend inline int  add(tobjlist& s, X* obj)                 { s.add(obj); return s.get_count() - 1; }
    friend inline void put(tobjlist& s, int i, X* obj)          { s.put(i, obj); }
    friend inline int  indexof(const tobjlist& s, X* obj)       { return s.indexof(obj); }
    friend inline int  push(tobjlist& s, X* obj)                { s.add(obj); return s.get_count() - 1; }
    friend inline X*   pop(tobjlist& s)                         { return (X*)s.pop(); }
    friend inline X*   top(const tobjlist& s)                   { return (X*)s.top(); }
    friend inline X*   get(const tobjlist& s, int i)            { return (X*)s[i]; }
#endif
};


template <class X> void tobjlist<X>::dofree(void* item)
{
    delete (X*)item;
}


template <class X> tobjlist<X>::~tobjlist()
{
    set_count(0);
}


// -------------------------------------------------------------------- //
// --- tstrlist ------------------------------------------------------- //
// -------------------------------------------------------------------- //

// _strlist is a base for the tstrlist template


typedef int slflags; // left for compatibility

#define SL_SORTED      1
#define SL_DUPLICATES  2
#define SL_CASESENS    4
#define SL_OWNOBJECTS  8


struct _stritem
{
    string key;
    void* obj;

    _stritem(const string& ikey, void* iobj)
        : key(ikey), obj(iobj)  {}
};


class ptpublic _strlist: protected tobjlist<_stritem>
{
protected:
    static void sortederror();
    static void notsortederror();
    static void duperror();

    virtual void dofree(void* item);
    virtual int  compare(const void* key, const void* item) const;
    virtual void dofreeobj(void* obj);          // pure; tstrlist overrides it

    const string& dogetkey(int index) const             { return doget(index)->key; }
    void* dogetobj(int index) const                     { return doget(index)->obj; }
    void  doins(int index, const string& key, void* obj);
    void  doput(int index, const string& key, void* obj);
    void  doput(int index, void* obj);

public:
    _strlist(int flags = 0);
    virtual ~_strlist();

    int   get_count() const                             { return count; }
    void  set_count(int newcount)                       { tobjlist<_stritem>::set_count(newcount); }
    int   get_capacity() const                          { return capacity; }
    void  set_capacity(int newcap)                      { tobjlist<_stritem>::set_capacity(newcap); }
    void  clear()                                       { tobjlist<_stritem>::clear(); }
    void  pack()                                        { tobjlist<_stritem>::pack(); }
    bool  get_sorted() const                            { return config.sorted; }
    bool  get_duplicates() const                        { return config.duplicates; }
    bool  get_casesens() const                          { return config.casesens; }
    bool  get_ownobjects() const                        { return config.ownslobjects; }
    void  set_ownobjects(bool newval)                   { config.ownslobjects = newval; }
    void  ins(int index, const string& key, void* obj)  { idxa(index); doins(index, key, obj); }
    void  put(int index, const string& key, void* obj)  { idx(index); doput(index, key, obj); }
    void  put(int index, void* obj)                     { idx(index); doput(index, obj); }
    int   put(const string& key, void* obj);
    int   add(const string& key, void* obj);
    void* operator [](int index) const                  { idx(index); return dogetobj(index); }
    void* operator [](const char* key) const;
    const string& getkey(int index) const               { idx(index); return dogetkey(index); }
    bool  search(const char* key, int& index) const     { return _objlist::search(key, index); }
    void  del(int index)                                { idx(index); dodel(index); }
    void  del(int index, int delcount)                  { idx(index); dodel(index, delcount); }
    void  del(const char* key)                          { put(key, nil); }
    int   indexof(const char* key) const;
    int   indexof(void* obj) const;
};


// the tstrlist template implements a list of string/object pairs,
// optionally sorted for fast searching by string key.

template <class X> class tstrlist: public _strlist
{
protected:
    virtual void dofreeobj(void* obj);

public:
    tstrlist(int flags = 0): _strlist(flags)  {}
    virtual ~tstrlist();

    void  ins(int index, const string& key, X* obj)     { _strlist::ins(index, key, obj); }
    void  put(int index, const string& key, X* obj)     { _strlist::put(index, key, obj); }
    void  put(int index, X* obj)                        { _strlist::put(index, obj); }
    int   put(const string& key, X* obj)                { return _strlist::put(key, obj); }
    int   add(const string& key, X* obj)                { return _strlist::add(key, obj); }
    X*    operator [](int index) const                  { return (X*)_strlist::operator [](index); }
    X*    operator [](const char* key) const            { return (X*)_strlist::operator [](key); }
    int   indexof(X* obj) const                         { return _strlist::indexof(obj); }
    int   indexof(const char* key) const                { return _strlist::indexof(key); }

#ifdef PTYPES19_COMPAT
    // pre-2.0 interface for backwards compatibility
    friend inline void ins(tstrlist& s, int i, const string& str, X* obj)  { s.ins(i, str, obj); }
    friend inline int  add(tstrlist& s, const string& str, X* obj)         { return s.add(str, obj); }
    friend inline void put(tstrlist& s, int i, const string& str, X* obj)  { s.put(i, str, obj); }
    friend inline void put(tstrlist& s, int i, X* obj)                     { s.put(i, obj); }
    friend inline int indexof(const tstrlist& s, X* obj)                   { return s.indexof(obj); }
    friend inline X* get(const tstrlist& s, int i)                         { return (X*)s[i]; }
#endif
};


template <class X> void tstrlist<X>::dofreeobj(void* obj)
{
    delete (X*)obj;
}


template <class X> tstrlist<X>::~tstrlist()
{
    set_count(0);
}


// -------------------------------------------------------------------- //
// --- textmap -------------------------------------------------------- //
// -------------------------------------------------------------------- //

// textmap is a list of string pairs (key/value)

struct _textitem
{
    string key;
    string value;

    _textitem(const string& ikey, const string& ivalue)
        : key(ikey), value(ivalue)  {}
};


class ptpublic textmap: protected tobjlist<_textitem>
{
protected:
    virtual int compare(const void* key, const void* item) const;
    const string& dogetvalue(int index) const           { return doget(index)->value; }
    const string& dogetkey(int index) const             { return doget(index)->key; }

public:
    textmap(bool casesens = false);
    virtual ~textmap();

    int   get_count() const                             { return tobjlist<_textitem>::get_count(); }
    void  pack()                                        { tobjlist<_textitem>::pack(); }
    void  clear()                                       { tobjlist<_textitem>::clear(); }
    int   put(const string& key, const string& value);
    void  del(int index)                                { idx(index); dodel(index); }
    void  del(const char* key)                          { put(key, nullstring); }
    const string& get(int index) const                  { idx(index); return dogetvalue(index); }
    const string& getkey(int index) const               { idx(index); return dogetkey(index); }
    const string& get(const char* key) const;
    const string& operator [](int index) const          { return get(index); }
    const string& operator [](const char* key) const    { return get(key); }
    int   indexof(const char* key) const;
};


// -------------------------------------------------------------------- //
// --- component ------------------------------------------------------ //
// -------------------------------------------------------------------- //

// the component class is an abstract class that provides reference
// counting and delete notification mechanisms. all stream classes
// in ptypes are derived from component.

// class ID's for all basic types: the first byte (least significant)
// contains the base ID, the next is for the second level of inheritance,
// etc. total of 4 levels allowed for basic types. call classid() for an
// object, mask out first N bytes of interest and compare with a CLASS_XXX
// value. f.ex. to determine whether an object is of type infile or any
// derivative: (o->classid() & 0xffff) == CLASS2_INFILE. this scheme is for
// internal use by PTypes and Objection; partly replaces the costly C++ RTTI
// system.

// first level of inheritance
const int CLASS_UNDEFINED = 0x00000000;
const int CLASS_INSTM     = 0x00000001;
const int CLASS_OUTSTM    = 0x00000002;
const int CLASS_UNIT      = 0x00000003;

// second level of inheritance
const int CLASS2_INFILE    = 0x00000100 | CLASS_INSTM;
const int CLASS2_INMEMORY  = 0x00000200 | CLASS_INSTM;
const int CLASS2_FDX       = 0x00000300 | CLASS_INSTM;
const int CLASS2_OUTFILE   = 0x00000100 | CLASS_OUTSTM;
const int CLASS2_OUTMEMORY = 0x00000200 | CLASS_OUTSTM;

// third level of inheritance
const int CLASS3_LOGFILE   = 0x00010000 | CLASS2_OUTFILE;
const int CLASS3_IPSTM     = 0x00020000 | CLASS2_FDX;
const int CLASS3_NPIPE     = 0x00030000 | CLASS2_FDX;


class ptpublic component: public unknown
{
protected:
    int                  refcount;     // reference counting, used by addref() and release()
    tobjlist<component>* freelist;     // list of components to notify about destruction, safer alternative to ref-counting
    void*                typeinfo;     // reserved for future use

    virtual void freenotify(component* sender);

public:
    component();
    virtual ~component();
    void addnotification(component* obj);
    void delnotification(component* obj);

    ptpublic friend component* ptdecl addref(component*);
    ptpublic friend bool ptdecl release(component*);
    friend int refcount(component* c);

    virtual int classid();

    void  set_typeinfo(void* t) { typeinfo = t; }
    void* get_typeinfo()        { return typeinfo; }
};

typedef component* pcomponent;


inline int refcount(component* c)  { return c->refcount; }

component* ptdecl addref(component* c );

template <class T> inline T* taddref(T* c)
    { return (T*)addref((component*)c); }


template <class T> class compref
{
protected:
    T* ref;
public:
    compref()                                   { ref = 0; }
    compref(const compref<T>& r)                { ref = taddref<T>(r.ref); }
    compref(T* c)                               { ref = taddref<T>(c); }
    ~compref()                                  { release(ref); }
    compref<T>& operator =(T* c);
    compref<T>& operator =(const compref<T>& r) { return operator =(r.ref); }
    T&   operator *() const                     { return *ref; }
    T*   operator ->() const                    { return ref; }
    bool operator ==(const compref<T>& r) const { return ref == r.ref; }
    bool operator ==(T* c) const                { return ref == c; }
    bool operator !=(const compref<T>& r) const { return ref != r.ref; }
    bool operator !=(T* c) const                { return ref != c; }
         operator T*() const                    { return ref; }
};


template <class T> compref<T>& compref<T>::operator =(T* c)
{
    release(tpexchange<T>(&ref, taddref<T>(c)));
    return *this;
}


// -------------------------------------------------------------------- //
// --- variant -------------------------------------------------------- //
// -------------------------------------------------------------------- //


enum {
    VAR_NULL,
    VAR_INT,
    VAR_BOOL,
    VAR_FLOAT,
    VAR_STRING,
    VAR_ARRAY,
    VAR_OBJECT,

    VAR_COMPOUND = VAR_STRING
};


class ptpublic _varray;


class ptpublic variant
{
    friend class string;
    friend class _varray;

protected:
    int tag;            // VAR_XXX
    union {
        large      i;   // 64-bit int value
        bool       b;   // bool value
        double     f;   // double value
        char*      s;   // string object; can't declare as string because of the union
        _varray*   a;   // pointer to a reference-counted _strlist object
        component* o;   // pointer to a reference-counted component object (or derivative)
    } value;            // we need this name to be able to copy the entire union in some situations

    void initialize()                       { tag = VAR_NULL; }
    void initialize(large v)                { tag = VAR_INT; value.i = v; }
    void initialize(bool v)                 { tag = VAR_BOOL; value.b = v; }
    void initialize(double v)               { tag = VAR_FLOAT; value.f = v; }
    void initialize(const char* v)          { tag = VAR_STRING; ptypes::initialize(PTR_TO_STRING(value.s), v); }
    void initialize(const string& v)        { tag = VAR_STRING; ptypes::initialize(PTR_TO_STRING(value.s), v); }
    void initialize(_varray* a);
    void initialize(component* o);
    void initialize(const variant& v);
    void finalize();

    void assign(large);
    void assign(bool);
    void assign(double);
    void assign(const char*);
    void assign(const string&);
    void assign(_varray*);
    void assign(component*);
    void assign(const variant&);

    bool equal(const variant& v) const;

    variant(_varray* a)                     { initialize(a); }

public:
    // construction
    variant()                               { initialize(); }
    variant(int v)                          { initialize(large(v)); }
    variant(unsigned int v)                 { initialize(large(v)); }
    variant(large v)                        { initialize(v); }
    variant(bool v)                         { initialize(v); }
    variant(double v)                       { initialize(v); }
    variant(const char* v)                  { initialize(v); }
    variant(const string& v)                { initialize(v); }
    variant(component* v)                   { initialize(v); }
    variant(const variant& v)               { initialize(v); }
    ~variant()                              { finalize(); }

    // assignment
    variant& operator= (int v)              { assign(large(v)); return *this; }
    variant& operator= (unsigned int v)     { assign(large(v)); return *this; }
    variant& operator= (large v)            { assign(v); return *this; }
    variant& operator= (bool v)             { assign(v); return *this; }
    variant& operator= (double v)           { assign(v); return *this; }
    variant& operator= (const char* v)      { assign(v); return *this; }
    variant& operator= (const string& v)    { assign(v); return *this; }
    variant& operator= (component* v)       { assign(v); return *this; }
    variant& operator= (const variant& v)   { assign(v); return *this; }

    // typecast
    operator int() const;
    operator unsigned int() const;
    operator long() const;
    operator unsigned long() const;
    operator large() const;
    operator bool() const;
    operator double() const;
    operator string() const;
    operator component*() const;

    // comparison
    bool operator== (const variant& v) const  { return equal(v); }
    bool operator!= (const variant& v) const  { return !equal(v); }

    // typification
    ptpublic friend void ptdecl clear(variant&);
    friend int  vartype(const variant& v);
    friend bool isnull(const variant& v);
    friend bool isint(const variant& v);
    friend bool isbool(const variant& v);
    friend bool isfloat(const variant& v);
    friend bool isstring(const variant& v);
    friend bool isarray(const variant& v);
    friend bool isobject(const variant& v);
    friend bool iscompound(const variant& v);

    // array manipulation
    ptpublic friend void ptdecl aclear(variant&);
    ptpublic friend variant ptdecl aclone(const variant&);
    ptpublic friend const variant& ptdecl get(const variant&, const string& key);
    ptpublic friend const variant& ptdecl get(const variant&, large key);
    ptpublic friend void ptdecl put(variant&, const string& key, const variant& item);
    ptpublic friend void ptdecl put(variant&, large key, const variant& item);
    ptpublic friend void ptdecl del(variant&, const string& key);
    ptpublic friend void ptdecl del(variant&, large key);

    // indexed access to arrays
    ptpublic friend int  ptdecl alength(const variant&);
    ptpublic friend void ptdecl apack(variant&);
    ptpublic friend bool ptdecl anext(const variant& a, int&, variant& item);
    ptpublic friend bool ptdecl anext(const variant& a, int&, variant& item, string& key);
    ptpublic friend int  ptdecl aadd(variant&, const variant& item);
    ptpublic friend void ptdecl aput(variant&, int index, const variant& item);
    ptpublic friend void ptdecl ains(variant&, int index, const variant& item);
    ptpublic friend void ptdecl adel(variant&, int index);
    ptpublic friend const variant& ptdecl aget(const variant&, int index);
    ptpublic friend string ptdecl akey(const variant&, int index);

    const variant& operator[](const char* key) const    { return get(*this, string(key)); }
    const variant& operator[](const string& key) const  { return get(*this, key); }
    const variant& operator[](large key) const          { return get(*this, key); }

    // 'manual' initialization/finalization, undocumented. use with care!
    friend void initialize(variant& v);
    friend void initialize(variant& v, large i);
    friend void initialize(variant& v, int i);
    friend void initialize(variant& v, unsigned int i);
    friend void initialize(variant& v, bool i);
    friend void initialize(variant& v, double i);
    friend void initialize(variant& v, const char* i);
    friend void initialize(variant& v, const string& i);
    friend void initialize(variant& v, component* i);
    friend void initialize(variant& v, const variant& i);
    friend void finalize(variant& v);
};


typedef variant* pvariant;


inline int  vartype(const variant& v)       { return v.tag; }
inline bool isnull(const variant& v)        { return v.tag == VAR_NULL; }
inline bool isint(const variant& v)         { return v.tag == VAR_INT; }
inline bool isbool(const variant& v)        { return v.tag == VAR_BOOL; }
inline bool isfloat(const variant& v)       { return v.tag == VAR_FLOAT; }
inline bool isstring(const variant& v)      { return v.tag == VAR_STRING; }
inline bool isarray(const variant& v)       { return v.tag == VAR_ARRAY; }
inline bool isobject(const variant& v)      { return v.tag == VAR_OBJECT; }
inline bool iscompound(const variant& v)    { return v.tag >= VAR_COMPOUND; }

inline void initialize(variant& v)                   { v.initialize(); }
inline void initialize(variant& v, large i)          { v.initialize(i); }
inline void initialize(variant& v, int i)            { v.initialize(large(i)); }
inline void initialize(variant& v, unsigned int i)   { v.initialize(large(i)); }
inline void initialize(variant& v, bool i)           { v.initialize(i); }
inline void initialize(variant& v, double i)         { v.initialize(i); }
inline void initialize(variant& v, const char* i)    { v.initialize(i); }
inline void initialize(variant& v, const string& i)  { v.initialize(i); }
inline void initialize(variant& v, component* i)     { v.initialize(i); }
inline void initialize(variant& v, const variant& i) { v.initialize(i); }
inline void finalize(variant& v)                     { if (v.tag >= VAR_COMPOUND) v.finalize(); }


ptpublic extern const variant nullvar;


// variant exception class; may be thrown when a variant
// is being typecast'ed to 32-bit int and the value is
// out of range

class ptpublic evariant: public exception
{
protected:
public:
    evariant(const char* msg): exception(msg)  {}
    evariant(const string& msg): exception(msg)  {}
    virtual ~evariant();
};



// -------------------------------------------------------------------- //
// --- pre-2.0 compatibility declarations ----------------------------- //
// -------------------------------------------------------------------- //


#ifdef PTYPES19_COMPAT

// ptypes-1.9 objlist and strlist: accept only 'unknown' and
// derivatives as a base type

class ptpublic objlist: public tobjlist<unknown>
{
public:
    objlist(bool ownobjects = false);
    virtual ~objlist();
};

inline int  length(const _objlist& s)                { return s.get_count(); }
inline void setlength(_objlist& s, int newcount)     { s.set_count(newcount); }
inline void pack(_objlist& s)                        { s.pack(); }
inline void clear(_objlist& s)                       { s.clear(); }
inline int  push(_objlist& s, unknown* obj)          { s.add(obj); return length(s) - 1; }
inline unknown* top(const _objlist& s)               { return (unknown*)s.top(); }
inline void ins(_objlist& s, int i, unknown* obj)    { s.ins(i, obj); }
inline int  add(_objlist& s, unknown* obj)           { s.add(obj); return length(s) - 1; }
inline void put(_objlist& s, int i, unknown* obj)    { s.put(i, obj); }
inline unknown* get(const _objlist& s, int i)        { return (unknown*)s[i]; }
inline unknown* pop(_objlist& s)                     { return (unknown*)s.pop(); }
inline void del(_objlist& s, int i)                  { s.del(i); }
inline int  indexof(const _objlist& s, unknown* obj) { return s.indexof(obj); }


class ptpublic strlist: public tstrlist<unknown>
{
public:
    strlist(int flags = 0);
    virtual ~strlist();
};

inline int  length(const _strlist& s)                                { return s.get_count(); }
inline void clear(_strlist& s)                                       { s.clear(); }
inline void pack(_strlist& s)                                        { s.pack(); }
inline bool search(const _strlist& s, const char* key, int& i)       { return s.search(key, i); }
inline void ins(_strlist& s, int i, const string& key, unknown* obj) { s.ins(i, key, obj); }
inline int  add(_strlist& s, const string& key, unknown* obj)        { return s.add(key, obj); }
inline void put(_strlist& s, int i, const string& key, unknown* obj) { s.put(i, key, obj); }
inline void put(_strlist& s, int i, unknown* obj)                    { s.put(i, obj); }
inline unknown* get(const _strlist& s, int i)                        { return (unknown*)s[i]; }
inline const string& getstr(const _strlist& s, int i)                { return s.getkey(i); }
inline void del(_strlist& s, int i)                                  { s.del(i); }
inline int  find(const _strlist& s, const char* key)                 { return s.indexof(key); }
inline int  indexof(const _strlist& s, unknown* obj)                 { return s.indexof(obj); }


// ptypes-1.9 strmap: now replaced with _strlist(SL_SORTED)

class ptpublic strmap: public tstrlist<unknown>
{
public:
    strmap(int flags = 0);
    virtual ~strmap();
};

inline void     put(strmap& m, const string& key, unknown* obj)     { m.put(key, obj); }
inline unknown* get(const strmap& m, const char* key)               { return m[key]; }
inline void     del(strmap& m, const char* key)                     { m.del(key); }

template <class X> class tstrmap: public strmap
{
public:
    tstrmap(): strmap()  {}
    tstrmap(int iflags): strmap(iflags)  {}
    friend inline X* get(const tstrmap& m, const char* str)         { return (X*)ptypes::get((const strmap&)m, str); }
    friend inline void put(tstrmap& m, const string& str, X* obj)   { unknown* t = obj; ptypes::put(m, str, t); }
    X* operator[] (const char* str) const                           { return (X*)ptypes::get(*this, str); }
};


// ptypes-1.9 textmap interface

inline int   length(const textmap& m)                           { return m.get_count(); }
inline void  clear(textmap& m)                                  { m.clear(); }
inline const string& get(const textmap& m, const string& k)     { return m.get(k); }
inline void  put(textmap& m, const string& k, const string& v)  { m.put(k, v); }
inline void  del(textmap& m, const string& k)                   { m.del(k); }


#endif // PTYPES19_COMPAT


#ifdef _MSC_VER
#pragma warning(pop)
#pragma pack(pop)
#endif


}

#endif // __PTYPES_H__
