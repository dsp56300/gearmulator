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
#include <limits.h>

#ifdef WIN32
#  include <windows.h>
#else
#  include <signal.h>
#  include <unistd.h>
#endif

#include "pstreams.h"


namespace ptypes {


/*

Known UNIX error codes:

EPERM         1          Not owner
ENOENT        2          No such file or directory
ESRCH         3          No such process
EINTR         4          Interrupted system call
EIO           5          I/O error
ENXIO         6          No such device or address
E2BIG         7          Argument list too long
ENOEXEC       8          Exec format error
EBADF         9          Bad file number
ECHILD       10          No spawned processes
EAGAIN       11          No more processes; not enough memory; maximum nesting level reached
ENOMEM       12          Not enough memory
EACCES       13          Permission denied
EFAULT       14          Bad address
ENOTBLK      15          Block device required
EBUSY        16          Mount device busy
EEXIST       17          File exists
EXDEV        18          Cross-device link
ENODEV       19          No such device
ENOTDIR      20          Not a directory
EISDIR       21          Is a directory
EINVAL       22          Invalid argument
ENFILE       23          File table overflow
EMFILE       24          Too many open files
ENOTTY       25          Not a teletype
ETXTBSY      26          Text file busy
EFBIG        27          File too large
ENOSPC       28          No space left on device
ESPIPE       29          Illegal seek
EROFS        30          Read-only file system
EMLINK       31          Too many links
EPIPE        32          Broken pipe
EDOM         33          Math argument
ERANGE       34          Result too large
EUCLEAN      35          File system needs cleaning
EDEADLK      36          Resource deadlock would occur
EDEADLOCK    36          Resource deadlock would occur

*/


#ifndef WIN32

static class _io_init
{
public:
    _io_init();
} _io_init_inst;


_io_init::_io_init()
{
    // We don't like broken pipes. PTypes will throw an exception instead.
    signal(SIGPIPE, SIG_IGN);
}

#endif



int ptdecl unixerrno() 
{
#ifdef WIN32
    switch(GetLastError()) 
    {
    case ERROR_FILE_NOT_FOUND:
    case ERROR_PATH_NOT_FOUND:      return ENOENT;
    case ERROR_TOO_MANY_OPEN_FILES: return EMFILE;
    case ERROR_ACCESS_DENIED:
    case ERROR_SHARING_VIOLATION:   return EACCES;
    case ERROR_INVALID_HANDLE:      return EBADF;
    case ERROR_NOT_ENOUGH_MEMORY:
    case ERROR_OUTOFMEMORY:         return ENOMEM;
    case ERROR_INVALID_DRIVE:       return ENODEV;
    case ERROR_WRITE_PROTECT:       return EROFS;
    case ERROR_FILE_EXISTS:         return EEXIST;
    case ERROR_BROKEN_PIPE:         return EPIPE;
    case ERROR_DISK_FULL:           return ENOSPC;
    case ERROR_SEEK_ON_DEVICE:      return ESPIPE;
    default: return EIO;
    }
#else
    return errno;
#endif
}


//
// This function gives error messages for most frequently occurring 
// IO errors. If the function returns NULL a generic message
// can be given, e.g. "I/O error". See also iobase::get_errormsg()
//

const char* ptdecl unixerrmsg(int code)
{
    switch(code) 
    {
    case EBADF:  return "Invalid file descriptor";
    case ESPIPE: return "Can not seek on this device";
    case ENOENT: return "No such file or directory";
    case EMFILE: return "Too many open files";
    case EACCES: return "Access denied";
    case ENOMEM: return "Not enough memory";
    case ENODEV: return "No such device";
    case EROFS:  return "Read-only file system";
    case EEXIST: return "File already exists";
    case ENOSPC: return "Disk full";
    case EPIPE:  return "Broken pipe";
    case EFBIG:  return "File too large";
    default: return nil;
    }
}


estream::estream(iobase* ierrstm, int icode, const char* imsg)
    : exception(imsg), code(icode), errstm(ierrstm) {}


estream::estream(iobase* ierrstm, int icode, const string& imsg)
    : exception(imsg), code(icode), errstm(ierrstm) {}


estream::~estream() {}


int defbufsize = 8192;
int stmbalance = 0;

iobase::iobase(int ibufsize)
    : component(), active(false), cancelled(false), eof(true), 
      handle(invhandle), abspos(0), bufsize(0), bufdata(nil), bufpos(0), bufend(0),
      stmerrno(0), deferrormsg(), status(IO_CREATED), onstatus(nil) 
{
    if (ibufsize < 0)
        bufsize = defbufsize;
    else
        bufsize = ibufsize;
}


iobase::~iobase() 
{
}


void iobase::bufalloc() 
{
    if (bufdata != nil)
        fatal(CRIT_FIRST + 13, "(ptypes internal) invalid buffer allocation");
    bufdata = (char*)memalloc(bufsize);
}


void iobase::buffree() 
{
    bufclear();
    memfree(bufdata);
    bufdata = 0;
}


void iobase::chstat(int newstat) 
{
    status = newstat;
    if (onstatus != nil)
        (*onstatus)(this, newstat);
}


void iobase::errstminactive() 
{
    error(EIO, "Stream inactive");
}


void iobase::errbufrequired()
{
    fatal(CRIT_FIRST + 11, "Internal: buffer required");
}


int iobase::convertoffset(large offs)
{
    if (offs < 0 || offs > INT_MAX)
        error(EFBIG, "File offset value too large");
    return (int)offs;
}


void iobase::open() 
{
    cancel();
    chstat(IO_OPENING);
    abspos = 0;
    cancelled = false;
    eof = false;
    stmerrno = 0;
    clear(deferrormsg);
    active = true;
    stmbalance++;
    bufalloc();
    doopen();
    chstat(IO_OPENED);
}


void iobase::close() 
{
    if (!active)
        return;
    stmbalance--;
    try 
    {
        if (bufdata != 0 && !cancelled)
            flush();
        doclose();
    }
    catch(estream* e) 
    {
        delete e;
    }
    buffree();
    active = false;
    eof = true;
    chstat(IO_CLOSED);
}


void iobase::cancel() 
{
    cancelled = true;
    close();
}


large iobase::seekx(large newpos, ioseekmode mode) 
{
    if (!active)
        errstminactive();
    flush();
    large ret = doseek(newpos, mode);
    if (ret < 0)
        error(ESPIPE, "Seek failed");
    bufclear();
    eof = false;
    abspos = ret;
    return ret;
}


void iobase::flush() 
{
}


large iobase::doseek(large newpos, ioseekmode mode)
{
    if (handle == invhandle)
    {
        error(ESPIPE, "Can't seek on this device");
        return -1;
    }
#ifdef WIN32
    static int wmode[3] = {FILE_BEGIN, FILE_CURRENT, FILE_END};
    LARGE_INTEGER li;
    li.QuadPart = newpos;
    li.LowPart = SetFilePointer(HANDLE(handle), li.LowPart, &li.HighPart, wmode[mode]);
    if (li.LowPart == INVALID_SET_FILE_POINTER && GetLastError() != NO_ERROR)
        return -1;
    return li.QuadPart;
#else
    static int umode[3] = {SEEK_SET, SEEK_CUR, SEEK_END};
    return lseek(handle, newpos, umode[mode]);
#endif
}


void iobase::doclose()
{
#ifdef WIN32
    CloseHandle(HANDLE(pexchange(&handle, invhandle)));
#else
    ::close(pexchange(&handle, invhandle));
#endif
}


void iobase::set_active(bool newval) 
{
    if (newval != active)
	{
        if (newval)
            open();
        else
            close();
	}
}


void iobase::set_bufsize(int newval) 
{
    if (active)
        fatal(CRIT_FIRST + 12, "Cannot change buffer size while stream is active");
    if (newval < 0)
        bufsize = defbufsize;
    else
        bufsize = newval;
}


string iobase::get_errstmname() 
{
    return get_streamname();
}


const char* iobase::uerrmsg(int code)
{
    return unixerrmsg(code);
}


int iobase::uerrno()
{
    return unixerrno();
}


string iobase::get_errormsg() 
{
    string s = uerrmsg(stmerrno);
    if (isempty(s))
        s = deferrormsg;
    if (pos('[', s) >= 0 && *(pconst(s) + length(s) - 1) == ']')
        return s;
    string e = get_errstmname();
    if (isempty(e))
        return s;
    return s + " [" + e + ']';
}


#ifdef _MSC_VER
// disable "unreachable code" warning for throw (known compiler bug)
#  pragma warning (disable: 4702)
#endif

void iobase::error(int code, const char* defmsg) 
{
    eof = true;
    stmerrno = code;
    deferrormsg = defmsg;
    throw new estream(this, code, get_errormsg());
}


}
