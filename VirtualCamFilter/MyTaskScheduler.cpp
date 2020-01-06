#include "stdafx.h"
#include "MyTaskScheduler.h"

/**********
This library is free software; you can redistribute it and/or modify it under
the terms of the GNU Lesser General Public License as published by the
Free Software Foundation; either version 2.1 of the License, or (at your
option) any later version. (See <http://www.gnu.org/copyleft/lesser.html>.)

This library is distributed in the hope that it will be useful, but WITHOUT
ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
FOR A PARTICULAR PURPOSE.  See the GNU Lesser General Public License for
more details.

You should have received a copy of the GNU Lesser General Public License
along with this library; if not, write to the Free Software Foundation, Inc.,
51 Franklin Street, Fifth Floor, Boston, MA 02110-1301  USA
**********/
// Copyright (c) 1996-2014 Live Networks, Inc.  All rights reserved.
// Basic Usage Environment: for a simple, non-scripted, console application
// Implementation

#include "HandlerSet.hh"

////////// A subclass of DelayQueueEntry,
//////////     used to implement MyTaskScheduler0::scheduleDelayedTask()

class AlarmHandler : public DelayQueueEntry {
public:
    AlarmHandler(TaskFunc* proc, void* clientData, DelayInterval timeToDelay)
        : DelayQueueEntry(timeToDelay), fProc(proc), fClientData(clientData) {
    }

private: // redefined virtual functions
    virtual void handleTimeout() {
        (*fProc)(fClientData);
        DelayQueueEntry::handleTimeout();
    }

private:
    TaskFunc* fProc;
    void* fClientData;
};


////////// MyTaskScheduler0 //////////

MyTaskScheduler0::MyTaskScheduler0()
    : fLastHandledSocketNum(-1), fTriggersAwaitingHandling(0), fLastUsedTriggerMask(1), fLastUsedTriggerNum(MAX_NUM_EVENT_TRIGGERS - 1) {
    fHandlers = new HandlerSet;
    for (unsigned i = 0; i < MAX_NUM_EVENT_TRIGGERS; ++i) {
        fTriggeredEventHandlers[i] = NULL;
        fTriggeredEventClientDatas[i] = NULL;
    }
}

MyTaskScheduler0::~MyTaskScheduler0() {
    delete fHandlers;
}

TaskToken MyTaskScheduler0::scheduleDelayedTask(int64_t microseconds,
    TaskFunc* proc,
    void* clientData) {
    if (microseconds < 0) microseconds = 0;
    DelayInterval timeToDelay((long)(microseconds / 1000000), (long)(microseconds % 1000000));
    AlarmHandler* alarmHandler = new AlarmHandler(proc, clientData, timeToDelay);
    fDelayQueue.addEntry(alarmHandler);

    return (void*)(alarmHandler->token());
}

void MyTaskScheduler0::unscheduleDelayedTask(TaskToken& prevTask) {
    DelayQueueEntry* alarmHandler = fDelayQueue.removeEntry((intptr_t)prevTask);
    prevTask = NULL;
    delete alarmHandler;
}

void MyTaskScheduler0::doEventLoop(char* watchVariable) {
    // Repeatedly loop, handling readble sockets and timed events:
    while (1) {
        if (watchVariable != NULL && *watchVariable != 0) break;
        SingleStep();
    }
}

EventTriggerId MyTaskScheduler0::createEventTrigger(TaskFunc* eventHandlerProc) {
    unsigned i = fLastUsedTriggerNum;
    EventTriggerId mask = fLastUsedTriggerMask;

    do {
        i = (i + 1) % MAX_NUM_EVENT_TRIGGERS;
        mask >>= 1;
        if (mask == 0) mask = 0x80000000;

        if (fTriggeredEventHandlers[i] == NULL) {
            // This trigger number is free; use it:
            fTriggeredEventHandlers[i] = eventHandlerProc;
            fTriggeredEventClientDatas[i] = NULL; // sanity

            fLastUsedTriggerMask = mask;
            fLastUsedTriggerNum = i;

            return mask;
        }
    } while (i != fLastUsedTriggerNum);

    // All available event triggers are allocated; return 0 instead:
    return 0;
}

