#pragma once

typedef struct __FrameBuffer
{
	long TimeStamp;
	long FrameType;
	size_t FrameLen;
	unsigned char* pData;
} FrameBuffer;

class RtspManager {
public:
	RtspManager();
	~RtspManager();
	void start(char* streamUrl);
	void doSingleStep();
	FrameBuffer* getFrameBuffer();

private:
	FrameBuffer* m_frameBuffer;
	char* m_streamUrl;
};
