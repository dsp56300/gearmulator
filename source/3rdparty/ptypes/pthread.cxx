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
#  include <process.h>
#else
#  include <pthread.h>
#endif

#include "pasync.h"


namespace ptypes {


thread::thread(bool iautofree)
    :
#ifdef WIN32
    id(0),
#endif
    handle(0), autofree(iautofree),
    running(0), signaled(0), finished(0), freed(0),
    reserved(0), relaxsem(0)
{
}


thread::~thread()
{
    if (pexchange(&freed, 1) != 0)
        return;
#ifdef WIN32
    if (autofree)
        // MSDN states this is not necessary, however, without closing
        // the handle debuggers show an obvious handle leak here
        CloseHandle(handle);
#else
    // though we require non-autofree threads to always call waitfor(),
    // the statement below is provided to cleanup thread resources even
    // if waitfor() was not called.
    if (!autofree && running)
        pthread_detach(handle);
#endif
}


void thread::signal()
{
    if (pexchange(&signaled, 1) == 0)
        relaxsem.post();
}


void thread::waitfor()
{
    if (pexchange(&freed, 1) != 0)
        return;
    if (pthrequal(get_id()))
        fatal(CRIT_FIRST + 47, "Can not waitfor() on myself");
    if (autofree)
        fatal(CRIT_FIRST + 48, "Can not waitfor() on an autofree thread");
#ifdef WIN32
    WaitForSingleObject(handle, INFINITE);
    CloseHandle(handle);
#else
    pthread_join(handle, nil);
//  detaching after 'join' is not required (or even do harm on some systems)
//  except for HPUX. we don't support HPUX yet.
//    pthread_detach(handle);
#endif
    handle = 0;
}


#ifdef WIN32
unsigned _stdcall _threadproc(void* arg)
{
#else
void* _threadproc(void* arg) 
{
#endif
    thread* thr = (thread*)arg;
#ifndef WIN32
    if (thr->autofree)
        // start() does not assign the handle for autofree threads
        thr->handle = pthread_self();
#endif
    try 
    {
        thr->execute();
    }
    catch(exception*)
    {
        _threadepilog(thr);
        throw;
    }
    _threadepilog(thr);
    return 0;
}


void _threadepilog(thread* thr)
{
    try
    {
        thr->cleanup();
    }
    catch(exception* e)
    {
        delete e;
    }
    pexchange(&thr->finished, 1);
    if (thr->autofree)
        delete thr;
}


void thread::start()
{
    if (pexchange(&running, 1) == 0)
    {
#ifdef WIN32
        handle = (HANDLE)_beginthreadex(nil, 0, _threadproc, this, 0, &id);
        if (handle == 0)
            fatal(CRIT_FIRST + 40, "CreateThread() failed");
#else
        pthread_t temp_handle;
        pthread_attr_t attr;
        pthread_attr_init(&attr);
        pthread_attr_setdetachstate(&attr, 
            autofree ? PTHREAD_CREATE_DETACHED : PTHREAD_CREATE_JOINABLE);
        if (pthread_create(autofree ? &temp_handle : &handle,
                &attr, _threadproc, this) != 0)
            fatal(CRIT_FIRST + 40, "pthread_create() failed");
        pthread_attr_destroy(&attr);
#endif
    }
}


void thread::cleanup()
{
}


}