void MyTaskScheduler0::deleteEventTrigger(EventTriggerId eventTriggerId) {
    fTriggersAwaitingHandling &= ~eventTriggerId;

    if (eventTriggerId == fLastUsedTriggerMask) { // common-case optimization:
        fTriggeredEventHandlers[fLastUsedTriggerNum] = NULL;
        fTriggeredEventClientDatas[fLastUsedTriggerNum] = NULL;
    }
    else {
        // "eventTriggerId" should have just one bit set.
        // However, we do the reasonable thing if the user happened to 'or' together two or more "EventTriggerId"s:
        EventTriggerId mask = 0x80000000;
        for (unsigned i = 0; i < MAX_NUM_EVENT_TRIGGERS; ++i) {
            if ((eventTriggerId & mask) != 0) {
                fTriggeredEventHandlers[i] = NULL;
                fTriggeredEventClientDatas[i] = NULL;
            }
            mask >>= 1;
        }
    }
}

void MyTaskScheduler0::triggerEvent(EventTriggerId eventTriggerId, void* clientData) {
    // First, record the "clientData".  (Note that we allow "eventTriggerId" to be a combination of bits for multiple events.)
    EventTriggerId mask = 0x80000000;
    for (unsigned i = 0; i < MAX_NUM_EVENT_TRIGGERS; ++i) {
        if ((eventTriggerId & mask) != 0) {
            fTriggeredEventClientDatas[i] = clientData;
        }
        mask >>= 1;
    }

    // Then, note this event as being ready to be handled.
    // (Note that because this function (unlike others in the library) can be called from an external thread, we do this last, to
    //  reduce the risk of a race condition.)
    fTriggersAwaitingHandling |= eventTriggerId;
}


////////// HandlerSet (etc.) implementation //////////

HandlerDescriptor::HandlerDescriptor(HandlerDescriptor* nextHandler)
    : conditionSet(0), handlerProc(NULL) {
    // Link this descriptor into a doubly-linked list:
    if (nextHandler == this) { // initialization
        fNextHandler = fPrevHandler = this;
    }
    else {
        fNextHandler = nextHandler;
        fPrevHandler = nextHandler->fPrevHandler;
        nextHandler->fPrevHandler = this;
        fPrevHandler->fNextHandler = this;
    }
}

HandlerDescriptor::~HandlerDescriptor() {
    // Unlink this descriptor from a doubly-linked list:
    fNextHandler->fPrevHandler = fPrevHandler;
    fPrevHandler->fNextHandler = fNextHandler;
}

HandlerSet::HandlerSet()
    : fHandlers(&fHandlers) {
    fHandlers.socketNum = -1; // shouldn't ever get looked at, but in case...
}

HandlerSet::~HandlerSet() {
    // Delete each handler descriptor:
    while (fHandlers.fNextHandler != &fHandlers) {
        delete fHandlers.fNextHandler; // changes fHandlers->fNextHandler
    }
}

void HandlerSet
::assignHandler(int socketNum, int conditionSet, TaskScheduler::BackgroundHandlerProc* handlerProc, void* clientData) {
    // First, see if there's already a handler for this socket:
    HandlerDescriptor* handler = lookupHandler(socketNum);
    if (handler == NULL) { // No existing handler, so create a new descr:
        handler = new HandlerDescriptor(fHandlers.fNextHandler);
        handler->socketNum = socketNum;
    }

    handler->conditionSet = conditionSet;
    handler->handlerProc = handlerProc;
    handler->clientData = clientData;
}

void HandlerSet::clearHandler(int socketNum) {
    HandlerDescriptor* handler = lookupHandler(socketNum);
    delete handler;
}

void HandlerSet::moveHandler(int oldSocketNum, int newSocketNum) {
    HandlerDescriptor* handler = lookupHandler(oldSocketNum);
    if (handler != NULL) {
        handler->socketNum = newSocketNum;
    }
}

HandlerDescriptor* HandlerSet::lookupHandler(int socketNum) {
    HandlerDescriptor* handler;
    HandlerIterator iter(*this);
    while ((handler = iter.next()) != NULL) {
        if (handler->socketNum == socketNum) break;
    }
    return handler;
}

HandlerIterator::HandlerIterator(HandlerSet& handlerSet)
    : fOurSet(handlerSet) {
    reset();
}

