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

#include "pport.h"


namespace ptypes {


const int quant = 64;
const int qmask = ~63;
const int quant2 = 4096;
const int qmask2 = ~4095;

// dynamic reallocation policy for strings and lists

int ptdecl memquantize(int a)
{
    if (a <= 16)
        return 16;
    if (a <= 32)
        return 32;
    else if (a <= 2048)
        return (a + quant - 1) & qmask;
    else
        return (a + quant2 - 1) & qmask2;
}


void ptdecl memerror() 
{
    fatal(CRIT_FIRST + 5, "Not enough memory");
}


void* ptdecl memalloc(uint a) 
{
    if (a == 0)
        return nil;
    else
    {
        void* p = malloc(a);
        if (p == nil) 
            memerror();
        return p;
    }
}


void* ptdecl memrealloc(void* p, uint a) 
{
    if (a == 0)
    {
        memfree(p);
        return nil;
    }
    else if (p == nil)
        return memalloc(a);
    else
    {
        p = realloc(p, a);
        if (p == nil) 
            memerror();
        return p;
    }
}


void ptdecl memfree(void* p) 
{
    if (p != nil)
        free(p);
}


}
