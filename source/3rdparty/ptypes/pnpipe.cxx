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
#  include <sys/types.h>
#  include <sys/socket.h>
#  include <sys/un.h>
#endif

#include "pstreams.h"


namespace ptypes {


#ifdef WIN32

string ptdecl namedpipe::realpipename(const string& pipename, const string& svrname)
{
    if (isempty(pipename))
        return nullstring;
    string realname = pipename;
    if (*pconst(pipename) == '/')
    {
        int i = rpos('/', realname);
        del(realname, 0, i + 1);
    }
    string s;
    if (isempty(svrname))
        s = '.';
    else
        s = svrname;
    return "\\\\" + s + "\\pipe\\" + realname;
}


#else

string ptdecl namedpipe::realpipename(const string& pipename)
{
    if (isempty(pipename))
        return nullstring;
    if (*pconst(pipename) == '/')
        return pipename;
    else
        return DEF_NAMED_PIPES_DIR + pipename;
}


bool namedpipe::setupsockaddr(const string& pipename, void* isa)
{
    sockaddr_un* sa = (sockaddr_un*)isa;
    memset(sa, 0, sizeof(sockaddr_un));
    sa->sun_family = AF_UNIX;
#ifdef __FreeBSD__
    sa->sun_len = length(pipename);
#endif

    // copy the path name into the sockaddr structure, 108 chars max (?)
    if (length(pipename) + 1 > (int)sizeof(sa->sun_path))
        return false;
    strcpy(sa->sun_path, pipename);
    return true;
}

#endif


namedpipe::namedpipe()
    : fdxstm(), pipename(), svhandle(invhandle)
{
    initovr();
}


namedpipe::namedpipe(const string& ipipename)
    : fdxstm(), pipename(), svhandle(invhandle)
{
    pipename = realpipename(ipipename);
    initovr();
}


#ifdef WIN32

namedpipe::namedpipe(const string& ipipename, const string& servername)
    : fdxstm(), pipename(), svhandle(invhandle)
{
    pipename = realpipename(ipipename, servername);
    initovr();
}


void namedpipe::initovr()
{
    ovr.hEvent = CreateEvent(0, false, false, 0);
}


#endif


namedpipe::~namedpipe()
{
    cancel();
#ifdef WIN32
    CloseHandle(ovr.hEvent);
#endif
}


int namedpipe::classid()
{
    return CLASS3_NPIPE;
}


string namedpipe::get_streamname()
{
    return pipename;
}


void namedpipe::set_pipename(const string& newvalue)
{
    close();
    pipename = realpipename(newvalue);
}


void namedpipe::set_pipename(const char* newvalue)
{
    close();
    pipename = realpipename(newvalue);
}


large namedpipe::doseek(large, ioseekmode)
{
    return -1;
}


void namedpipe::doopen()
{

#ifdef WIN32

    if (svhandle != invhandle)
        handle = svhandle;
    else
    {
        int tries = DEF_PIPE_OPEN_RETRY;
        int delay = DEF_PIPE_OPEN_TIMEOUT / 2;
retry:
        SECURITY_ATTRIBUTES sa;
        sa.nLength = sizeof(SECURITY_ATTRIBUTES);
        sa.lpSecurityDescriptor = NULL;
        sa.bInheritHandle = TRUE;

        handle = int(CreateFileA(pipename, GENERIC_READ | GENERIC_WRITE,
            0, &sa, OPEN_EXISTING, FILE_FLAG_OVERLAPPED, 0));
        if (handle == invhandle)
        {
            if (GetLastError() == ERROR_PIPE_BUSY)
            {
                if (--tries > 0)
                {
                    delay *= 2;
                    Sleep(delay);
                    goto retry;
                }
                else
                    error(EIO, "Pipe busy");
            }
            else
                error(uerrno(), "Couldn't open named pipe");
        }
    }

#else
    if (svhandle != invhandle)
    {
	if ((handle = ::accept(svhandle, 0, 0)) < 0)
	    error(uerrno(), "Couldn't create local socket");
    }
    
    else
    {
        sockaddr_un sa;
	if (!setupsockaddr(pipename, &sa))
    	    error(ERANGE, "Socket name too long");

        // cteate a client socket
	if ((handle = ::socket(sa.sun_family, SOCK_STREAM, 0)) < 0)
    	    error(uerrno(), "Couldn't create local socket");

        // ... and connect to the local socket
	if (::connect(handle, (sockaddr*)&sa, sizeof(sa)) < 0)
	{
    	    int e = uerrno();
            doclose();
	    error(e, "Couldn't connect to local socket");
        }
    }

#endif
}


#ifdef WIN32

int namedpipe::dorawread(char* buf, int count)
{
    unsigned long ret = uint(-1);
    ovr.Offset = 0;
    ovr.OffsetHigh = 0;
    if (!ReadFile(HANDLE(handle), buf, count, &ret, &ovr)) 
    {
        if (GetLastError() == ERROR_IO_PENDING)
        {
            if (WaitForSingleObject(ovr.hEvent, DEF_PIPE_TIMEOUT) == WAIT_TIMEOUT)
                error(EIO, "Timed out");
            if (!GetOverlappedResult(HANDLE(handle), &ovr, &ret, false))
                error(uerrno(), "Couldn't read");
        }
        else
            error(uerrno(), "Couldn't read");
    }
    return ret;
}


int namedpipe::dorawwrite(const char* buf, int count)
{
    unsigned long ret = uint(-1);
    ovr.Offset = 0;
    ovr.OffsetHigh = 0;
    if (!WriteFile(HANDLE(handle), buf, count, &ret, &ovr))
    {
        if (GetLastError() == ERROR_IO_PENDING)
        {
            if (WaitForSingleObject(ovr.hEvent, DEF_PIPE_TIMEOUT) == WAIT_TIMEOUT)
                error(EIO, "Timed out");
            if (!GetOverlappedResult(HANDLE(handle), &ovr, &ret, false))
                error(uerrno(), "Couldn't write");
        }
        else
            error(uerrno(), "Couldn't write");
    }
    return ret;
}


#endif


void namedpipe::doclose()
{
#ifdef WIN32
    if (svhandle != invhandle)
        DisconnectNamedPipe(HANDLE(handle)); 
#endif
    svhandle = invhandle;
    fdxstm::doclose();
}


void namedpipe::flush()
{
#ifdef WIN32
    if (!cancelled)
        FlushFileBuffers(HANDLE(handle));
#endif
    fdxstm::flush();
}



}
