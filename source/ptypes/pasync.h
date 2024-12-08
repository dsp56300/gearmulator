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

#ifndef __PASYNC_H__
#define __PASYNC_H__

#ifdef WIN32
#  define _WINSOCKAPI_   // prevent inclusion of winsock.h, since we need winsock2.h
#  include <windows.h>
#else
#  include <pthread.h>
#  ifndef __bsdi__
#    include <semaphore.h>
#  endif
#endif

#ifndef __PPORT_H__
#include "pport.h"
#endif

#ifndef __PTYPES_H__
#include "ptypes.h"
#endif


namespace ptypes {

//
//  Summary of implementation:
//
//  atomic increment/decrement/exchange
//    MSVC/BCC/i386: internal, asm
//    GCC/i386: internal, asm
//    GCC/PowerPC: internal, asm
//    GCC/SPARC: internal, asm
//    Other: internal, mutex hash table
//
//  mutex
//    Win32: Critical section
//    Other: POSIX mutex
//
//  trigger
//    Win32: Event
//    Other: internal, POSIX condvar/mutex
//
//  rwlock:
//    Win32: internal, Event/mutex
//    MacOS: internal, POSIX condvar/mutex
//    Other: POSIX rwlock
//
//  semaphore:
//    Win32: = timedsem
//    MacOS: = timedsem
//    Other: POSIX semaphore
//
//  timedsem (with timed waiting):
//    Win32: Semaphore
//    Other: internal, POSIX mutex/condvar
//


#ifdef _MSC_VER
#pragma pack(push, 4)
#endif


#ifdef WIN32
   typedef int pthread_id_t;
   typedef HANDLE pthread_t;
#else
   typedef pthread_t pthread_id_t;
#endif


ptpublic void ptdecl psleep(uint milliseconds);
ptpublic bool ptdecl pthrequal(pthread_id_t id);  // note: this is NOT the thread handle, use thread::get_id()
ptpublic pthread_id_t ptdecl pthrself();          // ... same


// -------------------------------------------------------------------- //
// --- mutex ---------------------------------------------------------- //
// -------------------------------------------------------------------- //


#ifdef WIN32

struct ptpublic mutex: public noncopyable
{
protected:
    CRITICAL_SECTION critsec;
public:
    mutex()         { InitializeCriticalSection(&critsec); }
    ~mutex()        { DeleteCriticalSection(&critsec); }
    void enter()    { EnterCriticalSection(&critsec); }
    void leave()    { LeaveCriticalSection(&critsec); }
    void lock()     { enter(); }
    void unlock()   { leave(); }
};


#else


struct ptpublic mutex: public noncopyable
{
protected:
    pthread_mutex_t mtx;
public:
    mutex()         { pthread_mutex_init(&mtx, 0); }
    ~mutex()        { pthread_mutex_destroy(&mtx); }
    void enter()    { pthread_mutex_lock(&mtx); }
    void leave()    { pthread_mutex_unlock(&mtx); }
    void lock()     { enter(); }
    void unlock()   { leave(); }
};

#endif


//
// scopelock
//

class scopelock: public noncopyable
{
protected:
    mutex* mtx;
public:
    scopelock(mutex& imtx): mtx(&imtx)  { mtx->lock(); }
    ~scopelock()  { mtx->unlock(); }
};


//
// mutex table for hashed memory locking (undocumented)
//

#define _MUTEX_HASH_SIZE     29      // a prime number for hashing

#ifdef WIN32
#  define pmemlock        mutex
#  define pmementer(m)    (m)->lock()
#  define pmemleave(m)    (m)->unlock()
#else
#  define _MTX_INIT       PTHREAD_MUTEX_INITIALIZER
#  define pmemlock        pthread_mutex_t
#  define pmementer       pthread_mutex_lock
#  define pmemleave       pthread_mutex_unlock
#endif


ptpublic extern pmemlock _mtxtable[_MUTEX_HASH_SIZE];

#define pgetmemlock(addr) (_mtxtable + pintptr(addr) % _MUTEX_HASH_SIZE)


// -------------------------------------------------------------------- //
// --- trigger -------------------------------------------------------- //
// -------------------------------------------------------------------- //


#ifdef WIN32

class ptpublic trigger: public noncopyable
{
protected:
    HANDLE handle;      // Event object
public:
    trigger(bool autoreset, bool state);
    ~trigger()          { CloseHandle(handle); }
    void wait()         { WaitForSingleObject(handle, INFINITE); }
    void post()         { SetEvent(handle); }
    void signal()       { post(); }
    void reset()        { ResetEvent(handle); }
};


#else


class ptpublic trigger: public noncopyable
{
protected:
    pthread_mutex_t mtx;
    pthread_cond_t cond;
    int state;
    bool autoreset;
public:
    trigger(bool autoreset, bool state);
    ~trigger();
    void wait();
    void post();
    void signal()  { post(); }
    void reset();
};

#endif


// -------------------------------------------------------------------- //
// --- rwlock --------------------------------------------------------- //
// -------------------------------------------------------------------- //


#if defined(WIN32) || defined(__DARWIN__) || defined(__bsdi__)
#  define __PTYPES_RWLOCK__
#elif defined(linux)
   // on Linux rwlocks are included only with -D_GNU_SOURCE.
   // programs that don't use rwlocks, do not need to define
   // _GNU_SOURCE either.
#  if defined(_GNU_SOURCE) || defined(__USE_UNIX98)
#    define __POSIX_RWLOCK__
#  endif
#else
#  define __POSIX_RWLOCK__
#endif


#ifdef __PTYPES_RWLOCK__

struct ptpublic rwlock: protected mutex
{
protected:
#ifdef WIN32
    HANDLE  reading;    // Event object
    HANDLE  finished;   // Event object
    int     readcnt;
    int     writecnt;
#else
    pthread_mutex_t mtx;
    pthread_cond_t readcond;
    pthread_cond_t writecond;
    int locks;
    int writers;
    int readers;
#endif
public:
    rwlock();
    ~rwlock();
    void rdlock();
    void wrlock();
    void unlock();
    void lock()     { wrlock(); }
};


#elif defined(__POSIX_RWLOCK__)


struct ptpublic rwlock: public noncopyable
{
protected:
    pthread_rwlock_t rw;
public:
    rwlock();
    ~rwlock()       { pthread_rwlock_destroy(&rw); }
    void rdlock()   { pthread_rwlock_rdlock(&rw); }
    void wrlock()   { pthread_rwlock_wrlock(&rw); }
    void unlock()   { pthread_rwlock_unlock(&rw); }
    void lock()     { wrlock(); }
};

#endif


#if defined(__PTYPES_RWLOCK__) || defined(__POSIX_RWLOCK__)

//
// scoperead & scopewrite
//

class scoperead: public noncopyable
{
protected:
    rwlock* rw;
public:
    scoperead(rwlock& irw): rw(&irw)  { rw->rdlock(); }
    ~scoperead()  { rw->unlock(); }
};


class scopewrite: public noncopyable
{
protected:
    rwlock* rw;
public:
    scopewrite(rwlock& irw): rw(&irw)  { rw->wrlock(); }
    ~scopewrite()  { rw->unlock(); }
};


#endif


// -------------------------------------------------------------------- //
// --- semaphore ------------------------------------------------------ //
// -------------------------------------------------------------------- //


#if defined(WIN32) || defined(__DARWIN__) || defined(__bsdi__)
#  define __SEM_TO_TIMEDSEM__
#endif


#ifdef __SEM_TO_TIMEDSEM__

// map ordinary semaphore to timed semaphore

class timedsem;
typedef timedsem semaphore;


#else


class ptpublic semaphore: public unknown
{
protected:
    sem_t handle;
public:
    semaphore(int initvalue);
    virtual ~semaphore();

