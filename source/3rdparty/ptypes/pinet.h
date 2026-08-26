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

#ifndef __PINET_H__
#define __PINET_H__

#ifndef __PPORT_H__
#include "pport.h"
#endif

#ifndef __PTYPES_H__
#include "ptypes.h"
#endif

#ifndef __PSTREAMS_H__
#include "pstreams.h"
#endif


#ifdef WIN32
#  include <winsock2.h>
#else
#  include <netdb.h>       // for socklen_t
#  include <sys/types.h>
#  include <sys/socket.h>
#endif


namespace ptypes {


#ifdef _MSC_VER
#pragma pack(push, 4)
#endif


//
// BSD-compatible socket error codes for Win32
//

#if defined(WSAENOTSOCK) && !defined(ENOTSOCK)

#define EWOULDBLOCK             WSAEWOULDBLOCK
#define EINPROGRESS             WSAEINPROGRESS
#define EALREADY                WSAEALREADY
#define ENOTSOCK                WSAENOTSOCK
#define EDESTADDRREQ            WSAEDESTADDRREQ
#define EMSGSIZE                WSAEMSGSIZE
#define EPROTOTYPE              WSAEPROTOTYPE
#define ENOPROTOOPT             WSAENOPROTOOPT
#define EPROTONOSUPPORT         WSAEPROTONOSUPPORT
#define ESOCKTNOSUPPORT         WSAESOCKTNOSUPPORT
#define EOPNOTSUPP              WSAEOPNOTSUPP
#define EPFNOSUPPORT            WSAEPFNOSUPPORT
#define EAFNOSUPPORT            WSAEAFNOSUPPORT
#define EADDRINUSE              WSAEADDRINUSE
#define EADDRNOTAVAIL           WSAEADDRNOTAVAIL
#define ENETDOWN                WSAENETDOWN
#define ENETUNREACH             WSAENETUNREACH
#define ENETRESET               WSAENETRESET
#define ECONNABORTED            WSAECONNABORTED
#define ECONNRESET              WSAECONNRESET
#define ENOBUFS                 WSAENOBUFS
#define EISCONN                 WSAEISCONN
#define ENOTCONN                WSAENOTCONN
#define ESHUTDOWN               WSAESHUTDOWN
#define ETOOMANYREFS            WSAETOOMANYREFS
#define ETIMEDOUT               WSAETIMEDOUT
#define ECONNREFUSED            WSAECONNREFUSED
#define ELOOP                   WSAELOOP
// #define ENAMETOOLONG            WSAENAMETOOLONG
#define EHOSTDOWN               WSAEHOSTDOWN
#define EHOSTUNREACH            WSAEHOSTUNREACH
// #define ENOTEMPTY               WSAENOTEMPTY
#define EPROCLIM                WSAEPROCLIM
#define EUSERS                  WSAEUSERS
#define EDQUOT                  WSAEDQUOT
#define ESTALE                  WSAESTALE
#define EREMOTE                 WSAEREMOTE

// NOTE: these are not errno constants in UNIX!
#define HOST_NOT_FOUND          WSAHOST_NOT_FOUND
#define TRY_AGAIN               WSATRY_AGAIN
#define NO_RECOVERY             WSANO_RECOVERY
#define NO_DATA                 WSANO_DATA

#endif


// shutdown() constants

#if defined(SD_RECEIVE) && !defined(SHUT_RD)
#  define SHUT_RD       SD_RECEIVE
#  define SHUT_WR       SD_SEND
#  define SHUT_RDWR     SD_BOTH
#endif


// max backlog value for listen()

#ifndef SOMAXCONN
#  define SOMAXCONN -1
#endif

typedef char* sockval_t;

#ifndef WIN32
#  define closesocket close
#endif


#if (defined(__DARWIN__) && !defined(_SOCKLEN_T)) || defined(WIN32) || defined(__hpux)
  typedef int psocklen;
#else
  typedef socklen_t psocklen;
#endif


// -------------------------------------------------------------------- //
// ---  IP address class and DNS utilities ---------------------------- //
// -------------------------------------------------------------------- //

//
// IP address
//

struct ptpublic ipaddress
{
public:
    union
    {
        uchar   data[4];
        ulong   ldata;
    };
    ipaddress()                                 {}
    ipaddress(ulong a)                          { ldata = a; }
    ipaddress(const ipaddress& a)               { ldata = a.ldata; }
    ipaddress(int a, int b, int c, int d);
    ipaddress& operator= (ulong a)              { ldata = a; return *this; }
    ipaddress& operator= (const ipaddress& a)   { ldata = a.ldata; return *this; }
    uchar& operator [] (int i)                  { return data[i]; }
    operator ulong() const                      { return ldata; }
};


ptpublic extern ipaddress ipnone;
ptpublic extern ipaddress ipany;
ptpublic extern ipaddress ipbcast;


//
// IP peer info: host name, IP and the port name
// used internally in ipstream and ipmessage
//


class ptpublic ippeerinfo: public noncopyable
{
protected:
    ipaddress ip;         // target IP
    string    host;       // target host name; either IP or hostname must be specified
    int       port;       // target port number

    void      notfound(); // throws a (estream*) exception

    ptpublic friend bool ptdecl psockname(int, ippeerinfo&);

public:
    ippeerinfo();
    ippeerinfo(ipaddress iip, const string& ihost, int iport);

