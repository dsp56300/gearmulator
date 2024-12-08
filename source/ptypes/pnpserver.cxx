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
#  include <sys/types.h>
#  include <sys/socket.h>
#  include <sys/un.h>
#  include <unistd.h>
#endif

#include "pstreams.h"


namespace ptypes {


npserver::npserver(const string& ipipename)
    : pipename(), handle(invhandle), active(false)
{
    pipename = namedpipe::realpipename(ipipename);
}


npserver::~npserver()
{
    close();
}


void npserver::error(int code, const char* defmsg)
{
    string msg = unixerrmsg(code);
    if (isempty(msg))
        msg = defmsg;
    msg += " [" + pipename + ']';
    throw new estream(nil, code, msg);
}


#ifdef WIN32

void npserver::openinst()
{
    // called once at startup and then again, after 
    // each client connection. strange logic, to say the least...
    SECURITY_ATTRIBUTES sa;
    sa.nLength = sizeof(SECURITY_ATTRIBUTES);
    sa.lpSecurityDescriptor = NULL;
    sa.bInheritHandle = TRUE;

    handle = (int)CreateNamedPipeA(pipename, 
        PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED,
        PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT,
        PIPE_UNLIMITED_INSTANCES, 
        DEF_PIPE_SYSTEM_BUF_SIZE, DEF_PIPE_SYSTEM_BUF_SIZE,
        DEF_PIPE_TIMEOUT, &sa);
    
    if (handle == invhandle)
        error(unixerrno(), "Couldn't create");
}


void npserver::closeinst()
{
    CloseHandle(HANDLE(pexchange(&handle, invhandle)));
}

#endif


void npserver::open()
{
    close();

#ifdef WIN32

    openinst();

#else

    sockaddr_un sa;
    if (!namedpipe::setupsockaddr(pipename, &sa))
        error(ERANGE, "Socket name too long");

    if ((handle = ::socket(sa.sun_family, SOCK_STREAM, 0)) < 0)
        error(unixerrno(), "Couldn't create local socket");

    unlink(pipename);
    if (::bind(handle, (sockaddr*)&sa, sizeof(sa)) != 0)
        error(unixerrno(), "Couldn't bind local socket");
    
    if (::listen(handle, SOMAXCONN) != 0)
        error(unixerrno(), "Couldn't listen on local socket");

#endif

    active = true;
}


void npserver::close()
{
    if (active)
    {
        active = false;
#ifdef WIN32
        closeinst();
#else
        ::close(pexchange(&handle, invhandle));
        unlink(pipename);
#endif
    }
}


bool npserver::serve(namedpipe& client, int timeout)
{
    if (!active)
        open();

    client.cancel();

#ifdef WIN32

    client.ovr.Offset = 0;
    client.ovr.OffsetHigh = 0;
    bool result = ConnectNamedPipe(HANDLE(handle), &client.ovr) ?
        true : (GetLastError() == ERROR_PIPE_CONNECTED);

    if (!result && GetLastError() == ERROR_IO_PENDING)
    {
        if (WaitForSingleObject(client.ovr.hEvent, timeout) == WAIT_TIMEOUT)
            return false;
        unsigned long ret;
        if (!GetOverlappedResult(HANDLE(handle), &client.ovr, &ret, false))
            error(unixerrno(), "Couldn't read");
        result = true;
    }

    if (result)
    {
        client.svhandle = handle;
        client.pipename = pipename;
        openinst();
        client.open();
        return true;
    }
    else
        error(unixerrno(), "Couldn't connect to client");

    return false;

#else

    fd_set set;
    FD_ZERO(&set);
    FD_SET((uint)handle, &set);
    timeval t;
    t.tv_sec = timeout / 1000;
    t.tv_usec = (timeout % 1000) * 1000;
    if (::select(FD_SETSIZE, &set, nil, nil, (timeout < 0) ? nil : &t) > 0)
    {
        client.svhandle = handle;
        client.pipename = pipename;
        client.open();
        return true;
    }
    return false;

#endif
}


}
