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

#include "pport.h"
#include "pstreams.h"

#ifdef WIN32
#  include <windows.h>
#else
#  include <unistd.h>
#endif


namespace ptypes {

infile   pin;
logfile  pout;
logfile  perr;
outnull  pnull;


static class _stdio_init
{
public:
    _stdio_init();
} _stdio_init_inst;


#ifdef WIN32

static HANDLE DuplicateSysHandle(DWORD stdh)
{
    HANDLE hold = GetStdHandle(stdh);
    HANDLE hnew = 0;
    DuplicateHandle(GetCurrentProcess(), hold, GetCurrentProcess(), 
        &hnew, 0, true, DUPLICATE_SAME_ACCESS);
    return hnew;
}

#endif


_stdio_init::_stdio_init()
{
#ifdef WIN32
    pin.set_syshandle(int(DuplicateSysHandle(STD_INPUT_HANDLE)));
    pout.set_syshandle(int(DuplicateSysHandle(STD_OUTPUT_HANDLE)));
    perr.set_syshandle(int(DuplicateSysHandle(STD_ERROR_HANDLE)));
#else
    pin.set_syshandle(::dup(STDIN_FILENO));
    pout.set_syshandle(::dup(STDOUT_FILENO));
    perr.set_syshandle(::dup(STDERR_FILENO));
#endif

    pin.set_bufsize(4096);
    pin.open();
    pout.open();
    perr.open();

    pnull.open();

    // prevent others from freeing these objects, if assigned to a variant.
    // will need to handle reference counting for static objects better. any ideas?
    addref(&pin);
    addref(&pout);
    addref(&perr);
    addref(&pnull);

    // this is to show objalloc = 0 at program exit
    objalloc -= 4;
}


//
// null output stream
//


outnull::outnull()
    : outstm(0)
{
}


outnull::~outnull()
{
    close();
}


int outnull::dorawwrite(const char*, int)
{
    return 0;
}


void outnull::doopen()
{
}


void outnull::doclose()
{
}


string outnull::get_streamname()
{
    return "<null>";
}


//
// logfile - file output with thread-safe putf()
//

logfile::logfile(): outfile()
{
    set_bufsize(0);
}


logfile::logfile(const char* ifn, bool iappend): outfile(ifn, iappend)
{
    set_bufsize(0);
}


logfile::logfile(const string& ifn, bool iappend): outfile(ifn, iappend)
{
    set_bufsize(0);
}


logfile::~logfile()
{
}


int logfile::classid()
{
    return CLASS3_LOGFILE;
}


}

