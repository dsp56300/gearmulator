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
#  include <unistd.h>
#endif

#include "pstreams.h"


namespace ptypes {


#ifdef _MSC_VER
// disable "'this' : used in base member initializer list" warning
#  pragma warning (disable: 4355)
#endif


fdxoutstm::fdxoutstm(int ibufsize, fdxstm* iin)
    : outstm(false, ibufsize), in(iin)  {}


fdxoutstm::~fdxoutstm()  {}


void fdxoutstm::chstat(int newstat)
{
    outstm::chstat(newstat);
    if (newstat == IO_WRITING)
        in->chstat(newstat);
}


int fdxoutstm::uerrno()
{
    return in->uerrno();
}


const char* fdxoutstm::uerrmsg(int code)
{
    return in->uerrmsg(code);
}


string fdxoutstm::get_streamname()
{
    return in->get_streamname();
}


void fdxoutstm::doopen()
{
}


void fdxoutstm::doclose()
{
    if (in->active)
        in->close();
}


int fdxoutstm::dorawwrite(const char* buf, int count)
{
    return in->dorawwrite(buf, count);
}


fdxstm::fdxstm(int ibufsize)
    : instm(ibufsize), out(ibufsize, this)
{
    out.in = this;
    addref(&out);
}


fdxstm::~fdxstm()  {}


int fdxstm::classid()
{
    return CLASS2_FDX;
}


void fdxstm::flush()
{
    if (out.active)
        out.flush();
}


int fdxstm::dorawwrite(const char* buf, int count)
{
    if (handle == invhandle)
	return -1;
#ifdef WIN32
    unsigned long ret;
    if (!WriteFile(HANDLE(handle), buf, count, &ret, nil)) 
    {
        error(uerrno(), "Couldn't write");
        ret = uint(-1);
    }
#else
    int ret;
    if ((ret = ::write(handle, buf, count)) < 0)
        error(uerrno(), "Couldn't write");
#endif
    return ret;
}


void fdxstm::set_bufsize(int newval)
{
    instm::set_bufsize(newval);
    out.set_bufsize(newval);
}


void fdxstm::open()
{
    instm::open();
    out.open();
}


void fdxstm::close()
{
    instm::close();
    out.close();
}


void fdxstm::cancel()
{
    instm::cancel();
    out.cancel();
}


large fdxstm::tellx(bool forin)
{
    if (forin)
        return instm::tellx();
    else
        return out.tellx();
}


}