    void wait();
    void post();
    void signal()  { post(); }

	void decrement()				{ return wait(); }
	void increment()				{ post(); }
};

#endif


class ptpublic timedsem: public unknown
{
protected:
#ifdef WIN32
    HANDLE handle;
#else
    int count;
    pthread_mutex_t mtx;
    pthread_cond_t cond;
#endif
public:
    timedsem(int initvalue);
    virtual ~timedsem();
    bool wait(int msecs = -1);
    void post();
    void signal()  { post(); }

	bool decrement(int msec = -1)	{ return wait(msec); }
	void increment()				{ post(); }
};


// -------------------------------------------------------------------- //
// --- thread --------------------------------------------------------- //
// -------------------------------------------------------------------- //


class ptpublic thread: public unknown
{
protected:
#ifdef WIN32
    unsigned id;
#endif
    pthread_t  handle;
    int  autofree;
    int  running;
    int  signaled;
    int  finished;
    int  freed;
    int  reserved;   // for priorities
    timedsem relaxsem;

    virtual void execute() = 0;
    virtual void cleanup();

    bool relax(int msecs) { return relaxsem.wait(msecs); }

    friend void _threadepilog(thread* thr);

#ifdef WIN32
    friend unsigned __stdcall _threadproc(void* arg);
#else
    friend void* _threadproc(void* arg);
#endif

public:
    thread(bool iautofree);
    virtual ~thread();

#ifdef WIN32
    pthread_id_t get_id()   { return int(id); }
#else
    pthread_id_t get_id()   { return handle; }
#endif

