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

#include <string.h>

#include "pstreams.h"


namespace ptypes {


string infilter::get_errstmname() 
{
    if (stm == nil)
        return get_streamname();
    else
        return get_streamname() + ": " + stm->get_errstmname();
}


void infilter::copytobuf(string& s) 
{
    int n = imin(savecount, length(s));
    if (n > 0) 
    {
        memcpy(savebuf, pconst(s), n);
        savebuf += n;
        savecount -= n;
        if (n == savecount)
            clear(s);
        else
            del(s, 0, n);
    }
}


void infilter::copytobuf(pconst& buf, int& count) 
{
    int n = imin(savecount, count);
    if (n > 0) 
    {
        memcpy(savebuf, buf, n);
        savebuf += n;
        savecount -= n;
        buf += n;
        count -= n;
    }
}


bool infilter::copytobuf(char c) 
{
    if (savecount > 0) {
        *savebuf = c;
        savebuf++;
        savecount--;
        return true;
    }
    else
        return false;
}


infilter::infilter(instm* istm, int ibufsize)
    : instm(ibufsize), stm(istm), savebuf(nil), savecount(0)  
{
    if (stm != nil)
        stm->addnotification(this);
}


infilter::~infilter() 
{
    if (stm != nil)
        stm->delnotification(this);
}


void infilter::freenotify(component* sender) 
{
    if (sender == stm) 
    {
        stm = nil;
        close();
    }
}


void infilter::doopen() 
{
    if (stm != nil && !stm->get_active())
        stm->open();
}


void infilter::doclose() 
{
    savebuf = nil;
    savecount = 0;
    clear(postponed);
}


void infilter::set_stm(instm* istm) 
{
    close();
    if (stm != nil)
        stm->delnotification(this);
    stm = istm;
    if (stm != nil)
        stm->addnotification(this);
}


int infilter::dorawread(char* buf, int count) 
{
    savebuf = buf;
    savecount = count;
    if (!isempty(postponed))
        copytobuf(postponed);
    if (savecount > 0 && stm != nil)
        dofilter();
    return count - savecount;
}


void infilter::post(const char* buf, int count) 
{
    if (count > 0) 
    {
        copytobuf(buf, count);
        if (count > 0)
            concat(postponed, buf, count);
    }
}


void infilter::post(string s) 
{
    if (!isempty(s)) 
    {
        copytobuf(s);
        if (!isempty(s))
            concat(postponed, s);
    }
}


void infilter::post(const char* s) 
{
    post(s, strlen(s));
}


void infilter::post(char c) 
{
    if (!copytobuf(c))
        concat(postponed, c);
}


}
