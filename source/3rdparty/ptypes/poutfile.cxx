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

#include "errno.h"

#ifdef WIN32
#  include <windows.h>
#else
#  include <fcntl.h>
#  include <unistd.h>
#endif

#include "pstreams.h"


namespace ptypes {


// *BSD hack
#ifndef O_LARGEFILE
#  define O_LARGEFILE 0
#endif


outfile::outfile()
    : outstm(), filename(), syshandle(invhandle), peerhandle(invhandle),
      umode(0644), append(false)  {}


outfile::outfile(const char* ifn, bool iappend)
    : outstm(), filename(ifn), syshandle(invhandle), peerhandle(invhandle),
      umode(0644), append(iappend)  {}


outfile::outfile(string const& ifn, bool iappend)
    : outstm(), filename(ifn), syshandle(invhandle), peerhandle(invhandle),
      umode(0644), append(iappend)  {}


outfile::~outfile() 
{
    close();
}


int outfile::classid()
{
    return CLASS2_OUTFILE;
}


string outfile::get_streamname() 
{
    return filename;
}


void outfile::doopen() 
{
    if (syshandle != invhandle)
        handle = syshandle;
    else
    {
#ifdef WIN32
        SECURITY_ATTRIBUTES sa;
        sa.nLength = sizeof(SECURITY_ATTRIBUTES);
        sa.lpSecurityDescriptor = NULL;
        sa.bInheritHandle = TRUE;

        handle = int(CreateFileA(filename, GENERIC_WRITE,
            FILE_SHARE_READ | FILE_SHARE_WRITE, &sa, 
            (append ? OPEN_ALWAYS : CREATE_ALWAYS), 0, 0));
#else
        handle = ::open(filename, 
            O_WRONLY | O_CREAT | O_LARGEFILE | (append ? 0 : O_TRUNC), umode);
#endif
        if (handle == invhandle)
            error(uerrno(), "Couldn't open");
        if (append)
            if (doseek(0, IO_END) == -1)
                error(uerrno(), "Couldn't seek to end of file");
    }
}


void outfile::flush()
{
    outstm::flush();
#ifdef WIN32
    FlushFileBuffers(HANDLE(handle));
#endif
}


void outfile::doclose()
{
    outstm::doclose();
    syshandle = invhandle;
    peerhandle = invhandle;
}


}
