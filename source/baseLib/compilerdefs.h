#pragma once

#ifdef _MSC_VER
#define BASELIB_NOINLINE __declspec(noinline)
#else
#define BASELIB_NOINLINE __attribute__((noinline))
#endif
