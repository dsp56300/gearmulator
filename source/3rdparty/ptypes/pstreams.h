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

#ifndef __PSTREAMS_H__
#define __PSTREAMS_H__

#ifndef __PPORT_H__
#include "pport.h"
#endif

#ifndef __PTYPES_H__
#include "ptypes.h"
#endif

#ifndef PTYPES_ST
#  ifndef __PASYNC_H__
#    include "pasync.h"  // for logfile.lock
#  endif
#endif

#include <stdarg.h>
#include <errno.h>


#ifdef WIN32
#  define _WINSOCKAPI_   // prevent inclusion of winsock.h, because we need winsock2.h
#  include "windows.h"   // for OVERLAPPED
#endif


namespace ptypes {


#ifdef _MSC_VER
#pragma pack(push, 4)
#endif


// -------------------------------------------------------------------- //
// ---  abstract stream i/o classes ----------------------------------- //
// -------------------------------------------------------------------- //


//
// stream exception class
//

class iobase;

class ptpublic estream: public exception 
{
protected:
    int code;
    iobase* errstm;
public:
    estream(iobase* ierrstm, int icode, const char* imsg);
    estream(iobase* ierrstm, int icode, const string& imsg);
    virtual ~estream();
    int get_code()          { return code; }
    iobase* get_errstm()    { return errstm; }
};


typedef void (ptdecl *iostatusevent)(iobase* sender, int code);

ptpublic int ptdecl unixerrno();
ptpublic const char* ptdecl unixerrmsg(int code);


// status codes: compatible with WinInet API
// additional status codes are defined in pinet.h for ipsocket

const int IO_CREATED = 1;
const int IO_OPENING = 5;
const int IO_OPENED = 35;
const int IO_READING = 37;
const int IO_WRITING = 38;
const int IO_EOF = 45;
const int IO_CLOSING = 250;
const int IO_CLOSED = 253;


//
// iobase
//

enum ioseekmode 
{
    IO_BEGIN,
    IO_CURRENT,
    IO_END
};


const int invhandle = -1;


class ptpublic iobase: public component 
{
    friend class fdxoutstm;

protected:
    bool    active;         // active status, changed by open() and close()
    bool    cancelled;      // the stream was cancelled by cancel()
    bool    eof;            // end of file reached, only for input streams
    int     handle;         // used in many derivative classes
    large   abspos;         // physical stream position
    int     bufsize;        // buffer size, can be changed only when not active
    char*   bufdata;        // internal: allocated buffer data
    int     bufpos;         // internal: current position
    int     bufend;         // internal: current data size in the buffer
    int     stmerrno;       // UNIX-compatible error numbers, see comments in piobase.cxx
    string  deferrormsg;    // internal: default error message when an exception is thrown,
    int     status;         // stream status code, see IO_xxx constants above
    iostatusevent onstatus; // user-defined status change handler

    virtual void bufalloc();
    virtual void buffree();
    void bufclear() { bufpos = 0; bufend = 0; }

    void errstminactive();
    void errbufrequired();
    void requireactive()        { if (!active) errstminactive(); }
    void requirebuf()           { requireactive(); if (bufdata == 0) errbufrequired(); }
    int  convertoffset(large);

    virtual void doopen() = 0;
    virtual void doclose();
    virtual large doseek(large newpos, ioseekmode mode);

    virtual void chstat(int newstat);
    virtual int uerrno();
    virtual const char* uerrmsg(int code);

public:
    iobase(int ibufsize = -1);
    virtual ~iobase();

    void open();
    void close();
    void cancel();
    void reopen()                                   { open(); }
    large seekx(large newpos, ioseekmode mode = IO_BEGIN);
    int seek(int newpos, ioseekmode mode = IO_BEGIN) { return convertoffset(seekx(newpos, mode)); }
    void error(int code, const char* defmsg);
    virtual void flush();

    virtual string get_errormsg();
    virtual string get_errstmname();
    virtual string get_streamname() = 0;

