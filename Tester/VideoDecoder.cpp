#include "stdafx.h"
#include "VideoDecoder.h"
//#include "MediaQueue.h"
//#include "trace.h"
#pragma unmanaged

/** Create a FrameInfo object.
*  inline helper to create FrameInfo object
\param frame_size Default frame size
*/
inline FrameInfo* FrameNew(int frame_size = 4096)
{
	FrameInfo* frame = (FrameInfo*)malloc(sizeof(FrameInfo) + frame_size);
	if (frame == NULL)
		return(NULL);
	frame->pdata = (char*)frame + sizeof(FrameInfo);//new char[frame_size];	
	frame->frameHead.FrameLen = frame_size;
	return(frame);
}

/** VideoDecoder Contrstructor.
* Allocates a AVFrame that will be reused by the decoder.  Always
* initializes the ffmpeg decoding library, to accept a H264 frame
*/
CVideoDecoder::CVideoDecoder(char const* format)
{
	try {
		//TRACE_INFO("Register codecs");
		m_frame = av_frame_alloc();
		if (m_frame) {
			int size = avpicture_get_size((AVPixelFormat)AV_PIX_FMT_YUV420P, 320, 240);
			const uint8_t* pBuf = (const uint8_t*)malloc(size);
			if (pBuf) {
				avpicture_fill((AVPicture*)m_frame, pBuf, (AVPixelFormat)AV_PIX_FMT_YUV420P, 320, 240);
			}
		}
		avcodec_register_all();

		//TRACE_INFO("Find codec");
		if (!strcmp(format, "H264")) {
			m_codec = avcodec_find_decoder(AV_CODEC_ID_H264);
		}
		else {
			if (!strcmp(format, "MP4V")) {
				m_codec = avcodec_find_decoder(AV_CODEC_ID_MPEG4);
			}
		}
		if (m_codec != NULL) {
			//TRACE_INFO("Decoder found %s", m_codec->name);
		}
		else {
			//TRACE_ERROR("Codec decoder not found");
		}

		//TRACE_INFO("Allocate code context");
		m_codecContext = avcodec_alloc_context3(m_codec);

		if (m_codec->capabilities & AV_CODEC_CAP_TRUNCATED)
			m_codecContext->flags |= AV_CODEC_FLAG_TRUNCATED;

		//we can receive truncated frames
		m_codecContext->flags2 |= AV_CODEC_FLAG2_CHUNKS;
		m_codecContext->thread_count = 4;//TODO: random value. May be changing can make decoding faster

		AVDictionary* dictionary = nullptr;
		if (avcodec_open2(m_codecContext, m_codec, &dictionary) < 0) {
			//TRACE_INFO();
		}
	}
	catch (...) {
		//TRACE_WARN("Ignoring Exception");
	}
}

/** VideoDecoder descructor.
Cleans up the ffmpeg environment and
frees the AVFrame
*/

CVideoDecoder::~CVideoDecoder()
{
	//TRACE_INFO("Cleaning up video decoder");
	if (m_frame != NULL) {
		avcodec_close(m_codecContext);
		av_free(m_frame);
	}
	m_frame = NULL;

	if (m_codecContext != NULL) av_free(m_codecContext);
	m_codecContext = NULL;
}

/** Decoder.
The supplied buffer should contain an H264 video frame, then DecodeFrame
will pass the buffer to the avcode_decode_video2 method. Once decoded we then
use the get_picture command to convert the frame to RGB24.  The RGB24 buffer is
then used to create a FrameInfo object which is placed on our video queue.

\param pBuffer Memory buffer holding an H264 frame
\param size Size of the buffer
*/
FrameInfo* CVideoDecoder::DecodeFrame(unsigned char* pBuffer, int size)
{
	//TRACE_INFO("size=%d", size);

	FrameInfo* p_block = NULL;
	uint8_t startCode4[] = { 0x00, 0x00, 0x00, 0x01 };
	int got_frame = 0;
	AVPacket packet;

	//Initialize optional fields of a packet with default values. 
	av_init_packet(&packet);

	//set the buffer and the size	
	packet.data = pBuffer;
	packet.size = size;

	while (packet.size > sizeof(startCode4)) {
		//TRACE_INFO("codecContext w=%d h=%d", m_codecContext->width, m_codecContext->height);
		//Decode the video frame of size avpkt->size from avpkt->data into picture. 

		int len = avcodec_decode_video2(m_codecContext, m_frame, &got_frame, &packet);
		//TRACE_INFO("len=%d", len);
		if (len < 0) {
			//TRACE_ERROR("Failed to decode video len=%d",len);
			break;
		}

		//sometime we dont get the whole frame, so move
		//forward and try again
		if (!got_frame) {
			//TRACE_INFO("continue: didn't get frame");
			packet.size -= len;
			packet.data += len;
			continue;
		}

		//allocate a working frame to store our rgb image
		AVFrame* rgb = av_frame_alloc();
		if (rgb == NULL) {
			//TRACE_INFO("Failed to allocate new av frame");
			return NULL;
		}

		//Allocate and return an SwsContext. 
		struct SwsContext* scale_ctx = sws_getContext(m_codecContext->width,
			m_codecContext->height,
			m_codecContext->pix_fmt,
			m_codecContext->width,
			m_codecContext->height,
			AV_PIX_FMT_BGR24,
			SWS_BICUBIC,
			NULL,
			NULL,
			NULL);
		if (scale_ctx == NULL) {
			//TRACE_INFO("Failed to get context");
			continue;
		}

		//Calculate the size in bytes that a picture of the given width and height would occupy if stored in the given picture format. 
		int numBytes = avpicture_get_size(AV_PIX_FMT_RGB24,
			m_codecContext->width,
			m_codecContext->height);
		//TRACE_INFO("numBytes=%d", numBytes);

		try {
			//create one of our FrameInfo objects
			p_block = FrameNew(numBytes);
			if (p_block == NULL) {

				//cleanup the working buffer
				av_free(rgb);
				sws_freeContext(scale_ctx);
				scale_ctx = NULL;
				return NULL;
			}

			//Fill our frame buffer with the rgb image
			avpicture_fill((AVPicture*)rgb,
				(uint8_t*)p_block->pdata,
				AV_PIX_FMT_RGB24,
				m_codecContext->width,
				m_codecContext->height);

			//Scale the image slice in srcSlice and put the resulting scaled slice in the image in dst. 
			sws_scale(scale_ctx,
				m_frame->data,
				m_frame->linesize,
				0,
				m_codecContext->height,
				rgb->data,
				rgb->linesize);

			//set the frame header to indicate rgb24
			p_block->frameHead.FrameType = (long)(AV_PIX_FMT_RGB24);
			p_block->frameHead.TimeStamp = 0;
		}
		catch (...) {
			//TRACE_ERROR("EXCEPTION: in afterGettingFrame1 ");
		}

		//cleanup the working buffer
		av_free(rgb);
		sws_freeContext(scale_ctx);

		//we got our frame no its time to move on
		break;
	}
	return p_block;
}
