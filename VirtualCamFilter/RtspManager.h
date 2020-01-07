#pragma once

#include "FrameInfo.h"

class RtspManager {
public:
	RtspManager();
	~RtspManager();
	void start(char* streamUrl);
	void doSingleStep();
	FrameInfo* getFrameInfo();

private:
	char* m_streamUrl;
};
