#include <iostream>

#include "server.h"

#ifndef _WIN32
#include <cstdio>
#include <execinfo.h>
#include <signal.h>
#include <cstdlib>

void segFaultHandler(int sig)
{
	void *array[10];
	size_t size;

	size = backtrace(array, 10);

	fprintf(stderr, "Error: signal %d:\n", sig);
	backtrace_symbols_fd(array, size, 2);
	exit(1);
}
#endif

int main(int _argc, char** _argv)
{
#ifndef _WIN32
	signal(SIGSEGV, segFaultHandler);
#endif

	while(true)
	{
		try
		{
			bridgeServer::Server server(_argc, _argv);
			server.run();
		}
		catch(...)
		{
			std::cout << "Server exception, attempting to restart\n";
		}
	}
	return 0;
}
