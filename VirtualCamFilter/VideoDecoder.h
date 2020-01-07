#pragma once

#ifndef INT64_C
#define INT64_C(c) (c ## LL)
#define UINT64_C(c) (c ## ULL)
#endif

#include <MediaSink.hh>
#include <H264VideoRTPSink.hh>
#include <liveMedia.hh>
#include <iostream>
#include "MediaSink.hh"
//#include "MediaQueue.h"
#include "FrameInfo.h"

extern "C"
{
#ifndef __STDC_CONSTANT_MACROS
#  define __STDC_CONSTANT_MACROS
#endif
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include "libswscale/swscale.h"
}

class CVideoDecoder
{
public:
	CVideoDecoder(char const* format);
	~CVideoDecoder();
	FrameInfo* DecodeFrame(unsigned char * pBuffer, int size);

private:
    AVCodec *m_codec;
    AVCodecContext *m_codecContext;
    AVFrame *m_frame;

};

