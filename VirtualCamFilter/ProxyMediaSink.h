#pragma once

#ifndef INT64_C
#define INT64_C(c) (c ## LL)
#define UINT64_C(c) (c ## ULL)
#endif

#include "liveMedia.hh"
#include "BasicUsageEnvironment.hh"
#include "VideoDecoder.h"
#include "FrameInfo.h"

/*
 * Media sink that accumulates received frames into given queue
 */
class ProxyMediaSink : public MediaSink
{
public:
    ProxyMediaSink(UsageEnvironment& env, MediaSubsession& subsession, size_t receiveBufferSize);

    virtual ~ProxyMediaSink();

    static void afterGettingFrame(void* clientData, unsigned frameSize, unsigned numTruncatedBytes,
                                  struct timeval presentationTime, unsigned durationInMicroseconds);

    void afterGettingFrame(unsigned frameSize, unsigned numTruncatedBytes,
                           struct timeval presentationTime, unsigned durationInMicroseconds);
    
    FrameInfo* getFrameInfo();

protected:
	CVideoDecoder* _decoder;

private:
    virtual Boolean continuePlaying();

private:
    size_t _receiveBufferSize;
    uint8_t* _receiveBuffer;
    MediaSubsession& _subsession;
	bool fHaveWrittenFirstFrame;
    char const* fSPropParameterSetsStr[3];
    FrameInfo* _frameInfo;
};
