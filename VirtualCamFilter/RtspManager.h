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
	void start();
	void doSingleStep();
	FrameBuffer* getFrameBuffer();

private:
};