HandlerIterator::~HandlerIterator() {
}

void HandlerIterator::reset() {
    fNextPtr = fOurSet.fHandlers.fNextHandler;
}

HandlerDescriptor* HandlerIterator::next() {
    HandlerDescriptor* result = fNextPtr;
    if (result == &fOurSet.fHandlers) { // no more
        result = NULL;
    }
    else {
        fNextPtr = fNextPtr->fNextHandler;
    }

    return result;
}


/**********
This library is free software; you can redistribute it and/or modify it under
the terms of the GNU Lesser General Public License as published by the
Free Software Foundation; either version 2.1 of the License, or (at your
option) any later version. (See <http://www.gnu.org/copyleft/lesser.html>.)

This library is distributed in the hope that it will be useful, but WITHOUT
ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
FOR A PARTICULAR PURPOSE.  See the GNU Lesser General Public License for
more details.

You should have received a copy of the GNU Lesser General Public License
along with this library; if not, write to the Free Software Foundation, Inc.,
51 Franklin Street, Fifth Floor, Boston, MA 02110-1301  USA
**********/
// Copyright (c) 1996-2014 Live Networks, Inc.  All rights reserved.
// Basic Usage Environment: for a simple, non-scripted, console application
// Implementation


#include <stdio.h>
#if defined(_QNX4)
#include <sys/select.h>
#include <unix.h>
#endif

////////// MyTaskScheduler //////////

MyTaskScheduler* MyTaskScheduler::createNew(unsigned maxSchedulerGranularity) {
    return new MyTaskScheduler(maxSchedulerGranularity);
}

MyTaskScheduler::MyTaskScheduler(unsigned maxSchedulerGranularity)
    : fMaxSchedulerGranularity(maxSchedulerGranularity), fMaxNumSockets(0) {
    FD_ZERO(&fReadSet);
    FD_ZERO(&fWriteSet);
    FD_ZERO(&fExceptionSet);

    if (maxSchedulerGranularity > 0) schedulerTickTask(); // ensures that we handle events frequently
}

MyTaskScheduler::~MyTaskScheduler() {
}

void MyTaskScheduler::schedulerTickTask(void* clientData) {
    ((MyTaskScheduler*)clientData)->schedulerTickTask();
}

void MyTaskScheduler::schedulerTickTask() {
    scheduleDelayedTask(fMaxSchedulerGranularity, schedulerTickTask, this);
}

#ifndef MILLION
#define MILLION 1000000
#endif

void MyTaskScheduler::doSingleStep(unsigned maxDelayTime) {
    SingleStep(maxDelayTime);
}

