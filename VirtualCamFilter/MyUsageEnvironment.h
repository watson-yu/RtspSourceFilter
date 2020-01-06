#pragma once

#ifndef _BASIC_USAGE_ENVIRONMENT_HH
#define _BASIC_USAGE_ENVIRONMENT_HH

#include "liveMedia.hh"

#define RESULT_MSG_BUFFER_MAX 1000

class MyUsageEnvironment0 : public UsageEnvironment {
public:
    // redefined virtual functions:
    virtual MsgString getResultMsg() const;

    virtual void setResultMsg(MsgString msg);
    virtual void setResultMsg(MsgString msg1,
        MsgString msg2);
    virtual void setResultMsg(MsgString msg1,
        MsgString msg2,
        MsgString msg3);
    virtual void setResultErrMsg(MsgString msg, int err = 0);

    virtual void appendToResultMsg(MsgString msg);

    virtual void reportBackgroundError();

protected:
    MyUsageEnvironment0(TaskScheduler& taskScheduler);
    virtual ~MyUsageEnvironment0();

private:
    void reset();

    char fResultMsgBuffer[RESULT_MSG_BUFFER_MAX];
    unsigned fCurBufferSize;
    unsigned fBufferMaxSize;
};

class MyUsageEnvironment : public MyUsageEnvironment0 {
public:
    static MyUsageEnvironment* createNew(TaskScheduler& taskScheduler);

    // redefined virtual functions:
    virtual int getErrno() const;

    virtual UsageEnvironment& operator<<(char const* str);
    virtual UsageEnvironment& operator<<(int i);
    virtual UsageEnvironment& operator<<(unsigned u);
    virtual UsageEnvironment& operator<<(double d);
    virtual UsageEnvironment& operator<<(void* p);

protected:
    MyUsageEnvironment(TaskScheduler& taskScheduler);
    // called only by "createNew()" (or subclass constructors)
    virtual ~MyUsageEnvironment();
};

#endif