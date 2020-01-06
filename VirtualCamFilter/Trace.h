#pragma once
#include "stdafx.h"

#define LOGLEVEL_OFF 0
#define LOGLEVEL_CRITICAL 1
#define LOGLEVEL_ERROR 2
#define LOGLEVEL_WARN 3
#define LOGLEVEL_INFO 4
#define LOGLEVEL_VERBOSE 5
#define LOGLEVEL_DEBUG 6

#define TRACE_CRITICAL(x,...) log(LOGLEVEL_CRITICAL, __FUNCTION__, x, ##__VA_ARGS__) 
#define TRACE_ERROR(x,...) log(LOGLEVEL_ERROR, __FUNCTION__, x, ##__VA_ARGS__) 
#define TRACE_WARN(x,...) log(LOGLEVEL_WARN, __FUNCTION__, x, ##__VA_ARGS__) 
#define TRACE_INFO(x,...) log(LOGLEVEL_INFO, __FUNCTION__, x, ##__VA_ARGS__) 
#define TRACE_VERBOSE(x,...) log(LOGLEVEL_VERBOSE, __FUNCTION__, x, ##__VA_ARGS__) 
#define TRACE_DEBUG(x,...) log(LOGLEVEL_DEBUG, __FUNCTION__, x, ##__VA_ARGS__) 

void log(int level, const char* functionName, const char* lpszFormat, ...);
