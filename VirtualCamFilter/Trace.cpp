#include "stdafx.h"
#include <iostream>
#include <sstream>
#include <fstream>
#include <atlstr.h>
#include "Trace.h"

void log(int level, const char* functionName, const char* lpszFormat, ...)
{
	try {
		static char szMsg[2048];
		va_list argList;
		va_start(argList, lpszFormat);
		try {
			vsprintf(szMsg, lpszFormat, argList);
		}
		catch (...) {
			strcpy(szMsg, "DebugHelper:Invalid string format!");
		}
		va_end(argList);
		//std::string logMsg = static_cast<std::ostringstream*>(&(std::ostringstream() << ::GetCurrentProcessId() << "," << ::GetCurrentThreadId() << "," << functionName << ", " << szMsg))->str();
		//std::string logMsg = static_cast<std::ostringstream*>(&(std::ostringstream() << functionName << ", " << szMsg))->str();
		try {
			//Write(level, logMsg.c_str());

			using namespace std;

			string filePath = "debug.log";
			ofstream ofs(filePath.c_str(), std::ios_base::out | std::ios_base::app);
			ofs << functionName << ", " << szMsg << '\n';
			ofs.close();
		}
		catch (...) {
		}
	}
	catch (...) {
		try {
			using namespace std;
			string filePath = "debug.log";
			ofstream ofs(filePath.c_str(), std::ios_base::out | std::ios_base::app);
			ofs << lpszFormat << '\n';
			ofs.close();
		}
		catch (...) {
		}
	}
}