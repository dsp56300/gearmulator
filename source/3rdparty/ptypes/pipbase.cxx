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
#  include <signal.h>
#  include <time.h>
#endif

#include <stdio.h>  // for snprintf

#include "pinet.h"
#ifndef PTYPES_ST
#  include "pasync.h"   // for mutex
#endif


namespace ptypes {

//
// reentrant gehostby*() mess
//

#if defined(PTYPES_ST)
#  define USE_GETHOSTBY
#else
#  if defined(WIN32) || defined(__hpux) || defined(__ANDROID__)
#    define USE_GETHOSTBY
#  elif defined(__FreeBSD__) || defined(__DARWIN__)
#    define USE_GETIPNODEBY
#  elif defined(linux) || defined(__EMSCRIPTEN__)
#    define USE_GETHOSTBY_R6
#  elif defined(__NetBSD__) || defined(__OpenBSD__) || defined(__CYGWIN__)
#    define USE_LOCKED_GETHOSTBY
#  else  // hopefully the Sun-style call will work on all other systems as well
#    define USE_GETHOSTBY_R5
#  endif
#endif

#define GETHOSTBY_BUF_SIZE 4096


//
// sockets init/startup
//

// usockerrno() is used in all socket classes anyway, so this module
// along with the initialization code below will always be linked to
// a networking program

#ifdef WIN32

static class _sock_init
{
public:
    _sock_init();
    ~_sock_init();
} _init;


_sock_init::_sock_init()
{
    WORD wVersionRequested;
    WSADATA wsaData;
    int err;

    wVersionRequested = MAKEWORD(2, 0);

    err = WSAStartup(wVersionRequested, &wsaData);
    if ( err != 0 )
        fatal(CRIT_FIRST + 50, "WinSock initialization failure");

    if ( LOBYTE(wsaData.wVersion) != 2 || HIBYTE(wsaData.wVersion) != 0 )
        fatal(CRIT_FIRST + 51, "WinSock version mismatch (2.0 or compatible required)");
}


_sock_init::~_sock_init()
{
    WSACleanup();
}

#endif



//
// internet address
//

ipaddress ipnone = uint(0xffffffff);
ipaddress ipany = INADDR_ANY;
ipaddress ipbcast = INADDR_BROADCAST;



ipaddress::ipaddress(int a, int b, int c, int d)
{
    data[0] = uchar(a);
    data[1] = uchar(b);
    data[2] = uchar(c);
    data[3] = uchar(d);
}


//
// peer info
//


ippeerinfo::ippeerinfo()
    : ip(ipnone), host(), port(0)
{
}


ippeerinfo::ippeerinfo(ipaddress iip, const string& ihost, int iport)
    : ip(iip), host(ihost), port(iport)
{
}


ipaddress ippeerinfo::get_ip()
{
    if (ip == ipnone && !isempty(host))
    {
        ip = ulong(phostbyname(host));
        if (ip == ipnone)
            notfound();
    }
    return ip;
}


string ippeerinfo::get_host()
{
    if (!isempty(host))
        return host;

    if (ip == ipnone || ip == ipany || ip == ipbcast)
        return nullstring;

    host = phostbyaddr(ip);
    if (isempty(host))
        notfound();

    return host;
}


void ippeerinfo::clear()
{
    ip = ipnone;
    ptypes::clear(host);
    port = 0;
}


string ippeerinfo::asstring(bool showport) const
{
    string t;
    if (!isempty(host))
        t = host;
    else if (ip == ipany)
        t = '*';
    else if (ip == ipnone)
        t = '-';
    else
        t = iptostring(ip);
    if (showport && port != 0)
        t += ':' + itostring(port);
    return t;
}


//
// internet utilities
//

int ptdecl usockerrno()
{
#ifdef WIN32
    return WSAGetLastError();
#else
    return errno;
#endif
}

//for VisualStudio2010
#if (_MSC_VER>=1600)
#define EPFNOSUPPORT WSAEPFNOSUPPORT
#define EHOSTDOWN WSAEHOSTDOWN
#endif

const char* ptdecl usockerrmsg(int code)
{
    switch(code)
    {
    // only minimal set of most frequent/expressive errors; others go as "I/O error"
    case ENOTSOCK:          return "Invalid socket descriptor";
    case EMSGSIZE:          return "Message too long";
    case ENOPROTOOPT:
    case EPROTONOSUPPORT:
    case EPFNOSUPPORT:
    case EAFNOSUPPORT:      return "Protocol or address family not supported";
    case EADDRINUSE:        return "Address already in use";
    case EADDRNOTAVAIL:     return "Address not available";
    case ENETDOWN:          return "Network is down";
    case ENETUNREACH:       return "Network is unreachable";
    case ECONNRESET:        return "Connection reset by peer";
    case ETIMEDOUT:         return "Operation timed out";
    case ECONNREFUSED:      return "Connection refused";
    case EHOSTDOWN:         return "Host is down";
    case EHOSTUNREACH:      return "No route to host";

    // we always translate h_errno to ENOENT and simply show "host not found"
    case ENOENT:            return "Host not found";
    default: return unixerrmsg(code);
    }
}


string ptdecl iptostring(ipaddress ip)
{
    char buf[16];
    snprintf(buf, sizeof(buf), "%d.%d.%d.%d",
        uint(ip[0]), uint(ip[1]), uint(ip[2]), uint(ip[3]));
    return string(buf);
}


#if defined(USE_LOCKED_GETHOSTBY)
static mutex hplock;
#endif


ipaddress ptdecl phostbyname(const char* name)
{
    ipaddress ip;
    hostent* hp;

    if ((ip = ::inet_addr(name)) != ipnone)
    {
        if (ip[3] == 0) // network address?
            return ipnone;
    }
    else
    {
#if defined(USE_GETHOSTBY)
        if ((hp = ::gethostbyname(name)) != nil)
#elif defined(USE_LOCKED_GETHOSTBY)
        hplock.enter();
        if ((hp = ::gethostbyname(name)) != nil)
#elif defined(USE_GETIPNODEBY)
        int herrno;
        if ((hp = ::getipnodebyname(name, AF_INET, 0, &herrno)) != nil)
#elif defined(USE_GETHOSTBY_R6)
        int herrno;
        hostent result;
        char buf[GETHOSTBY_BUF_SIZE];
        if ((::gethostbyname_r(name, &result, buf, sizeof(buf), &hp, &herrno) == 0) && hp)
#elif defined(USE_GETHOSTBY_R5)
        int herrno;
        hostent result;
        char buf[GETHOSTBY_BUF_SIZE];
        if ((hp = ::gethostbyname_r(name, &result, buf, sizeof(buf), &herrno)) != nil)
#endif
        {
            if (hp->h_addrtype == AF_INET)
                memcpy(ip.data, hp->h_addr, sizeof(ip.data));
#ifdef USE_GETIPNODEBY
            freehostent(hp);
#endif
        }
#if defined(USE_LOCKED_GETHOSTBY)
        hplock.leave();
#endif
    }

    return ip;
}


string ptdecl phostbyaddr(ipaddress ip)
{
    hostent* hp;
    string r;

#if defined(USE_GETHOSTBY)
    if ((hp = ::gethostbyaddr(pconst(ip.data), sizeof(ip.data), AF_INET)) != nil)
#elif defined(USE_LOCKED_GETHOSTBY)
    hplock.enter();
    if ((hp = ::gethostbyaddr(pconst(ip.data), sizeof(ip.data), AF_INET)) != nil)
#elif defined(USE_GETIPNODEBY)
    int herrno;
    if ((hp = ::getipnodebyaddr(pconst(ip.data), sizeof(ip.data), AF_INET, &herrno)) != nil)
#elif defined(USE_GETHOSTBY_R6)
    int herrno;
    hostent result;
    char buf[GETHOSTBY_BUF_SIZE];
    if ((::gethostbyaddr_r(pconst(ip.data), sizeof(ip.data), AF_INET, &result, buf, sizeof(buf), &hp, &herrno) == 0) && hp)
#elif defined(USE_GETHOSTBY_R5)
    int herrno;
    hostent result;
    char buf[GETHOSTBY_BUF_SIZE];
    if ((hp = ::gethostbyaddr_r(pconst(ip.data), sizeof(ip.data), AF_INET, &result, buf, sizeof(buf), &herrno)) != nil)
#endif
    {
        r = hp->h_name;
#ifdef USE_GETIPNODEBY
        freehostent(hp);
#endif
    }
#if defined(USE_LOCKED_GETHOSTBY)
    hplock.leave();
#endif

    return r;
}


string ptdecl phostcname(const char* name)
{
    hostent* hp;
    string r;

#if defined(USE_GETHOSTBY)
    if ((hp = ::gethostbyname(name)) != nil)
#elif defined(USE_LOCKED_GETHOSTBY)
    hplock.enter();
    if ((hp = ::gethostbyname(name)) != nil)
#elif defined(USE_GETIPNODEBY)
    int herrno;
    if ((hp = ::getipnodebyname(name, AF_INET, 0, &herrno)) != nil)
#elif defined(USE_GETHOSTBY_R6)
    int herrno;
    hostent result;
    char buf[GETHOSTBY_BUF_SIZE];
    if ((::gethostbyname_r(name, &result, buf, sizeof(buf), &hp, &herrno) == 0) && hp)
#elif defined(USE_GETHOSTBY_R5)
    int herrno;
    hostent result;
    char buf[GETHOSTBY_BUF_SIZE];
    if ((hp = ::gethostbyname_r(name, &result, buf, sizeof(buf), &herrno)) != nil)
#endif
    {
        r = hp->h_name;
#ifdef USE_GETIPNODEBY
        freehostent(hp);
#endif
    }
#if defined(USE_LOCKED_GETHOSTBY)
    hplock.leave();
#endif

    return r;
}


bool ptdecl psockwait(int handle, int timeout)
{
#ifdef _MSC_VER
// disable "condition always true" warning caused by Microsoft's FD_SET macro
#  pragma warning (disable: 4127)
#endif
    if (handle < 0)
        return false;
    fd_set readfds;
    FD_ZERO(&readfds);
    FD_SET((uint)handle, &readfds);
    timeval t;
    t.tv_sec = timeout / 1000;
    t.tv_usec = (timeout % 1000) * 1000;
    return ::select(FD_SETSIZE, &readfds, nil, nil, (timeout < 0) ? nil : &t) > 0;
}


bool ptdecl psockname(int handle, ippeerinfo& p)
{
    sockaddr_in sa;
    memset(&sa, 0, sizeof(sa));
    psocklen addrlen = sizeof(sa);
    if (getsockname(handle, (sockaddr*)&sa, &addrlen) != 0)
        return false;
    if (sa.sin_family != AF_INET)
        return false;
    p.ip = sa.sin_addr.s_addr;
    p.port = ntohs(sa.sin_port);
    return true;
}


#ifdef _MSC_VER
// disable "unreachable code" warning for throw (known compiler bug)
#  pragma warning (disable: 4702)
#endif

void ippeerinfo::notfound()
{
    string t = usockerrmsg(ENOENT);
    throw new estream(nil, ENOENT, t + " [" + asstring(false) + ']');
}


}
