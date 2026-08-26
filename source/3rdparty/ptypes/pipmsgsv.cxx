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
// ipmsgserver: IPv4 UDP socket server
//


ipmsgserver::ipmsgserver()
    : ipsvbase(SOCK_DGRAM), ippeerinfo(), handle(invhandle)
{
}


ipmsgserver::~ipmsgserver()
{
    close();
}


void ipmsgserver::dobind(ipbindinfo* b)
{
    sockaddr_in sa;
    memset(&sa, 0, sizeof(sa));
    sa.sin_family = AF_INET;
    sa.sin_port = htons(ushort(b->get_port()));
    sa.sin_addr.s_addr = b->get_ip();
    if (::bind(b->handle, (sockaddr*)&sa, sizeof(sa)) != 0)
        error(*b, usockerrno(), "Couldn't bind address");
}


void ipmsgserver::close()
{
    if (!active)
        return;
    ipsvbase::close();
    handle = invhandle;
    ippeerinfo::clear();
}


bool ipmsgserver::poll(int i, int timeout)
{
    if (!active)
        open();
    return dopoll(&i, timeout);
}


int ipmsgserver::receive(char* buf, int count)
{
    if (!active)
        open();
    ippeerinfo::clear();

    // determine which socket has pending data
    int i = -1;
    if (!dopoll(&i, -1))
        error(*this, EINVAL, "Couldn't read");
    ipbindinfo* b = (ipbindinfo*)addrlist[i];
    handle = b->handle;

    // read data
    sockaddr_in sa;
    psocklen len = sizeof(sa);
    int result = ::recvfrom(handle, buf, count, 0, (sockaddr*)&sa, &len);
    if (result < 0)
        error(*b, usockerrno(), "Couldn't read");

    // set up peer ip and port
    ip = sa.sin_addr.s_addr;
    port = ntohs(sa.sin_port);
    return result;
}


string ipmsgserver::receive(int max)
{
    string result;
    setlength(result, max);
    int numread = receive(pchar(pconst(result)), max);
    setlength(result, numread);
    return result;
}


void ipmsgserver::send(const char* buf, int count)
{
    if (!active || handle == invhandle || ip == ipnone)
        error(*this, EINVAL, "Couldn't write");  // must receive() first

    sendto(buf, count, get_ip(), get_port());
}


void ipmsgserver::sendto(const char* buf, int count, ipaddress ip, int port)
{
    if (!active || handle == invhandle || ip == ipnone)
        error(*this, EINVAL, "Couldn't write");  // must receive() first

    sockaddr_in sa;
    memset(&sa, 0, sizeof(sa));
    sa.sin_family = AF_INET;
    sa.sin_port = htons(ushort(port));
    sa.sin_addr.s_addr = ip;
    if (::sendto(handle, buf, count, 0, (sockaddr*)&sa, sizeof(sa)) < 0)
        error(*this, usockerrno(), "Couldn't write");
}


}
