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

#include <errno.h>

#ifdef WIN32
#  include <windows.h>
#else
#  include <pthread.h>
#endif

#include "pasync.h"


namespace ptypes {


#ifndef __SEM_TO_TIMEDSEM__


static void sem_fail()
{
    fatal(CRIT_FIRST + 41, "Semaphore failed");
}


semaphore::semaphore(int initvalue) 
{
    if (sem_init(&handle, 0, initvalue) != 0)
        sem_fail();
}


semaphore::~semaphore() 
{
    sem_destroy(&handle);
}


void semaphore::wait() 
{
    int err;
    do {
        err = sem_wait(&handle);
    } while (err == -1 && errno == EINTR);
    if (err != 0)
        sem_fail();
}


void semaphore::post() 
{
    if (sem_post(&handle) != 0)
        sem_fail();
}


#else


int _psemaphore_dummy_symbol;  // avoid ranlib's warning message


#endif



}
