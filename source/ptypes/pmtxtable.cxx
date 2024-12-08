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


#include "pasync.h"


namespace ptypes {


pmemlock _mtxtable[_MUTEX_HASH_SIZE] // currently 29
#ifndef WIN32
    = {
    _MTX_INIT, _MTX_INIT,
    _MTX_INIT, _MTX_INIT,
    _MTX_INIT, _MTX_INIT,
    _MTX_INIT, _MTX_INIT,
    _MTX_INIT, _MTX_INIT,
    _MTX_INIT, _MTX_INIT,
    _MTX_INIT, _MTX_INIT,
    _MTX_INIT, _MTX_INIT,
    _MTX_INIT, _MTX_INIT,
    _MTX_INIT, _MTX_INIT,
    _MTX_INIT, _MTX_INIT,
    _MTX_INIT, _MTX_INIT,
    _MTX_INIT, _MTX_INIT,
    _MTX_INIT, _MTX_INIT,
    _MTX_INIT
}
#endif
;


}
