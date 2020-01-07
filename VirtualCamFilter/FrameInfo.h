#pragma once

typedef struct __FrameHeader
{
	long TimeStamp;
	long FrameType;
	long FrameLen;
} FrameHeader;

typedef struct __FrameInfo
{
	FrameHeader frameHead;
	char* pdata;
} FrameInfo;
