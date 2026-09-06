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
#else
#  include <pthread.h>
#endif

#include "pasync.h"


namespace ptypes {


static void rwlock_fail()
{
    fatal(CRIT_FIRST + 41, "rwlock failed");
}


inline void rwlock_syscheck(int r)
{
    if (r != 0)
        rwlock_fail();
}


#ifdef __PTYPES_RWLOCK__


#  ifdef WIN32

//
// this implementation of the read/write lock is derived
// from Apache Portable Runtime (APR) source, which 
// in turn was originally based on an erroneous (or just
// incomplete?) example in one of the MSDN technical articles.
// 

rwlock::rwlock()
    : mutex(), readcnt(-1), writecnt(0)
{
    reading = CreateEvent(0, true, false, 0);
    finished = CreateEvent(0, false, true, 0);
    if (reading == 0 || finished == 0)
        rwlock_fail();
}


rwlock::~rwlock()
{
    CloseHandle(reading);
    CloseHandle(finished);
}


void rwlock::rdlock()
{
    if (pincrement(&readcnt) == 0) 
    {
        WaitForSingleObject(finished, INFINITE);
        SetEvent(reading);
    }
    WaitForSingleObject(reading, INFINITE);
}


void rwlock::wrlock()
{
    mutex::enter();
    WaitForSingleObject(finished, INFINITE);
    writecnt++;
}


void rwlock::unlock()
{
    if (writecnt != 0) 
    {
        writecnt--;
        SetEvent(finished);
        mutex::leave();
    } 
    else if (pdecrement(&readcnt) < 0) 
    {
        ResetEvent(reading);
        SetEvent(finished);
    } 
}


#  else	  // !defined(WIN32)

//
// for other platforms that lack POSIX rwlock we implement
// the rwlock object using POSIX condvar. the code below
// is based on Sean Burke's algorithm posted in 
// comp.programming.threads.
//


rwlock::rwlock()
    : locks(0), writers(0), readers(0)
{
    rwlock_syscheck(pthread_mutex_init(&mtx, 0));
    rwlock_syscheck(pthread_cond_init(&readcond, 0));
    rwlock_syscheck(pthread_cond_init(&writecond, 0));
}


rwlock::~rwlock()
{
    pthread_cond_destroy(&writecond);
    pthread_cond_destroy(&readcond);
    pthread_mutex_destroy(&mtx);
}


void rwlock::rdlock()
{
    pthread_mutex_lock(&mtx);
    readers++;
    while (locks < 0)
        pthread_cond_wait(&readcond, &mtx);
    readers--;
    locks++;
    pthread_mutex_unlock(&mtx);
}


void rwlock::wrlock()
{
    pthread_mutex_lock(&mtx);
    writers++;
    while (locks != 0)
        pthread_cond_wait(&writecond, &mtx);
    locks = -1;
    writers--;
    pthread_mutex_unlock(&mtx);
}


void rwlock::unlock()
{
    pthread_mutex_lock(&mtx);
    if (locks > 0)
    {
        locks--;
        if (locks == 0)
            pthread_cond_signal(&writecond);
    }
    else
    {
        locks = 0;
        if (readers != 0)
            pthread_cond_broadcast(&readcond);
        else
            pthread_cond_signal(&writecond);
    }
    pthread_mutex_unlock(&mtx);
}


#  endif    // !defined(WIN32)


#else	// !defined(__PTYPES_RWLOCK__)

//
// for other systems we declare a fully-inlined rwlock
// object in pasync.h
//

rwlock::rwlock()
{
    rwlock_syscheck(pthread_rwlock_init(&rw, 0));
}


#endif



}
