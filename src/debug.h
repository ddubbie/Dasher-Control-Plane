#ifndef __DEBUG_H__
#define __DEBUG_H__

#include "core.h"

void debug_setup(void);
void dump_log(char *filename, int line, char *log, ...);
void debug_teardown(void);

#define ENABLE_DUMP_LOG TRUE

#if ENABLE_DUMP_LOG
#define DEBUG_GET_REQUEST FALSE
#define DEBUG_FREE_REQUEST FALSE
#else
#define DEBUG_GET_REQUEST FALSE
#define DEBUG_FREE_REQUEST FALSE
#endif

#define LOG_INFO(f, ...) do{\
	fprintf(stdout, "(%10s:%4d) " f, __FILE__, __LINE__, ##__VA_ARGS__);\
} while(0)

#define LOG_ERROR(f, ...) do {\
	fprintf(stderr, "(%10s:%4d) " f, __FILE__, __LINE__, ##__VA_ARGS__);\
} while(0)

#if ENABLE_DUMP_LOG
#define DUMP_LOG(f, ...) dump_log(NULL, -1, f, __VA_ARGS__)
#else
#define DUMP_LOG(f, ...) UNUSED(0)
#endif

#if DEBUG_GET_REQUEST
#define LOG_GET_REQUEST(f, ...) dump_log(__FILE__, __LINE__, f, __VA_ARGS__)
#else
#define LOG_GET_REQUEST(f, ...) UNUSED(0)
#endif

#if DEBUG_FREE_REQUEST
#define LOG_FREE_REQUEST(f, ...) dump_log(__FILE__, __LINE__, f, __VA_ARGS__)
#else
#define LOG_FREE_REQUEST(f, ...) UNUSED(0)
#endif

#endif
