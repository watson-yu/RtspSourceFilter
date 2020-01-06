#include "stdafx.h"
#include "MyUsageEnvironment.h"

MyUsageEnvironment0::MyUsageEnvironment0(TaskScheduler& taskScheduler)
    : UsageEnvironment(taskScheduler),
    fBufferMaxSize(RESULT_MSG_BUFFER_MAX) {
    reset();
}

MyUsageEnvironment0::~MyUsageEnvironment0() {
}

void MyUsageEnvironment0::reset() {
    fCurBufferSize = 0;
    fResultMsgBuffer[fCurBufferSize] = '\0';
}

// Implementation of virtual functions:

char const* MyUsageEnvironment0::getResultMsg() const {
    return fResultMsgBuffer;
}

void MyUsageEnvironment0::setResultMsg(MsgString msg) {
    reset();
    appendToResultMsg(msg);
}

void MyUsageEnvironment0::setResultMsg(MsgString msg1, MsgString msg2) {
    setResultMsg(msg1);
    appendToResultMsg(msg2);
}

void MyUsageEnvironment0::setResultMsg(MsgString msg1, MsgString msg2,
    MsgString msg3) {
    setResultMsg(msg1, msg2);
    appendToResultMsg(msg3);
}

void MyUsageEnvironment0::setResultErrMsg(MsgString msg, int err) {
    setResultMsg(msg);

#ifndef _WIN32_WCE
    appendToResultMsg(strerror(err == 0 ? getErrno() : err));
#endif
}

void MyUsageEnvironment0::appendToResultMsg(MsgString msg) {
    char* curPtr = &fResultMsgBuffer[fCurBufferSize];
    unsigned spaceAvailable = fBufferMaxSize - fCurBufferSize;
    unsigned msgLength = strlen(msg);

    // Copy only enough of "msg" as will fit:
    if (msgLength > spaceAvailable - 1) {
        msgLength = spaceAvailable - 1;
    }

    memmove(curPtr, (char*)msg, msgLength);
    fCurBufferSize += msgLength;
    fResultMsgBuffer[fCurBufferSize] = '\0';
}

void MyUsageEnvironment0::reportBackgroundError() {
    fputs(getResultMsg(), stderr);
}

MyUsageEnvironment::MyUsageEnvironment(TaskScheduler& taskScheduler)
    : MyUsageEnvironment0(taskScheduler) {
}

MyUsageEnvironment::~MyUsageEnvironment() {
}

MyUsageEnvironment*
MyUsageEnvironment::createNew(TaskScheduler& taskScheduler) {
    return new MyUsageEnvironment(taskScheduler);
}

int MyUsageEnvironment::getErrno() const {
#if defined(__WIN32__) || defined(_WIN32) || defined(_WIN32_WCE)
    return WSAGetLastError();
#else
    return errno;
#endif
}

UsageEnvironment& MyUsageEnvironment::operator<<(char const* str) {
    if (str == NULL) str = "(NULL)"; // sanity check
    fprintf(stderr, "%s", str);
    return *this;
}

UsageEnvironment& MyUsageEnvironment::operator<<(int i) {
    fprintf(stderr, "%d", i);
    return *this;
}

UsageEnvironment& MyUsageEnvironment::operator<<(unsigned u) {
    fprintf(stderr, "%u", u);
    return *this;
}

UsageEnvironment& MyUsageEnvironment::operator<<(double d) {
    fprintf(stderr, "%f", d);
    return *this;
}

UsageEnvironment& MyUsageEnvironment::operator<<(void* p) {
    fprintf(stderr, "%p", p);
    return *this;
}
