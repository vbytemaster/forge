#pragma once

#ifdef _MSC_VER
#define NO_RETURN __declspec(noreturn)
#else
#define NO_RETURN __attribute__((noreturn))
#endif

#define MAX_NUM_ARRAY_ELEMENTS (1024 * 1024)
#define MAX_SIZE_OF_BYTE_ARRAYS (20 * 1024 * 1024)
