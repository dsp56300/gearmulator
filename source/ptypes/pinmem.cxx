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

#include "pstreams.h"


namespace ptypes {


inmemory::inmemory(const string& imem)
    : instm(length(imem)), mem(imem) 
{
}


inmemory::~inmemory() 
{
    close();
}


int inmemory::classid()
{
    return CLASS2_INMEMORY;
}


void inmemory::bufalloc() 
{
    bufdata = pchar(pconst(mem));
    abspos = bufsize = bufend = length(mem);
}


void inmemory::buffree() 
{
    bufclear();
    bufdata = nil;
}


void inmemory::bufvalidate() 
{
    eof = bufpos >= bufend;
}


void inmemory::doopen() 
{
}


void inmemory::doclose() 
{
}


large inmemory::doseek(large, ioseekmode)
{
    // normally shouldn't reach this point, because seek is
    // supposed to happen within the I/O buffer
    return -1;
}


int inmemory::dorawread(char*, int) 
{
    return 0;
}


string inmemory::get_streamname() 
{
    return "mem";
}


large inmemory::seekx(large newpos, ioseekmode mode)
{
    if (mode == IO_END)
    {
        newpos += bufsize;
        mode = IO_BEGIN;
    }
    return instm::seekx(newpos, mode);
}


void inmemory::set_strdata(const string& data)
{
    close();
    mem = data;
}


}
