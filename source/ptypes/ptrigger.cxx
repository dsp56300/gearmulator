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
#  include <sys/time.h>
#  include <pthread.h>
#  include <errno.h>
#endif

#include "pasync.h"


namespace ptypes {


static void trig_fail()
{
    fatal(CRIT_FIRST + 41, "Trigger failed");
}



#ifdef WIN32


trigger::trigger(bool autoreset, bool state)
{
    handle = CreateEvent(0, !autoreset, state, 0);
    if (handle == 0)
        trig_fail();
}


#else


inline void trig_syscheck(int r)
{
    if (r != 0)
        trig_fail();
}


trigger::trigger(bool iautoreset, bool istate)
    : state(int(istate)), autoreset(iautoreset)
{
    trig_syscheck(pthread_mutex_init(&mtx, 0));
    trig_syscheck(pthread_cond_init(&cond, 0));
}


trigger::~trigger()
{
    pthread_cond_destroy(&cond);
    pthread_mutex_destroy(&mtx);
}


void trigger::wait()
{
    pthread_mutex_lock(&mtx);
    while (state == 0)
        pthread_cond_wait(&cond, &mtx);
    if (autoreset)
	state = 0;
    pthread_mutex_unlock(&mtx);
} 


void trigger::post()
{
    pthread_mutex_lock(&mtx);
    state = 1;
    if (autoreset)
        pthread_cond_signal(&cond);
    else
        pthread_cond_broadcast(&cond);
    pthread_mutex_unlock(&mtx);
}


void trigger::reset()
{
    state = 0;
}


#endif


}