    bool get_active()                               { return active; }
    void set_active(bool newval);
    bool get_cancelled()                            { return cancelled; }
    void set_cancelled(bool newval)                 { cancelled = newval; }
    int  get_handle()                               { return handle; }
    int  get_bufsize()                              { return bufsize; }
    void set_bufsize(int newval);
    int  get_stmerrno()                             { return stmerrno; }
    int  get_status()                               { return status; }
    iostatusevent get_onstatus()                    { return onstatus; }
    void set_onstatus(iostatusevent newval)         { onstatus = newval; }
};
typedef iobase* piobase;


ptpublic extern int defbufsize;
ptpublic extern int stmbalance;


//
// instm - abstract input stream
//

const char eofchar = 0;

class ptpublic instm: public iobase 
{
protected:
    virtual int dorawread(char* buf, int count);
    int rawread(char* buf, int count);
    virtual void bufvalidate();
    void skipeol();

public:
    instm(int ibufsize = -1);
    virtual ~instm();
    virtual int classid();

    bool get_eof();
    void set_eof(bool ieof)     { eof = ieof; }
    bool get_eol();
    int  get_dataavail();
    char preview();
    char get();
    void putback();
    string token(const cset& chars);
    string token(const cset& chars, int limit);
    int token(const cset& chars, char* buf, int size);
    string line();
    string line(int limit);
    int line(char* buf, int size, bool eateol = true);
    int read(void* buf, int count);
    int skip(int count);
    int skiptoken(const cset& chars);
    void skipline(bool eateol = true);
    large tellx();
    int tell()  { return convertoffset(tellx()); }
    large seekx(large newpos, ioseekmode mode = IO_BEGIN);
    int seek(int newpos, ioseekmode mode = IO_BEGIN)   { return convertoffset(seekx(newpos, mode)); }
};
typedef instm* pinstm;


//
// outstm - abstract output stream
//

class ptpublic outstm: public iobase 
{
protected:
    bool flusheol;

    virtual int dorawwrite(const char* buf, int count);
    int rawwrite(const char* buf, int count);
    virtual void bufvalidate();
    void bufadvance(int delta)  
        { bufpos += delta; if (bufend < bufpos) bufend = bufpos; }
    bool canwrite();

public:
    outstm(bool iflusheol = false, int ibufsize = -1);
    virtual ~outstm();
    virtual int classid();

    bool get_flusheol()             { return flusheol; }
    void set_flusheol(bool newval)  { flusheol = newval; }

    virtual void flush();
    bool get_eof()                  { return eof; }
    void put(char c);
    void put(const char* str);
    void put(const string& str);
    void vputf(const char* fmt, va_list);
    void putf(const char* fmt, ...);
    void putline(const char* str);
    void putline(const string& str);
    void puteol();
    int write(const void* buf, int count);
    large tellx()                   { return abspos + bufpos; }
    int tell()                      { return convertoffset(tellx()); }
    large seekx(large newpos, ioseekmode mode = IO_BEGIN);
    int seek(int newpos, ioseekmode mode = IO_BEGIN)  { return convertoffset(seekx(newpos, mode)); }
};
typedef outstm* poutstm;


// %t and %T formats
ptpublic extern const char* const shorttimefmt;  // "%d-%b-%Y %X"
ptpublic extern const char* const longtimefmt;   // "%a %b %d %X %Y"


//
// internal class used in fdxstm
//

class ptpublic fdxstm;


class ptpublic fdxoutstm: public outstm
{
    friend class fdxstm;

protected:
    fdxstm* in;
    virtual void chstat(int newstat);
    virtual int uerrno();
    virtual const char* uerrmsg(int code);
    virtual void doopen();
    virtual void doclose();
    virtual int dorawwrite(const char* buf, int count);

public:
    fdxoutstm(int ibufsize, fdxstm* iin);
    virtual ~fdxoutstm();
    virtual string get_streamname();
};
typedef fdxstm* pfdxstm;


//
// fdxstm: abstract full-duplex stream (for sockets and pipes)
//

class ptpublic fdxstm: public instm
{
    friend class fdxoutstm;

protected:
    fdxoutstm out;

    virtual int dorawwrite(const char* buf, int count);

public:

    fdxstm(int ibufsize = -1);
    virtual ~fdxstm();
    virtual int classid();

    void set_bufsize(int newval);       // sets both input and output buffer sizes

    void open();            // rewritten to pass the call to the output stream too
    void close();
    void cancel();
    virtual void flush();
    large tellx(bool);      // true for input and false for output
    int tell(bool forin)                    { return convertoffset(tellx(forin)); }

    // output interface: pretend this class is derived both
    // from instm and outstm. actually we can't use multiple
    // inheritance here, since this is a full-duplex stream,
    // hence everything must be duplicated for input and output
    void putf(const char* fmt, ...);
    void put(char c)                        { out.put(c); }
    void put(const char* str)               { out.put(str); }
    void put(const string& str)             { out.put(str); }
    void putline(const char* str)           { out.putline(str); }
    void putline(const string& str)         { out.putline(str); }
    void puteol()                           { out.puteol(); }
    int  write(const void* buf, int count)  { return out.write(buf, count); }
    bool get_flusheol()                     { return out.get_flusheol(); }
    void set_flusheol(bool newval)          { out.set_flusheol(newval); }

    operator outstm&()			            { return out; }
};


//
// abstract input filter class
//

class ptpublic infilter: public instm 
{
protected:
    instm*   stm;
    char*    savebuf;
    int      savecount;
    string   postponed;

    void copytobuf(string& s);
    void copytobuf(pconst& buf, int& count);
    bool copytobuf(char c);

    virtual void freenotify(component* sender);
    virtual void doopen();
    virtual void doclose();
    virtual int  dorawread(char* buf, int count);
    virtual void dofilter() = 0;

    bool bufavail()  { return savecount > 0; }
    void post(const char* buf, int count);
    void post(const char* s);
    void post(char c);
    virtual void post(string s);

public:
    infilter(instm* istm, int ibufsize = -1);
    virtual ~infilter();

    virtual string get_errstmname();

    instm* get_stm()  { return stm; }
    void set_stm(instm* stm);
};


//
// abstract output filter class
//

class ptpublic outfilter: public outstm
{
protected:
    outstm* stm;
    virtual void freenotify(component* sender);
    virtual void doopen();
    virtual void doclose();

public:
    outfilter(outstm* istm, int ibufsize = -1);
    virtual ~outfilter();
    virtual string get_errstmname();
    outstm* get_stm()  { return stm; }
    void set_stm(outstm* stm);
};


//
// inmemory - memory stream
//

class ptpublic inmemory: public instm 
{
protected:
    string mem;
    virtual void bufalloc();
    virtual void buffree();
    virtual void bufvalidate();
    virtual void doopen();
    virtual void doclose();
    virtual large doseek(large newpos, ioseekmode mode);
    virtual int dorawread(char* buf, int count);

public:
    inmemory(const string& imem);
    virtual ~inmemory();
    virtual int classid();
    virtual string get_streamname();
    large seekx(large newpos, ioseekmode mode = IO_BEGIN);
    int seek(int newpos, ioseekmode mode = IO_BEGIN)  { return convertoffset(seekx(newpos, mode)); }
    string get_strdata()  { return mem; }
    void set_strdata(const string& data);
};


//
// outmemory - memory stream
//

class ptpublic outmemory: public outstm 
{
protected:
    string mem;
    int limit;

    virtual void doopen();
    virtual void doclose();
    virtual large doseek(large newpos, ioseekmode mode);
    virtual int dorawwrite(const char* buf, int count);

public:
    outmemory(int limit = -1);
    virtual ~outmemory();
    virtual int classid();
    virtual string get_streamname();
    large tellx()               { return abspos; }
    int tell()                  { return (int)abspos; }
    string get_strdata();
};


// -------------------------------------------------------------------- //
// ---  file input/output --------------------------------------------- //
// -------------------------------------------------------------------- //


//
// infile - file input
//

class outfile;

class ptpublic infile: public instm
{
protected:
    string filename;
    int    syshandle;   // if not -1, assigned to handle in open() instead of opening a file by a name
    int    peerhandle;  // pipe peer handle, needed for closing the peer after fork() on unix

