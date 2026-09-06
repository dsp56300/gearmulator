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

#include "ptypes.h"
#include "pasync.h"
#include "pstreams.h"

#ifdef WIN32
#  include <windows.h>
#else
#  include <unistd.h>
#endif


namespace ptypes {


//
// internal thread class for running units asynchronously
//

class unit_thread: public thread
{
protected:
    unit* target;
    virtual void execute();
public:
    unit_thread(unit* itarget);
    virtual ~unit_thread();
};


unit_thread::unit_thread(unit* itarget)
    : thread(false), target(itarget)
{
    start();
}



unit_thread::~unit_thread()
{
    waitfor();
}


void unit_thread::execute()
{
    target->do_main();
}


//
// unit class
//

unit::unit()
    : component(), pipe_next(nil), main_thread(nil), 
      running(0), uin(&pin), uout(&pout)
{
}


unit::~unit()
{
    delete tpexchange<unit_thread>(&main_thread, nil);
}


int unit::classid()
{
    return CLASS_UNIT;
}


void unit::main()
{
}


void unit::cleanup()
{
}


void unit::do_main()
{
    try
    {
        if (!uout->get_active())
            uout->open();
        if (!uin->get_active())
            uin->open();
        main();
        if (uout->get_active())
            uout->flush();
    }
    catch(exception* e)
    {
        perr.putf("Error: %s\n", pconst(e->get_message()));
        delete e;
    }

    try
    {
        cleanup();
    }
    catch(exception* e)
    {
        perr.putf("Error: %s\n", pconst(e->get_message()));
        delete e;
    }

    if (pipe_next != nil)
        uout->close();
}


void unit::connect(unit* next)
{
    waitfor();
    pipe_next = next;
    infile* in = new infile();
    outfile* out = new outfile();
    next->uin = in;
    uout = out;
    in->pipe(*out);
}


void unit::waitfor()
{
    if (running == 0)
        return;
    delete tpexchange<unit_thread>(&main_thread, nil);
    unit* next = tpexchange<unit>(&pipe_next, nil);
    if (next != nil)
    {
        next->waitfor();
        next->uin = &pin;
    }
    uout = &pout;
    running = 0;
}


void unit::run(bool async)
{
    if (pexchange(&running, 1) != 0)
        return;

    if (main_thread != nil)
        fatal(CRIT_FIRST + 60, "Unit already running");
    
    if (pipe_next != nil)
        pipe_next->run(true);

    if (async)
        main_thread = new unit_thread(this);
    else
    {
        do_main();
        waitfor();
    }
}


}
