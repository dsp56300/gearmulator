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
// internet (ipv4) socket
//


ipstream::ipstream()
    : fdxstm(), ippeerinfo(0, nullstring, 0), svsocket(invhandle)  {}


ipstream::ipstream(ipaddress iip, int iport)
    : fdxstm(), ippeerinfo(iip, nullstring, iport), svsocket(invhandle)  {}


ipstream::ipstream(const char* ihost, int iport)
    : fdxstm(), ippeerinfo(ipnone, ihost, iport), svsocket(invhandle)  {}


ipstream::ipstream(const string& ihost, int iport)
    : fdxstm(), ippeerinfo(ipnone, ihost, iport), svsocket(invhandle)  {}


ipstream::~ipstream()
{
    cancel();
}


int ipstream::classid()
{
    return CLASS3_IPSTM;
}


int ipstream::uerrno()
{
    return usockerrno();
}


const char* ipstream::uerrmsg(int code)
{
    return usockerrmsg(code);
}


string ipstream::get_streamname()
{
    return ippeerinfo::asstring(true);
}


void ipstream::set_ip(ipaddress iip)
{
    close();
    ip = iip;
    ptypes::clear(host);
}


void ipstream::set_host(const string& ihost)
{
    close();
    host = ihost;
    ip = ipnone;
}


void ipstream::set_host(const char* ihost)
{
    close();
    host = ihost;
    ip = ipnone;
}


void ipstream::set_port(int iport)
{
    close();
    port = iport;
}


void ipstream::doopen()
{
    sockaddr_in sa;
    memset(&sa, 0, sizeof(sa));

    if (svsocket != invhandle)
    {
        psocklen addrlen = sizeof(sa);

        // open an active server socket and assign ip and host fields
        chstat(IO_CONNECTING);

        // the last parameter of accept() can be either int* or uint*
        // depending on the target platform :(
        if ((handle = ::accept(svsocket, (sockaddr*)&sa, &addrlen)) < 0)
            error(uerrno(), "Couldn't create socket");
        chstat(IO_CONNECTED);

        if (sa.sin_family != AF_INET)
            error(EAFNOSUPPORT, "Address family not supported");

        ptypes::clear(host);
        ip = sa.sin_addr.s_addr;
        port = ntohs(sa.sin_port);
    }

    else
    {
        sa.sin_family = AF_INET;
        sa.sin_port = htons(ushort(get_port()));

        chstat(IO_RESOLVING);
        sa.sin_addr.s_addr = get_ip();  // force to resolve the address if needed
        chstat(IO_RESOLVED);

        // open a client socket
        if ((handle = ::socket(sa.sin_family, SOCK_STREAM, 0)) < 0)
            error(uerrno(), "Couldn't create socket");

        // a chance to set up extra socket options
        sockopt(handle);

        chstat(IO_CONNECTING);
        if (::connect(handle, (sockaddr*)&sa, sizeof(sa)) < 0)
        {
            int e = uerrno();
            closehandle();
            error(e, "Couldn't connect to remote host");
        }
        chstat(IO_CONNECTED);
    }
}


void ipstream::sockopt(int)
{
}


void ipstream::closehandle()
{
    ::closesocket(pexchange(&handle, invhandle));
}


large ipstream::doseek(large, ioseekmode)
{
    return -1;
}


void ipstream::doclose()
{
    svsocket = invhandle;
    if (!cancelled)
        ::shutdown(handle, SHUT_RDWR);
    closehandle();
}


#ifdef WIN32

int ipstream::dorawread(char* buf, int count) 
{
    int ret;
    if ((ret = ::recv(handle, buf, count, 0)) == -1) 
        error(uerrno(), "Couldn't read");
    return ret;
}


int ipstream::dorawwrite(const char* buf, int count)
{
    int ret;
    if ((ret = ::send(handle, buf, count, 0)) == -1) 
        error(uerrno(), "Couldn't write");
    return ret;
}

#endif


bool ipstream::waitfor(int timeout)
{
    if (!active)
        errstminactive();
    if (bufsize > 0 && bufend > bufpos)
        return true;
    return psockwait(handle, timeout);
}


ipaddress ipstream::get_myip()
{
    if (!active)
        errstminactive();
    ippeerinfo p;
    if (!psockname(handle, p))
        error(uerrno(), "Couldn't get my IP");
    return p.get_ip();
}


int ipstream::get_myport()
{
    if (!active)
        errstminactive();
    ippeerinfo p;
    if (!psockname(handle, p))
        error(uerrno(), "Couldn't get my port number");
    return p.get_port();
}


}
