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

#ifdef WIN32
#  include <windows.h>
#endif

#include "ptypes.h"
#include "pasync.h"     // for pmemlock*

#include <assert.h>

namespace ptypes {


#ifdef PTYPES_ST
// single-threaded version


int __PFASTCALL pexchange(int* target, int value)
{
    int r = *target;
    *target = value;
    return r;
}


void* __PFASTCALL pexchange(void** target, void* value)
{
    void* r = *target;
    *target = value;
    return r;
}


int __PFASTCALL pincrement(int* target)
{
    return ++(*target);
}


int __PFASTCALL pdecrement(int* target)
{
    return --(*target);
}


#else
// multi-threaded version

#if defined(__GNUC__) && (defined(__i386__) || defined(__I386__))
#  define GCC_i386
#elif defined(__GNUC__) && defined(__ppc__)
#  define GCC_PPC
#elif defined(_MSC_VER) && defined(_M_IX86)
#  define MSC_i386
#elif defined(_MSC_VER) && (defined(_M_IA64) || defined(_WIN64))
#  define MSC_x64
#elif defined(__BORLANDC__) && defined(_M_IX86)
#  define BCC_i386
#elif defined(__GNUC__) && defined(__sparc__) && !defined(__arch64__)
#  define GCC_sparc
#endif


#if defined(MSC_i386) || defined(BCC_i386)

//
// atomic operations for Microsoft C or Borland C on Windows
//

#if defined(_MSC_VER)
#  pragma warning (disable: 4035)
#elif defined(__BORLANDC__)
#  pragma warn -rvl
#endif

// !!! NOTE
// the following functions implement atomic exchange/inc/dec on
// windows. they are dangerous in that they rely on the calling
// conventions of MSVC and BCC. the first one passes the first
// two arguments in ECX and EDX, and the second one - in EAX and 
// EDX.

int __PFASTCALL pincrement(int* v)
{
	return InterlockedIncrement((LONG*)v);
}


int __PFASTCALL pdecrement(int* v)
{
	return InterlockedDecrement((LONG*)v);
}


int __PFASTCALL pexchange(int* a, int b)
{
	return InterlockedExchange((LONG*)a,b);
}


void* __PFASTCALL pexchange(void** a, void* b)
{
#ifdef _WIN64
	return InterlockedExchange64( a,b);
#else
	return (void*)InterlockedExchange((LONG*)a,(LONG)b);
#endif
}


#elif defined(GCC_i386)

//
// GNU C compiler on any i386 platform (actually 486+ for xadd)
//

int pexchange(int* target, int value)
{
    __asm__ __volatile ("lock ; xchgl (%1),%0" : "+r" (value) : "r" (target));
    return value;
}


void* pexchange(void** target, void* value)
{
    __asm__ __volatile ("lock ; xchgl (%1),%0" : "+r" (value) : "r" (target));
    return value;
}


int pincrement(int* target)
{
    int temp = 1;
    __asm__ __volatile ("lock ; xaddl %0,(%1)" : "+r" (temp) : "r" (target));
    return temp + 1;
}


int pdecrement(int* target)
{
    int temp = -1;
    __asm__ __volatile ("lock ; xaddl %0,(%1)" : "+r" (temp) : "r" (target));
    return temp - 1;
}


#elif defined(GCC_PPC)

//
// GNU C compiler on any PPC platform
//

int pexchange(int* target, int value)
{
    int temp;
    __asm__ __volatile (
"1: lwarx  %0,0,%1\n\
	stwcx. %2,0,%1\n\
	bne-   1b\n\
	isync"
	: "=&r" (temp)
	: "r" (target), "r" (value)
	: "cc", "memory"
	);
    return temp;
}


void* pexchange(void** target, void* value)
{
    void* temp;
    __asm__ __volatile (
"1: lwarx  %0,0,%1\n\
	stwcx. %2,0,%1\n\
	bne-   1b\n\
	isync"
	: "=&r" (temp)
	: "r" (target), "r" (value)
	: "cc", "memory"
	);
    return temp;
}


int pincrement(int* target)
{
    int temp;
    __asm__ __volatile (
"1: lwarx  %0,0,%1\n\
	addic  %0,%0,1\n\
	stwcx. %0,0,%1\n\
	bne-   1b\n\
	isync"
	: "=&r" (temp)
	: "r" (target)
	: "cc", "memory"
	);
    return temp;
}


int pdecrement(int* target)
{
    int temp;
    __asm__ __volatile (
"1: lwarx  %0,0,%1\n\
	addic  %0,%0,-1\n\
	stwcx. %0,0,%1\n\
	bne-   1b\n\
	isync"
	: "=&r" (temp)
	: "r" (target)
	: "cc", "memory"
	);
    return temp;
}


#elif defined GCC_sparc

//
// GNU C compiler on SPARC in 32-bit mode (pointers are 32-bit)
//

// assembly routines defined in patomic.sparc.s
// we currently don't use CAS in the library, but let it be there
extern "C" {
    int __patomic_add(volatile int* __mem, int __val);
    int __patomic_swap(volatile int* __mem, int __val);
    int __patomic_cas(volatile int* __mem, int __expected, int __newval);
}

#define __patomic_swap_p(mem,val) \
    (void*)(__patomic_swap((int*)(mem), (int)(val)))


int pexchange(int* target, int value)
{
    return __patomic_swap(target, value);
}


void* pexchange(void** target, void* value)
{
    return __patomic_swap_p(target, value);
}


int pincrement(int* target)
{
    return __patomic_add(target, 1);
}


int pdecrement(int* target)
{
    return __patomic_add(target, -1);
}

#elif defined __EMSCRIPTEN__
#include <emscripten/threading.h>
int pexchange(int* target, int value)
{
	return emscripten_atomic_exchange_u32(target, value);
}


void* pexchange(void** target, void* value)
{
	return (void*)emscripten_atomic_exchange_u32(*target, (uint32_t)value);
}


int pincrement(int* target)
{
	return emscripten_atomic_add_u32(target, 1);
}


int pdecrement(int* target)
{
	return emscripten_atomic_add_u32(target, -1);
}

#elif defined MSC_x64

int pexchange(int* target, int value)
{
	assert( sizeof(target) == sizeof(volatile LONG*) );

	return InterlockedExchange((volatile LONG*)target,value);
}


void* pexchange(void** target, void* value)
{
	return InterlockedExchangePointer(target,value);
}

int pincrement(int* target)
{
	assert( sizeof(target) == sizeof(volatile LONG*) );
	return InterlockedIncrement((volatile LONG*)target);
}


int pdecrement(int* target)
{
	assert( sizeof(target) == sizeof(volatile LONG*) );
	return InterlockedDecrement((volatile LONG*)target);
}


#else

//
// other platforms: mutex locking
//

int pexchange(int* target, int value)
{
    pmemlock* m = pgetmemlock(target);
    pmementer(m);
    int r = *target;
    *target = value;
    pmemleave(m);
    return r;
}


void* pexchange(void** target, void* value)
{
    pmemlock* m = pgetmemlock(target);
    pmementer(m);
    void* r = *target;
    *target = value;
    pmemleave(m);
    return r;
}

int pincrement(int* target)
{
	assert( NULL != _mtxtable );
    pmemlock* m = pgetmemlock(target);
    pmementer(m);
    int r = ++(*target);
    pmemleave(m);
    return r;
}


int pdecrement(int* target)
{
    pmemlock* m = pgetmemlock(target);
    pmementer(m);
    int r = --(*target);
    pmemleave(m);
    return r;
}

#endif


#endif


}
