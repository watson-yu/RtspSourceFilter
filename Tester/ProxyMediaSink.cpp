#include "stdafx.h"
#include "ProxyMediaSink.h"

ProxyMediaSink::ProxyMediaSink(UsageEnvironment& env, MediaSubsession& subsession,
	MediaPacketQueue& mediaPacketQueue, size_t receiveBufferSize)
	: MediaSink(env)
	, _receiveBufferSize(receiveBufferSize)
	, _receiveBuffer(new uint8_t[receiveBufferSize])
	, _subsession(subsession)
	, _mediaPacketQueue(mediaPacketQueue)
{
	_decoder = new CVideoDecoder("H264");
	fHaveWrittenFirstFrame = False;
	fSPropParameterSetsStr[0] = subsession.fmtp_spropparametersets();
	fSPropParameterSetsStr[1] = NULL;
	fSPropParameterSetsStr[2] = NULL;
}

ProxyMediaSink::~ProxyMediaSink() { delete[] _receiveBuffer; }

void ProxyMediaSink::afterGettingFrame(void* clientData, unsigned frameSize,
	unsigned numTruncatedBytes, struct timeval presentationTime,
	unsigned durationInMicroseconds)
{
	ProxyMediaSink* sink = static_cast<ProxyMediaSink*>(clientData);
	sink->afterGettingFrame(frameSize, numTruncatedBytes, presentationTime, durationInMicroseconds);
}

void ProxyMediaSink::afterGettingFrame(unsigned frameSize, unsigned numTruncatedBytes,
	struct timeval presentationTime,
	unsigned durationInMicroseconds)
{
	if (numTruncatedBytes == 0) {
		bool isRtcpSynced =
			_subsession.rtpSource() && _subsession.rtpSource()->hasBeenSynchronizedUsingRTCP();

		unsigned char pData[655360];
		long bufferSize = 655360;

		BYTE* pBuffer = pData;
		long writtenSize = 0;

		unsigned char const start_code[4] = { 0x00, 0x00, 0x00, 0x01 };

		if (!fHaveWrittenFirstFrame) {
			// If we have NAL units encoded in "sprop parameter strings", prepend these to the file:
			for (unsigned j = 0; j < 3; ++j) {
				unsigned numSPropRecords;
				SPropRecord* sPropRecords
					= parseSPropParameterSets(fSPropParameterSetsStr[j], numSPropRecords);
				for (unsigned i = 0; i < numSPropRecords; ++i) {
					memcpy_s(pBuffer, bufferSize, start_code, 4);
					pBuffer += 4;
					bufferSize -= 4;
					writtenSize += 4;
					memcpy_s(pBuffer, bufferSize, sPropRecords[i].sPropBytes, sPropRecords[i].sPropLength);
					pBuffer += sPropRecords[i].sPropLength;
					bufferSize -= sPropRecords[i].sPropLength;
					writtenSize += sPropRecords[i].sPropLength;
				}
				delete[] sPropRecords;
			}
			fHaveWrittenFirstFrame = True; // for next time
		}

		// Write the input data to the file, with the start code in front:
		memcpy_s(pBuffer, bufferSize, start_code, 4);
		pBuffer += 4;
		bufferSize -= 4;
		writtenSize += 4;

		memcpy_s(pBuffer, bufferSize, _receiveBuffer, _receiveBufferSize);
		writtenSize += _receiveBufferSize;

		FrameInfo* frame = _decoder->DecodeFrame(pData, writtenSize);

		/*
		// send the frame out to the decoder
		frame = _decoder->DecodeFrame(pData, size);
		if (frame != NULL) {
			//_mediaPacketQueue.push(mediaSample);
		}*/
		//}
	}
	else {
	}

	continuePlaying();
}

Boolean ProxyMediaSink::continuePlaying()
{
	if (fSource == nullptr)
		return False;
	fSource->getNextFrame(_receiveBuffer, _receiveBufferSize, afterGettingFrame, this,
		onSourceClosure, this);
	return True;
}
