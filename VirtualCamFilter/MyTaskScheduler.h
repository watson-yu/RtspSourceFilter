#pragma once

#include "liveMedia.hh"
#include "DelayQueue.hh"

class HandlerSet;

#define MAX_NUM_EVENT_TRIGGERS 32

// An abstract base class, useful for subclassing
// (e.g., to redefine the implementation of socket event handling)
class MyTaskScheduler0 : public TaskScheduler {
public:
    virtual ~MyTaskScheduler0();

    virtual void SingleStep(unsigned maxDelayTime = 0) = 0;
    // "maxDelayTime" is in microseconds.  It allows a subclass to impose a limit
    // on how long "select()" can delay, in case it wants to also do polling.
    // 0 (the default value) means: There's no maximum; just look at the delay queue

public:
    // Redefined virtual functions:
    virtual TaskToken scheduleDelayedTask(int64_t microseconds, TaskFunc* proc,
        void* clientData);
    virtual void unscheduleDelayedTask(TaskToken& prevTask);

    virtual void doEventLoop(char* watchVariable);

    virtual EventTriggerId createEventTrigger(TaskFunc* eventHandlerProc);
    virtual void deleteEventTrigger(EventTriggerId eventTriggerId);
    virtual void triggerEvent(EventTriggerId eventTriggerId, void* clientData = NULL);

protected:
    MyTaskScheduler0();

protected:
    // To implement delayed operations:
    DelayQueue fDelayQueue;

    // To implement background reads:
    HandlerSet* fHandlers;
    int fLastHandledSocketNum;

    // To implement event triggers:
    EventTriggerId fTriggersAwaitingHandling, fLastUsedTriggerMask; // implemented as 32-bit bitmaps
    TaskFunc* fTriggeredEventHandlers[MAX_NUM_EVENT_TRIGGERS];
    void* fTriggeredEventClientDatas[MAX_NUM_EVENT_TRIGGERS];
    unsigned fLastUsedTriggerNum; // in the range [0,MAX_NUM_EVENT_TRIGGERS)
};


class MyTaskScheduler : public MyTaskScheduler0 {
public:
    static MyTaskScheduler* createNew(unsigned maxSchedulerGranularity = 10000/*microseconds*/);
    // "maxSchedulerGranularity" (default value: 10 ms) specifies the maximum time that we wait (in "select()") before
    // returning to the event loop to handle non-socket or non-timer-based events, such as 'triggered events'.
    // You can change this is you wish (but only if you know what you're doing!), or set it to 0, to specify no such maximum time.
    // (You should set it to 0 only if you know that you will not be using 'event triggers'.)
    virtual ~MyTaskScheduler();
    void doSingleStep(unsigned maxDelayTime);

protected:
    MyTaskScheduler(unsigned maxSchedulerGranularity);
    // called only by "createNew()"

    static void schedulerTickTask(void* clientData);
    void schedulerTickTask();

protected:
    // Redefined virtual functions:
    virtual void SingleStep(unsigned maxDelayTime);

    virtual void setBackgroundHandling(int socketNum, int conditionSet, BackgroundHandlerProc* handlerProc, void* clientData);
    virtual void moveSocketHandling(int oldSocketNum, int newSocketNum);

protected:
    unsigned fMaxSchedulerGranularity;

    // To implement background operations:
    int fMaxNumSockets;
    fd_set fReadSet;
    fd_set fWriteSet;
    fd_set fExceptionSet;
};