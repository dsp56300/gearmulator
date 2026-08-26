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
#  include <winsock2.h>
#else
#  include <sys/time.h>
#  include <sys/types.h>
#  include <sys/socket.h>
#  include <netinet/in.h>
#  include <arpa/inet.h>
#  include <netdb.h>
#  include <unistd.h>
#  include <time.h>
#endif

#include "pinet.h"


namespace ptypes {


//
// ipbindinfo
//


ipbindinfo::ipbindinfo(ipaddress iip, const string& ihost, int iport)
    : unknown(), ippeerinfo(iip, ihost, iport), handle(invhandle)
{
}


ipbindinfo::~ipbindinfo()
{
}


//
// ipsvbase
//


ipsvbase::ipsvbase(int isocktype)
    : socktype(isocktype), active(false), addrlist(true)  {}


ipsvbase::~ipsvbase()
{
    close();
}


void ipsvbase::error(ippeerinfo& p, int code, const char* defmsg)
{
    string msg = usockerrmsg(code);
    if (isempty(msg))
        msg = defmsg;
    msg += " [" + p.asstring(true) + ']';
    throw new estream(nil, code, msg);
}


int ipsvbase::bind(ipaddress ip, int port)
{
    close();
    addrlist.add(new ipbindinfo(ip, nullstring, port));
    return addrlist.get_count() - 1;
}


int ipsvbase::bindall(int port)
{
    close();
    return bind(ipany, port);
}


void ipsvbase::clear()
{
    close();
    addrlist.clear();
}


void ipsvbase::open()
{
    close();
    if (addrlist.get_count() == 0)
        fatal(CRIT_FIRST + 52, "No addresses specified to bind to");
    active = true;
    for (int i = 0; i < addrlist.get_count(); i++)
    {
        ipbindinfo* b = addrlist[i];
        b->handle = ::socket(AF_INET, socktype, 0);
        if (b->handle < 0)
            error(*b, usockerrno(), "Couldn't create socket");
        sockopt(b->handle);
        dobind(b);
    }
}


void ipsvbase::close()
{
    if (!active)
        return;
    for (int i = 0; i < addrlist.get_count(); i++)
    {
        ipbindinfo* b = addrlist[i];
        ::closesocket(pexchange(&b->handle, invhandle));
    }
    active = false;
}


bool ipsvbase::dopoll(int* i, int timeout)
{
    fd_set set;
    setupfds(&set, *i);
    timeval t;
    t.tv_sec = timeout / 1000;
    t.tv_usec = (timeout % 1000) * 1000;
    if (::select(FD_SETSIZE, &set, nil, nil, (timeout < 0) ? nil : &t) > 0)
    {
        if (*i >= 0)
            return true;
        // if the user selected -1 (all), find the socket which has a pending connection
        // and assign it to i
        for (int j = 0; j < addrlist.get_count(); j++)   
            if (FD_ISSET(uint(addrlist[j]->handle), &set))
            {
                *i = j;
                return true;
            }
    }
    return false;
}


void ipsvbase::setupfds(void* set, int i)
{
#ifdef _MSC_VER
// disable "condition always true" warning caused by Microsoft's FD_SET macro
#  pragma warning (disable: 4127)
#endif
    FD_ZERO((fd_set*)set);
    if (i >= 0)
    {
        int h = get_addr(i).handle;
        if (h >= 0)
            FD_SET((uint)h, (fd_set*)set);
    }
    else
        for (i = 0; i < addrlist.get_count(); i++)
        {
            int h = addrlist[i]->handle;
            if (h >= 0)
                FD_SET((uint)h, (fd_set*)set);
        }
}


void ipsvbase::sockopt(int)
{
}


}