    ipaddress get_ip();     // resolves the host name if necessary (only once)
    string    get_host();   // performs reverse-lookup if necessary (only once)
    int       get_port()    { return port; }
    void      clear();
    string    asstring(bool showport) const;
};


ptpublic string    ptdecl iptostring(ipaddress ip);
ptpublic ipaddress ptdecl phostbyname(const char* name);
ptpublic string    ptdecl phostbyaddr(ipaddress ip);
ptpublic string    ptdecl phostcname(const char* name);

// internal utilities
ptpublic int ptdecl usockerrno();
ptpublic const char* ptdecl usockerrmsg(int code);
ptpublic bool ptdecl psockwait(int handle, int timeout);
ptpublic bool ptdecl psockname(int handle, ippeerinfo&);


// -------------------------------------------------------------------- //
// ---  TCP socket classes -------------------------------------------- //
// -------------------------------------------------------------------- //


// additional IO status codes

const int IO_RESOLVING  = 10;
const int IO_RESOLVED   = 11;
const int IO_CONNECTING = 20;
const int IO_CONNECTED  = 21;


//
// ipstream
//

class ptpublic ipstream: public fdxstm, public ippeerinfo
{
    friend class ipstmserver;

protected:
    int svsocket;   // server socket descriptor, used internally by ipstmserver

#ifdef WIN32
    // sockets are not compatible with file handles on Windows
    virtual int dorawread(char* buf, int count);
    virtual int dorawwrite(const char* buf, int count);
#endif

    virtual int  uerrno();
    virtual const char* uerrmsg(int code);
    virtual void doopen();
    virtual large doseek(large newpos, ioseekmode mode);
    virtual void doclose();
    virtual void sockopt(int socket);
    void closehandle();

public:
    ipstream();
    ipstream(ipaddress ip, int port);
    ipstream(const char* host, int port);
    ipstream(const string& host, int port);
    virtual ~ipstream();
    virtual int classid();

    virtual string get_streamname();

    bool      waitfor(int timeout);
    ipaddress get_myip();
    int       get_myport();
    void      set_ip(ipaddress);
    void      set_host(const string&);
    void      set_host(const char*);
    void      set_port(int);
};


//
// common internal interfaces for ipstmserver and ipmsgserver
//

class ipbindinfo: public unknown, public ippeerinfo
{
public:
    int handle;

    ipbindinfo(ipaddress iip, const string& ihost, int iport);
    virtual ~ipbindinfo();
};


class ptpublic ipsvbase: public unknown
{
protected:
    int     socktype;
    bool    active;
    tobjlist<ipbindinfo> addrlist;       // list of local socket addresses to bind to

    void error(ippeerinfo& peer, int code, const char* defmsg);
    bool dopoll(int* i, int timeout);
    void setupfds(void* set, int i);
    virtual void open();
    virtual void close();
    virtual void dobind(ipbindinfo*) = 0;
    virtual void sockopt(int socket);

public:
    ipsvbase(int isocktype);
    virtual ~ipsvbase();

    int bind(ipaddress ip, int port);
    int bindall(int port);

    int get_addrcount()                  { return addrlist.get_count(); }
    const ipbindinfo& get_addr(int i)    { return *addrlist[i]; }
    void clear();
};


//
// ipstmserver
//

class ptpublic ipstmserver: public ipsvbase
{
protected:
    virtual void dobind(ipbindinfo*);

public:
    ipstmserver();
    virtual ~ipstmserver();

    bool poll(int i = -1, int timeout = 0);
    bool serve(ipstream& client, int i = -1, int timeout = -1);
};


// -------------------------------------------------------------------- //
// ---  UDP socket classes -------------------------------------------- //
// -------------------------------------------------------------------- //


//
// ipmessage
//

class ptpublic ipmessage: public unknown, public ippeerinfo
{
protected:
    int handle;

    void error(int code, const char* msg);
    void open();
    void close();
    virtual void sockopt(int socket);

public:
    ipmessage();
    ipmessage(ipaddress ip, int port);
    ipmessage(const char* host, int port);
    ipmessage(const string& host, int port);
    virtual ~ipmessage();

    void set_ip(ipaddress iip);
    void set_host(const string&);
    void set_host(const char*);
    void set_port(int);
    ipaddress get_myip();
    int get_myport();
    int get_handle()                            { return handle; }

    bool   waitfor(int timeout);
    int    receive(char* buf, int count, ipaddress& src);
    int    receive(char* buf, int count);
    string receive(int max, ipaddress& src);
    string receive(int max);
    void   send(const char* buf, int count);
    void   send(const string& s)                { send(s, length(s)); }
};


//
// ipmsgserver
//

class ptpublic ipmsgserver: public ipsvbase, public ippeerinfo
{
protected:
    int handle;

    virtual void close();
    virtual void dobind(ipbindinfo*);

public:
    ipmsgserver();
    virtual ~ipmsgserver();

    int get_handle()                            { return handle; }

    bool   poll(int i = -1, int timeout = 0);
    int    receive(char* buf, int count);
    string receive(int max);
    void   send(const char* buf, int count);
    void   send(const string& s)                { send(s, length(s)); }
    void   sendto(const char* buf, int count, ipaddress ip, int port);
    void   sendto(const string& s, ipaddress ip, int port)
                                                { sendto(s, length(s), ip, port); }
};


#ifdef _MSC_VER
#pragma pack(pop)
#endif


}


#endif // __PINET_H__