void MyTaskScheduler::SingleStep(unsigned maxDelayTime) {
    fd_set readSet = fReadSet; // make a copy for this select() call
    fd_set writeSet = fWriteSet; // ditto
    fd_set exceptionSet = fExceptionSet; // ditto

    DelayInterval const& timeToDelay = fDelayQueue.timeToNextAlarm();
    struct timeval tv_timeToDelay;
    tv_timeToDelay.tv_sec = timeToDelay.seconds();
    tv_timeToDelay.tv_usec = timeToDelay.useconds();
    // Very large "tv_sec" values cause select() to fail.
    // Don't make it any larger than 1 million seconds (11.5 days)
    const long MAX_TV_SEC = MILLION;
    if (tv_timeToDelay.tv_sec > MAX_TV_SEC) {
        tv_timeToDelay.tv_sec = MAX_TV_SEC;
    }
    // Also check our "maxDelayTime" parameter (if it's > 0):
    if (maxDelayTime > 0 &&
        (tv_timeToDelay.tv_sec > (long)maxDelayTime / MILLION ||
        (tv_timeToDelay.tv_sec == (long)maxDelayTime / MILLION &&
            tv_timeToDelay.tv_usec > (long)maxDelayTime% MILLION))) {
        tv_timeToDelay.tv_sec = maxDelayTime / MILLION;
        tv_timeToDelay.tv_usec = maxDelayTime % MILLION;
    }

    int selectResult = select(fMaxNumSockets, &readSet, &writeSet, &exceptionSet, &tv_timeToDelay);
    if (selectResult < 0) {
#if defined(__WIN32__) || defined(_WIN32)
        int err = WSAGetLastError();
        // For some unknown reason, select() in Windoze sometimes fails with WSAEINVAL if
        // it was called with no entries set in "readSet".  If this happens, ignore it:
        if (err == WSAEINVAL && readSet.fd_count == 0) {
            err = EINTR;
            // To stop this from happening again, create a dummy socket:
            int dummySocketNum = socket(AF_INET, SOCK_DGRAM, 0);
            FD_SET((unsigned)dummySocketNum, &fReadSet);
        }
        if (err != EINTR) {
#else
        if (errno != EINTR && errno != EAGAIN) {
#endif
            // Unexpected error - treat this as fatal:
#if !defined(_WIN32_WCE)
            perror("MyTaskScheduler::SingleStep(): select() fails");
            // Because this failure is often "Bad file descriptor" - which is caused by an invalid socket number (i.e., a socket number
            // that had already been closed) being used in "select()" - we print out the sockets that were being used in "select()",
            // to assist in debugging:
            fprintf(stderr, "socket numbers used in the select() call:");
            for (int i = 0; i < 10000; ++i) {
                if (FD_ISSET(i, &fReadSet) || FD_ISSET(i, &fWriteSet) || FD_ISSET(i, &fExceptionSet)) {
                    fprintf(stderr, " %d(", i);
                    if (FD_ISSET(i, &fReadSet)) fprintf(stderr, "r");
                    if (FD_ISSET(i, &fWriteSet)) fprintf(stderr, "w");
                    if (FD_ISSET(i, &fExceptionSet)) fprintf(stderr, "e");
                    fprintf(stderr, ")");
                }
            }
            fprintf(stderr, "\n");
#endif
            internalError();
        }
        }

    // Call the handler function for one readable socket:
    HandlerIterator iter(*fHandlers);
    HandlerDescriptor* handler;
    // To ensure forward progress through the handlers, begin past the last
    // socket number that we handled:
    if (fLastHandledSocketNum >= 0) {
        while ((handler = iter.next()) != NULL) {
            if (handler->socketNum == fLastHandledSocketNum) break;
        }
        if (handler == NULL) {
            fLastHandledSocketNum = -1;
            iter.reset(); // start from the beginning instead
        }
    }
    while ((handler = iter.next()) != NULL) {
        int sock = handler->socketNum; // alias
        int resultConditionSet = 0;
        if (FD_ISSET(sock, &readSet) && FD_ISSET(sock, &fReadSet)/*sanity check*/) resultConditionSet |= SOCKET_READABLE;
        if (FD_ISSET(sock, &writeSet) && FD_ISSET(sock, &fWriteSet)/*sanity check*/) resultConditionSet |= SOCKET_WRITABLE;
        if (FD_ISSET(sock, &exceptionSet) && FD_ISSET(sock, &fExceptionSet)/*sanity check*/) resultConditionSet |= SOCKET_EXCEPTION;
        if ((resultConditionSet & handler->conditionSet) != 0 && handler->handlerProc != NULL) {
            fLastHandledSocketNum = sock;
            // Note: we set "fLastHandledSocketNum" before calling the handler,
            // in case the handler calls "doEventLoop()" reentrantly.
            (*handler->handlerProc)(handler->clientData, resultConditionSet);
            break;
        }
    }
    if (handler == NULL && fLastHandledSocketNum >= 0) {
        // We didn't call a handler, but we didn't get to check all of them,
        // so try again from the beginning:
        iter.reset();
        while ((handler = iter.next()) != NULL) {
            int sock = handler->socketNum; // alias
            int resultConditionSet = 0;
            if (FD_ISSET(sock, &readSet) && FD_ISSET(sock, &fReadSet)/*sanity check*/) resultConditionSet |= SOCKET_READABLE;
            if (FD_ISSET(sock, &writeSet) && FD_ISSET(sock, &fWriteSet)/*sanity check*/) resultConditionSet |= SOCKET_WRITABLE;
            if (FD_ISSET(sock, &exceptionSet) && FD_ISSET(sock, &fExceptionSet)/*sanity check*/) resultConditionSet |= SOCKET_EXCEPTION;
            if ((resultConditionSet & handler->conditionSet) != 0 && handler->handlerProc != NULL) {
                fLastHandledSocketNum = sock;
                // Note: we set "fLastHandledSocketNum" before calling the handler,
                    // in case the handler calls "doEventLoop()" reentrantly.
                (*handler->handlerProc)(handler->clientData, resultConditionSet);
                break;
            }
        }
        if (handler == NULL) fLastHandledSocketNum = -1;//because we didn't call a handler
    }

    // Also handle any newly-triggered event (Note that we do this *after* calling a socket handler,
    // in case the triggered event handler modifies The set of readable sockets.)
    if (fTriggersAwaitingHandling != 0) {
        if (fTriggersAwaitingHandling == fLastUsedTriggerMask) {
            // Common-case optimization for a single event trigger:
            fTriggersAwaitingHandling = 0;
            if (fTriggeredEventHandlers[fLastUsedTriggerNum] != NULL) {
                (*fTriggeredEventHandlers[fLastUsedTriggerNum])(fTriggeredEventClientDatas[fLastUsedTriggerNum]);
            }
        }
        else {
            // Look for an event trigger that needs handling (making sure that we make forward progress through all possible triggers):
            unsigned i = fLastUsedTriggerNum;
            EventTriggerId mask = fLastUsedTriggerMask;

            do {
                i = (i + 1) % MAX_NUM_EVENT_TRIGGERS;
                mask >>= 1;
                if (mask == 0) mask = 0x80000000;

                if ((fTriggersAwaitingHandling & mask) != 0) {
                    fTriggersAwaitingHandling &= ~mask;
                    if (fTriggeredEventHandlers[i] != NULL) {
                        (*fTriggeredEventHandlers[i])(fTriggeredEventClientDatas[i]);
                    }

                    fLastUsedTriggerMask = mask;
                    fLastUsedTriggerNum = i;
                    break;
                }
            } while (i != fLastUsedTriggerNum);
        }
    }

    // Also handle any delayed event that may have come due.
    fDelayQueue.handleAlarm();
    }

void MyTaskScheduler
::setBackgroundHandling(int socketNum, int conditionSet, BackgroundHandlerProc * handlerProc, void* clientData) {
    if (socketNum < 0) return;
    FD_CLR((unsigned)socketNum, &fReadSet);
    FD_CLR((unsigned)socketNum, &fWriteSet);
    FD_CLR((unsigned)socketNum, &fExceptionSet);
    if (conditionSet == 0) {
        fHandlers->clearHandler(socketNum);
        if (socketNum + 1 == fMaxNumSockets) {
            --fMaxNumSockets;
        }
    }
    else {
        fHandlers->assignHandler(socketNum, conditionSet, handlerProc, clientData);
        if (socketNum + 1 > fMaxNumSockets) {
            fMaxNumSockets = socketNum + 1;
        }
        if (conditionSet & SOCKET_READABLE) FD_SET((unsigned)socketNum, &fReadSet);
        if (conditionSet & SOCKET_WRITABLE) FD_SET((unsigned)socketNum, &fWriteSet);
        if (conditionSet & SOCKET_EXCEPTION) FD_SET((unsigned)socketNum, &fExceptionSet);
    }
}

void MyTaskScheduler::moveSocketHandling(int oldSocketNum, int newSocketNum) {
    if (oldSocketNum < 0 || newSocketNum < 0) return; // sanity check
    if (FD_ISSET(oldSocketNum, &fReadSet)) { FD_CLR((unsigned)oldSocketNum, &fReadSet); FD_SET((unsigned)newSocketNum, &fReadSet); }
    if (FD_ISSET(oldSocketNum, &fWriteSet)) { FD_CLR((unsigned)oldSocketNum, &fWriteSet); FD_SET((unsigned)newSocketNum, &fWriteSet); }
    if (FD_ISSET(oldSocketNum, &fExceptionSet)) { FD_CLR((unsigned)oldSocketNum, &fExceptionSet); FD_SET((unsigned)newSocketNum, &fExceptionSet); }
    fHandlers->moveHandler(oldSocketNum, newSocketNum);

    if (oldSocketNum + 1 == fMaxNumSockets) {
        --fMaxNumSockets;
    }
    if (newSocketNum + 1 > fMaxNumSockets) {
        fMaxNumSockets = newSocketNum + 1;
    }
}