    bool get_running()    { return running != 0; }
    bool get_finished()   { return finished != 0; }
    bool get_signaled()   { return signaled != 0; }

    void start();
    void signal();
    void waitfor();
};



// -------------------------------------------------------------------- //
// --- jobqueue & msgqueue -------------------------------------------- //
// -------------------------------------------------------------------- //


const int MSG_USER = 0;
const int MSG_QUIT = -1;

const int DEF_QUEUE_LIMIT = 5000;

class ptpublic message: public unknown
{
protected:
    message* next;          // next in the message chain, used internally
    semaphore* sync;        // used internally by msgqueue::send(), when called from a different thread
    friend class jobqueue;  // my friends, job queue and message queue...
    friend class msgqueue;
public:
    int id;
    pintptr param;
    pintptr result;
    message(int iid, pintptr iparam = 0);
    virtual ~message();
};


class ptpublic jobqueue: public noncopyable
{
private:
    int       limit;        // queue limit
    message*  head;         // queue head
    message*  tail;         // queue tail
    int       qcount;       // number of items in the queue
    timedsem  sem;          // queue semaphore
    timedsem  ovrsem;       // overflow semaphore
    mutex     qlock;        // critical sections in enqueue and dequeue

protected:
    bool enqueue(message* msg, int timeout = -1);
    bool push(message* msg, int timeout = -1);
    message* dequeue(bool safe = true, int timeout = -1);
    void purgequeue();

public:
    jobqueue(int ilimit = DEF_QUEUE_LIMIT);
    virtual ~jobqueue();

    int  get_count() const  { return qcount; }
    int  get_limit() const  { return limit; }

    bool post(message* msg, int timeout = -1);
    bool post(int id, pintptr param = 0, int timeout = -1);
    bool posturgent(message* msg, int timeout = -1);
    bool posturgent(int id, pintptr param = 0, int timeout = -1);
    message* getmessage(int timeout = -1);

#ifdef PTYPES19_COMPAT
    int  msgsavail() const  { return get_count(); }
#endif
};


template <class T> class tjobqueue: protected jobqueue
{
public:
    tjobqueue(int ilimit = DEF_QUEUE_LIMIT);

    int  get_count() const                      { return jobqueue::get_count(); }
    int  get_limit() const                      { return jobqueue::get_limit(); }
    bool post(T* msg, int timeout = -1)         { return jobqueue::post(msg, timeout); }
    bool posturgent(T* msg, int timeout = -1)   { return jobqueue::posturgent(msg, timeout); }
    T*   getmessage(int timeout = -1)           { return (T*)jobqueue::getmessage(timeout); }
};


class ptpublic msgqueue: protected jobqueue
{
private:
    mutex thrlock;          // lock for the queue processing
    pthread_id_t owner;     // thread ID of the queue processing thread

    pintptr finishmsg(message* msg);
    void handlemsg(message* msg);
    void takeownership();

protected:
    bool quit;

    void defhandler(message& msg);
    virtual void msghandler(message& msg) = 0;

public:
    msgqueue(int ilimit = DEF_QUEUE_LIMIT);
    virtual ~msgqueue();

    // functions calling from the owner thread:
    void processone();  // process one message, may hang if no msgs in the queue
    void processmsgs(); // process all available messages and return
    void run();         // process messages until MSG_QUIT

    // functions calling from any thread:
    int  get_count() const                                        { return jobqueue::get_count(); }
    int  get_limit() const                                        { return jobqueue::get_limit(); }
    bool post(message* msg, int timeout = -1)                     { return jobqueue::post(msg, timeout); }
    bool post(int id, pintptr param = 0, int timeout = -1)        { return jobqueue::post(id, param, timeout); }
    bool posturgent(message* msg, int timeout = -1)               { return jobqueue::posturgent(msg, timeout); }
    bool posturgent(int id, pintptr param = 0, int timeout = -1)  { return jobqueue::posturgent(id, param, timeout); }
    pintptr send(message* msg);
    pintptr send(int id, pintptr param = 0);

#ifdef PTYPES19_COMPAT
    int  msgsavail() const  { return get_count(); }
#endif
};


#ifdef _MSC_VER
#pragma pack(pop)
#endif


}

#endif // __PASYNC_H__