    virtual void doopen();
    virtual void doclose();

public:
    infile();
    infile(const char* ifn);
    infile(const string& ifn);
    virtual ~infile();
    virtual int classid();

    void pipe(outfile&);
    virtual string get_streamname();
    int get_syshandle()                     { return syshandle; }
    void set_syshandle(int ihandle)         { close(); syshandle = ihandle; }
    int get_peerhandle()                    { return peerhandle; }
    string get_filename()                   { return filename; }
    void set_filename(const string& ifn)    { close(); filename = ifn; }
    void set_filename(const char* ifn)      { close(); filename = ifn; }
};


//
// outfile - file output
//

class ptpublic outfile: public outstm
{
protected:
    friend class infile; // infile::pipe() needs access to peerhandle

    string filename;
    int    syshandle;   // if not -1, assigned to handle in open() instead of opening a file by a name
    int    peerhandle;  // pipe peer handle, needed for closing the peer after fork() on unix
    int    umode;       // unix file mode (unix only), default = 644
    bool   append;      // append (create new if needed), default = false

    virtual void doopen();
    virtual void doclose();

public:
    outfile();
    outfile(const char* ifn, bool iappend = false);
    outfile(const string& ifn, bool iappend = false);
    virtual ~outfile();
    virtual int classid();

    virtual void flush();
    virtual string get_streamname();

    int get_syshandle()                     { return syshandle; }
    void set_syshandle(int ihandle)         { close(); syshandle = ihandle; }
    int get_peerhandle()                    { return peerhandle; }
    string get_filename()                   { return filename; }
    void set_filename(const string& ifn)    { close(); filename = ifn; }
    void set_filename(const char* ifn)      { close(); filename = ifn; }
    bool get_append()                       { return append; }
    void set_append(bool iappend)           { close(); append = iappend; }
    int  get_umode()                        { return umode; }
    void set_umode(int iumode)              { close(); umode = iumode; }
};


//
// logfile - file output with thread-safe putf()
//

class ptpublic logfile: public outfile
{
protected:
#ifndef PTYPES_ST
    mutex lock;
#endif
public:
    logfile();
    logfile(const char* ifn, bool iappend = true);
    logfile(const string& ifn, bool iappend = true);
    virtual ~logfile();
    virtual int classid();

    void vputf(const char* fmt, va_list);
    void putf(const char* fmt, ...);
};


//
// intee - UNIX tee-style utility class
//

class ptpublic intee: public infilter {
protected:
    outfile file;
    virtual void doopen();
    virtual void doclose();
    virtual void dofilter();
public:
    intee(instm* istm, const char* ifn, bool iappend = false);
    intee(instm* istm, const string& ifn, bool iappend = false);
    virtual ~intee();

    outfile* get_file()   { return &file; }
    virtual string get_streamname();
};


// -------------------------------------------------------------------- //
// ---  named pipes --------------------------------------------------- //
// -------------------------------------------------------------------- //


// on Unix this directory can be overridden by providing the
// full path, e.g. '/var/run/mypipe'. the path is ignored on 
// Windows and is always replaced with '\\<server>\pipe\'

#ifndef WIN32
#  define DEF_NAMED_PIPES_DIR "/tmp/"
#endif


#ifdef WIN32

const int DEF_PIPE_TIMEOUT = 20000;         // in milliseconds, for reading and writing
const int DEF_PIPE_OPEN_TIMEOUT = 1000;     // for connecting to the remote pipe:
const int DEF_PIPE_OPEN_RETRY = 5;          //    will double the timeout value for each retry,
                                            //    i.e. 1 second, then 2, then 4 etc.
const int DEF_PIPE_SYSTEM_BUF_SIZE = 4096;

#endif


class ptpublic namedpipe: public fdxstm
{
    friend class npserver;

protected:
    string pipename;
    int    svhandle;

#ifdef WIN32
    // we use overlapped IO in order to have timed waiting in serve()
    // and also to implement timeout error on the client side
    OVERLAPPED ovr;
    virtual int dorawread(char* buf, int count);
    virtual int dorawwrite(const char* buf, int count);
    static string ptdecl realpipename(const string& pipename, const string& svrname = nullstring);
    void initovr();
#else
    static string realpipename(const string& pipename);
    static bool setupsockaddr(const string& pipename, void* sa);
    void initovr()  {}
#endif

