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
// ipstmserver
//


ipstmserver::ipstmserver()
    : ipsvbase(SOCK_STREAM)
{
}


ipstmserver::~ipstmserver()
{
    close();
}


void ipstmserver::dobind(ipbindinfo* b)
{
#ifndef WIN32
    // set SO_REAUSEADDR to true, unix only. on windows this option causes
    // the previous owner of the socket to give up, which is not desirable
    // in most cases, neither compatible with unix.
    int one = 1;
    if (::setsockopt(b->handle, SOL_SOCKET, SO_REUSEADDR,
            (sockval_t)&one, sizeof(one)) != 0)
        error(*b, usockerrno(), "Can't reuse local address");
#endif

    // set up sockaddr_in and try to bind it to the socket
    sockaddr_in sa;
    memset(&sa, 0, sizeof(sa));
    sa.sin_family = AF_INET;
    sa.sin_port = htons(ushort(b->get_port()));
    sa.sin_addr.s_addr = b->get_ip();
    
    if (::bind(b->handle, (sockaddr*)&sa, sizeof(sa)) != 0)
        error(*b, usockerrno(), "Couldn't bind address");
    
    if (::listen(b->handle, SOMAXCONN) != 0)
        error(*b, usockerrno(), "Couldn't listen on socket");
}


bool ipstmserver::poll(int i, int timeout)
{
    if (!active)
        open();
    return dopoll(&i, timeout);
}


bool ipstmserver::serve(ipstream& client, int i, int timeout)
{
    if (!active)
        open();

    client.cancel();
    if (dopoll(&i, timeout))
    {
        // connect the ipstream object to the client requesting the connection
        client.svsocket = get_addr(i).handle;
        client.open();
        return true;
    }
    return false;
}


}
