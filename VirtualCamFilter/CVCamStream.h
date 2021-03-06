#pragma once

#include <streams.h>
#include "CVCam.h"
#include "RtspManager.h"
//#include "playCommon.hh"

enum class VideoState {
	NoVideo,
	Lost,
	Reloading,
	Started,
	Playing,
};

class CVCamStream : public CSourceStream, public IAMStreamConfig, public IKsPropertySet
{
public:

	//////////////////////////////////////////////////////////////////////////
	//  IUnknown
	//////////////////////////////////////////////////////////////////////////
	STDMETHODIMP QueryInterface(REFIID riid, void** ppv);
	STDMETHODIMP_(ULONG) AddRef() { return GetOwner()->AddRef(); }                                                          \
		STDMETHODIMP_(ULONG) Release() { return GetOwner()->Release(); }

	//////////////////////////////////////////////////////////////////////////
	//  IQualityControl
	//////////////////////////////////////////////////////////////////////////
	STDMETHODIMP Notify(IBaseFilter* pSender, Quality q);

	//////////////////////////////////////////////////////////////////////////
	//  IAMStreamConfig
	//////////////////////////////////////////////////////////////////////////
	HRESULT STDMETHODCALLTYPE SetFormat(AM_MEDIA_TYPE* pmt);
	HRESULT STDMETHODCALLTYPE GetFormat(AM_MEDIA_TYPE** ppmt);
	HRESULT STDMETHODCALLTYPE GetNumberOfCapabilities(int* piCount, int* piSize);
	HRESULT STDMETHODCALLTYPE GetStreamCaps(int iIndex, AM_MEDIA_TYPE** pmt, BYTE* pSCC);

	//////////////////////////////////////////////////////////////////////////
	//  IKsPropertySet
	//////////////////////////////////////////////////////////////////////////
	HRESULT STDMETHODCALLTYPE Set(REFGUID guidPropSet, DWORD dwID, void* pInstanceData, DWORD cbInstanceData, void* pPropData, DWORD cbPropData);
	HRESULT STDMETHODCALLTYPE Get(REFGUID guidPropSet, DWORD dwPropID, void* pInstanceData, DWORD cbInstanceData, void* pPropData, DWORD cbPropData, DWORD* pcbReturned);
	HRESULT STDMETHODCALLTYPE QuerySupported(REFGUID guidPropSet, DWORD dwPropID, DWORD* pTypeSupport);

	//////////////////////////////////////////////////////////////////////////
	//  CSourceStream
	//////////////////////////////////////////////////////////////////////////
	CVCamStream(HRESULT* phr, CVCam* pParent, LPCWSTR pPinName);
	~CVCamStream();

	HRESULT FillBuffer(IMediaSample* pms);
	HRESULT DecideBufferSize(IMemAllocator* pIMemAlloc, ALLOCATOR_PROPERTIES* pProperties);
	HRESULT CheckMediaType(const CMediaType* pMediaType);
	HRESULT GetMediaType(int iPosition, CMediaType* pmt);
	HRESULT SetMediaType(const CMediaType* pmt);
	HRESULT OnThreadCreate(void);

private:
	CVCam* m_pParent;
	REFERENCE_TIME m_rtLastTime;
	HBITMAP m_hLogoBmp;
	CCritSec m_cSharedState;
	IReferenceClock* m_pClock;
	VideoState m_currentVideoState;
	//Medium* m_medium;
	RtspManager* m_rtspManager;
	int m_width;
	int m_height;

	bool ProcessVideo(IMediaSample* pSample);
	int QueryVideo(char* url);
};