    virtual void doopen();
    virtual void doclose();
    virtual large doseek(large, ioseekmode);

public:
    namedpipe();
    namedpipe(const string& ipipename);
#ifdef WIN32
    namedpipe(const string& ipipename, const string& servername);
#endif
    virtual ~namedpipe();
    virtual int classid();

    virtual void flush();
    virtual string get_streamname();

    string get_pipename()   { return pipename; }
    void set_pipename(const string&);
    void set_pipename(const char*);
};


class ptpublic npserver: public unknown
{
    string pipename;
    int    handle;
    bool   active;

    void error(int code, const char* defmsg);
    void open();
    void close();
#ifdef WIN32
    void openinst();
    void closeinst();
#endif

public:
    npserver(const string& ipipename);
    ~npserver();

    bool serve(namedpipe& client, int timeout = -1);
};


// -------------------------------------------------------------------- //
// ---  utility streams ----------------------------------------------- //
// -------------------------------------------------------------------- //

//
// MD5 -- message digest algorithm
// Derived from L. Peter Deutsch's work, please see src/pmd5.cxx
//


const int md5_digsize = 16;
typedef uchar md5_digest[md5_digsize];

// from md5.h

typedef unsigned char md5_byte_t; /* 8-bit byte */
typedef unsigned int md5_word_t; /* 32-bit word */


typedef struct md5_state_s
{
    md5_word_t count[2];	/* message length in bits, lsw first */
    md5_word_t abcd[4];		/* digest buffer */
    md5_byte_t buf[64];		/* accumulate block */
} md5_state_t;


class ptpublic outmd5: public outfilter
{
protected:
    md5_state_s ctx;
    md5_digest digest;

    virtual void doopen();
    virtual void doclose();
    virtual int dorawwrite(const char* buf, int count);

public:
    outmd5(outstm* istm = nil);
    virtual ~outmd5();
    
    virtual string get_streamname();

    const unsigned char* get_bindigest()  { close(); return digest; }
    string get_digest();
};


//
// null output stream
//


class ptpublic outnull: public outstm
{
protected:
    virtual int  dorawwrite(const char*, int);
    virtual void doopen();
    virtual void doclose();
public:
    outnull();
    virtual ~outnull();
    virtual string get_streamname();
};


// -------------------------------------------------------------------- //
// ---  unit ---------------------------------------------------------- //
// -------------------------------------------------------------------- //


#ifdef _MSC_VER
// disable "type name first seen using 'struct' now seen using 'class'" warning
#  pragma warning (disable: 4099)
// disable "class '...' needs to have dll-interface to be used by clients of class 
// '...'" warning, since the compiler may sometimes give this warning incorrectly.
#  pragma warning (disable: 4251)
#endif

class unit_thread;

class ptpublic unit: public component
{
protected:
    friend class unit_thread;

    unit*         pipe_next;    // next unit in the pipe chain, assigned by connect()
    unit_thread*  main_thread;  // async execution thread, started by run() if necessary
    int           running;      // running status, to protect from recursive calls to run() and waitfor()

    void do_main();

public:
    compref<instm> uin;
    compref<outstm> uout;

    unit();
    virtual ~unit();
    virtual int classid();

    // things that may be overridden in descendant classes
    virtual void main();        // main code, called from run()
    virtual void cleanup();     // main code cleanup, called from run()

    // service methods
    void connect(unit* next);
    void run(bool async = false);
    void waitfor();
};
typedef unit* punit;


typedef unit CUnit;         // send me a $10 check if you use this alias (not obligatory though,
                            // because the library is free, after all)


//
// standard input, output and error devices
//

ptpublic extern infile  pin;
ptpublic extern logfile pout;
ptpublic extern logfile perr;
ptpublic extern outnull pnull;


#ifdef _MSC_VER
#pragma pack(pop)
#endif


}

#endif // __PSTREAMS_H__

