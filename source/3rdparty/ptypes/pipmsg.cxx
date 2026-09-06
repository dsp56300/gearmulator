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
// ipmessage: IPv4 UDP message class
//


ipmessage::ipmessage()
    : unknown(), ippeerinfo(ipnone, nullstring, 0), handle(invhandle)  {}


ipmessage::ipmessage(ipaddress iip, int iport)
    : unknown(), ippeerinfo(iip, nullstring, iport), handle(invhandle)  {}


ipmessage::ipmessage(const char* ihost, int iport)
    : unknown(), ippeerinfo(ipnone, ihost, iport), handle(invhandle)  {}


ipmessage::ipmessage(const string& ihost, int iport)
    : unknown(), ippeerinfo(ipnone, ihost, iport), handle(invhandle)  {}


ipmessage::~ipmessage()
{
    close();
}


void ipmessage::set_ip(ipaddress iip)
{
    ip = iip;
    ptypes::clear(host);
}


void ipmessage::set_host(const string& ihost)
{
    host = ihost;
    ip = 0;
}


void ipmessage::set_host(const char* ihost)
{
    host = ihost;
    ip = 0;
}


void ipmessage::set_port(int iport)
{
    port = iport;
}


ipaddress ipmessage::get_myip()
{
    ippeerinfo p;
    if (!psockname(handle, p))
        error(usockerrno(), "Couldn't get my IP");
    return p.get_ip();
}


int ipmessage::get_myport()
{
    ippeerinfo p;
    if (!psockname(handle, p))
        error(usockerrno(), "Couldn't get my port number");
    return p.get_port();
}


void ipmessage::close()
{
    if (handle != invhandle)
        ::closesocket(pexchange(&handle, invhandle));
}


void ipmessage::open()
{
    close();
    if ((handle = ::socket(AF_INET, SOCK_DGRAM, 0)) < 0)
        error(usockerrno(), "Couldn't create socket");
    // allow broadcasts
    int one = 1;
    if (::setsockopt(handle, SOL_SOCKET, SO_BROADCAST, (sockval_t)&one, sizeof(one)) != 0)
        error(usockerrno(), "Couldn't enable broadcasts");
    sockopt(handle);
}


void ipmessage::sockopt(int)
{
}


bool ipmessage::waitfor(int timeout)
{
    return psockwait(handle, timeout);
}


void ipmessage::send(const char* buf, int count)
{
    if (handle == invhandle)
        open();

    sockaddr_in sa;
    memset(&sa, 0, sizeof(sa));
    sa.sin_family = AF_INET;
    sa.sin_port = htons(ushort(get_port()));
    sa.sin_addr.s_addr = get_ip();
    if (sendto(handle, buf, count, 0, (sockaddr*)&sa, sizeof(sa)) < 0)
        error(usockerrno(), "Couldn't write");
}


int ipmessage::receive(char* buf, int count, ipaddress& src)
{
    if (handle == invhandle)
        error(EINVAL, "Couldn't read");  // must send() first

    sockaddr_in sa;
    psocklen fromlen = sizeof(sa);
    int result = ::recvfrom(handle, buf, count, 0, (sockaddr*)&sa, &fromlen);
    if (result < 0)
        error(usockerrno(), "Couldn't read");
    src = sa.sin_addr.s_addr;
    return result;
}


int ipmessage::receive(char* buf, int count)
{
    ipaddress src;
    return receive(buf, count, src);
}


string ipmessage::receive(int max, ipaddress& src)
{
    string result;
    setlength(result, max);
    int numread = receive(pchar(pconst(result)), max, src);
    setlength(result, numread);
    return result;
}


string ipmessage::receive(int max)
{
    ipaddress src;
    return receive(max, src);
}


#ifdef _MSC_VER
// disable "unreachable code" warning for throw (known compiler bug)
#  pragma warning (disable: 4702)
#endif

void ipmessage::error(int code, const char* msg)
{
    string s = usockerrmsg(code);
    if (isempty(s))
        s = msg;
    throw new estream(nil, code, s + " [" + ippeerinfo::asstring(true) + ']');
}


}
