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

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "pport.h"

#if defined(WIN32) && !defined(NO_CRIT_MSGBOX)
#  include <windows.h>
#  define CRIT_MSGBOX
#endif


namespace ptypes {


static void ptdecl defhandler(int code, const char* msg) 
{
#ifdef CRIT_MSGBOX
    char buf[2048];
    _snprintf(buf, sizeof(buf) - 1, "Fatal [%05x]: %s", code, msg);
    MessageBoxA(0, buf, "Internal error", MB_OK | MB_ICONSTOP);
#else
    fprintf(stderr, "\nInternal [%04x]: %s\n", code, msg);
#endif
}

static _pcrithandler crith = defhandler;


_pcrithandler ptdecl getcrithandler() 
{
    return crith;
}


_pcrithandler ptdecl setcrithandler(_pcrithandler newh) 
{
    _pcrithandler ret = crith;
    crith = newh;
    return ret;
}


void ptdecl fatal(int code, const char* msg)
{
    if (crith != nil)
        (*crith)(code, msg);
    exit(code);
}


}
