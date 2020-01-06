#include "stdafx.h"
#include "CVCamStream.h"
#include "RtspManager.h"
#include "Trace.h"

//////////////////////////////////////////////////////////////////////////
// CVCamStream is the one and only output pin of CVCam which handles 
// all the stuff.
//////////////////////////////////////////////////////////////////////////
CVCamStream::CVCamStream(HRESULT* phr, CVCam* pParent, LPCWSTR pPinName) :
	CSourceStream(NAME("Virtual Cam Filter"), phr, pParent, pPinName), m_pParent(pParent)
{
	// Set the default media type as 320x240x24@15
	GetMediaType(4, &m_mt);
	m_currentVideoState = VideoState::NoVideo;
}

CVCamStream::~CVCamStream()
{
}

HRESULT CVCamStream::QueryInterface(REFIID riid, void** ppv)
{
	// Standard OLE stuff
	if (riid == _uuidof(IAMStreamConfig))
		*ppv = (IAMStreamConfig*)this;
	else if (riid == _uuidof(IKsPropertySet))
		*ppv = (IKsPropertySet*)this;
	else
		return CSourceStream::QueryInterface(riid, ppv);

	AddRef();
	return S_OK;
}

//////////////////////////////////////////////////////////////////////////
//  This is the routine where we create the data being output by the Virtual
//  Camera device.
//////////////////////////////////////////////////////////////////////////

HRESULT CVCamStream::FillBuffer(IMediaSample* pms)
{
	FrameBuffer* frameBuffer = NULL;
	REFERENCE_TIME rtNow;

	REFERENCE_TIME avgFrameTime = ((VIDEOINFOHEADER*)m_mt.pbFormat)->AvgTimePerFrame;

	rtNow = m_rtLastTime;
	m_rtLastTime += avgFrameTime;
	pms->SetTime(&rtNow, &m_rtLastTime);
	pms->SetSyncPoint(TRUE);

	BYTE* pData;
	long lDataLen;
	pms->GetPointer(&pData);
	lDataLen = pms->GetSize();

	if (m_rtspManager != NULL) {
		m_rtspManager->doSingleStep();
		frameBuffer = m_rtspManager->getFrameBuffer();
		if (frameBuffer != NULL) {
			size_t frameSize = frameBuffer->FrameLen;
			unsigned char* frameData = frameBuffer->pData;
			for (int i = 0; i < lDataLen; ++i) {
				if (i < frameSize) pData[lDataLen - i] = frameData[i];
				else pData[lDataLen - i] = 0;
			}
			return NOERROR;
		}
	}
	for (int i = 0; i < lDataLen; ++i) {
		pData[i] = 0xff;
	}

	return NOERROR;
} // FillBuffer


//
// Notify
// Ignore quality management messages sent from the downstream filter
STDMETHODIMP CVCamStream::Notify(IBaseFilter* pSender, Quality q)
{
	return E_NOTIMPL;
} // Notify

//////////////////////////////////////////////////////////////////////////
// This is called when the output format has been negotiated
//////////////////////////////////////////////////////////////////////////
HRESULT CVCamStream::SetMediaType(const CMediaType* pmt)
{
	DECLARE_PTR(VIDEOINFOHEADER, pvi, pmt->Format());
	HRESULT hr = CSourceStream::SetMediaType(pmt);
	return hr;
}

// See Directshow help topic for IAMStreamConfig for details on this method
HRESULT CVCamStream::GetMediaType(int iPosition, CMediaType* pmt)
{
	//TRACE_DEBUG("pos=%d, type=0x%x", iPosition, pmt);

	if (iPosition < 0) return E_INVALIDARG;
	if (iPosition > 8) return VFW_S_NO_MORE_ITEMS;

	if (iPosition == 0) {
		*pmt = m_mt;
		return S_OK;
	}

	QueryVideo("rtsp://192.168.1.1/h264?w=640&h=360&fps=30&br=20000");

	DECLARE_PTR(VIDEOINFOHEADER, pvi, pmt->AllocFormatBuffer(sizeof(VIDEOINFOHEADER)));
	ZeroMemory(pvi, sizeof(VIDEOINFOHEADER));

	pvi->bmiHeader.biCompression = BI_RGB;
	pvi->bmiHeader.biBitCount = 24;
	pvi->bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
	pvi->bmiHeader.biWidth = 80 * iPosition;
	pvi->bmiHeader.biHeight = 60 * iPosition;
	pvi->bmiHeader.biPlanes = 1;
	pvi->bmiHeader.biSizeImage = GetBitmapSize(&pvi->bmiHeader);
	pvi->bmiHeader.biClrImportant = 0;

	pvi->AvgTimePerFrame = 1000000;

	SetRectEmpty(&(pvi->rcSource)); // we want the whole image area rendered.
	SetRectEmpty(&(pvi->rcTarget)); // no particular destination rectangle

	pmt->SetType(&MEDIATYPE_Video);
	pmt->SetFormatType(&FORMAT_VideoInfo);
	pmt->SetTemporalCompression(FALSE);

	// Work out the GUID for the subtype from the header info.
	const GUID SubTypeGUID = GetBitmapSubtype(&pvi->bmiHeader);
	pmt->SetSubtype(&SubTypeGUID);
	pmt->SetSampleSize(pvi->bmiHeader.biSizeImage);

	return NOERROR;

} // GetMediaType

// This method is called to see if a given output format is supported
HRESULT CVCamStream::CheckMediaType(const CMediaType* pMediaType)
{
	VIDEOINFOHEADER* pvi = (VIDEOINFOHEADER*)(pMediaType->Format());
	if (*pMediaType != m_mt)
		return E_INVALIDARG;
	return S_OK;
} // CheckMediaType

// This method is called after the pins are connected to allocate buffers to stream data
HRESULT CVCamStream::DecideBufferSize(IMemAllocator* pAlloc, ALLOCATOR_PROPERTIES* pProperties)
{
	CAutoLock cAutoLock(m_pFilter->pStateLock());
	HRESULT hr = NOERROR;

	VIDEOINFOHEADER* pvi = (VIDEOINFOHEADER*)m_mt.Format();
	pProperties->cBuffers = 1;
	pProperties->cbBuffer = pvi->bmiHeader.biSizeImage;

	ALLOCATOR_PROPERTIES Actual;
	hr = pAlloc->SetProperties(pProperties, &Actual);

	if (FAILED(hr)) return hr;
	if (Actual.cbBuffer < pProperties->cbBuffer) return E_FAIL;

	return NOERROR;
} // DecideBufferSize

// Called when graph is run
HRESULT CVCamStream::OnThreadCreate()
{
	m_rtLastTime = 0;
	return NOERROR;
} // OnThreadCreate


//////////////////////////////////////////////////////////////////////////
//  IAMStreamConfig
//////////////////////////////////////////////////////////////////////////

HRESULT STDMETHODCALLTYPE CVCamStream::SetFormat(AM_MEDIA_TYPE* pmt)
{
	DECLARE_PTR(VIDEOINFOHEADER, pvi, m_mt.pbFormat);
	m_mt = *pmt;
	IPin* pin;
	ConnectedTo(&pin);
	if (pin)
	{
		IFilterGraph* pGraph = m_pParent->GetGraph();
		pGraph->Reconnect(this);
	}
	return S_OK;
}

HRESULT STDMETHODCALLTYPE CVCamStream::GetFormat(AM_MEDIA_TYPE** ppmt)
{
	*ppmt = CreateMediaType(&m_mt);
	return S_OK;
}

HRESULT STDMETHODCALLTYPE CVCamStream::GetNumberOfCapabilities(int* piCount, int* piSize)
{
	*piCount = 8;
	*piSize = sizeof(VIDEO_STREAM_CONFIG_CAPS);
	return S_OK;
}

HRESULT STDMETHODCALLTYPE CVCamStream::GetStreamCaps(int iIndex, AM_MEDIA_TYPE** pmt, BYTE* pSCC)
{
	*pmt = CreateMediaType(&m_mt);
	DECLARE_PTR(VIDEOINFOHEADER, pvi, (*pmt)->pbFormat);

	if (iIndex == 0) iIndex = 4;

	pvi->bmiHeader.biCompression = BI_RGB;
	pvi->bmiHeader.biBitCount = 24;
	pvi->bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
	pvi->bmiHeader.biWidth = 80 * iIndex;
	pvi->bmiHeader.biHeight = 60 * iIndex;
	pvi->bmiHeader.biPlanes = 1;
	pvi->bmiHeader.biSizeImage = GetBitmapSize(&pvi->bmiHeader);
	pvi->bmiHeader.biClrImportant = 0;

	SetRectEmpty(&(pvi->rcSource)); // we want the whole image area rendered.
	SetRectEmpty(&(pvi->rcTarget)); // no particular destination rectangle

	(*pmt)->majortype = MEDIATYPE_Video;
	(*pmt)->subtype = MEDIASUBTYPE_RGB24;
	(*pmt)->formattype = FORMAT_VideoInfo;
	(*pmt)->bTemporalCompression = FALSE;
	(*pmt)->bFixedSizeSamples = FALSE;
	(*pmt)->lSampleSize = pvi->bmiHeader.biSizeImage;
	(*pmt)->cbFormat = sizeof(VIDEOINFOHEADER);

	DECLARE_PTR(VIDEO_STREAM_CONFIG_CAPS, pvscc, pSCC);

	pvscc->guid = FORMAT_VideoInfo;
	pvscc->VideoStandard = AnalogVideo_None;
	pvscc->InputSize.cx = 640;
	pvscc->InputSize.cy = 480;
	pvscc->MinCroppingSize.cx = 80;
	pvscc->MinCroppingSize.cy = 60;
	pvscc->MaxCroppingSize.cx = 640;
	pvscc->MaxCroppingSize.cy = 480;
	pvscc->CropGranularityX = 80;
	pvscc->CropGranularityY = 60;
	pvscc->CropAlignX = 0;
	pvscc->CropAlignY = 0;

	pvscc->MinOutputSize.cx = 80;
	pvscc->MinOutputSize.cy = 60;
	pvscc->MaxOutputSize.cx = 640;
	pvscc->MaxOutputSize.cy = 480;
	pvscc->OutputGranularityX = 0;
	pvscc->OutputGranularityY = 0;
	pvscc->StretchTapsX = 0;
	pvscc->StretchTapsY = 0;
	pvscc->ShrinkTapsX = 0;
	pvscc->ShrinkTapsY = 0;
	pvscc->MinFrameInterval = 200000;   //50 fps
	pvscc->MaxFrameInterval = 50000000; // 0.2 fps
	pvscc->MinBitsPerSecond = (80 * 60 * 3 * 8) / 5;
	pvscc->MaxBitsPerSecond = 640 * 480 * 3 * 8 * 50;

	return S_OK;
}

//////////////////////////////////////////////////////////////////////////
// IKsPropertySet
//////////////////////////////////////////////////////////////////////////

HRESULT CVCamStream::Set(REFGUID guidPropSet, DWORD dwID, void* pInstanceData,
	DWORD cbInstanceData, void* pPropData, DWORD cbPropData)
{// Set: Cannot set any properties.
	return E_NOTIMPL;
}

// Get: Return the pin category (our only property). 
HRESULT CVCamStream::Get(
	REFGUID guidPropSet,   // Which property set.
	DWORD dwPropID,        // Which property in that set.
	void* pInstanceData,   // Instance data (ignore).
	DWORD cbInstanceData,  // Size of the instance data (ignore).
	void* pPropData,       // Buffer to receive the property data.
	DWORD cbPropData,      // Size of the buffer.
	DWORD* pcbReturned     // Return the size of the property.
)
{
	if (guidPropSet != AMPROPSETID_Pin)             return E_PROP_SET_UNSUPPORTED;
	if (dwPropID != AMPROPERTY_PIN_CATEGORY)        return E_PROP_ID_UNSUPPORTED;
	if (pPropData == NULL && pcbReturned == NULL)   return E_POINTER;

	if (pcbReturned) *pcbReturned = sizeof(GUID);
	if (pPropData == NULL)          return S_OK; // Caller just wants to know the size. 
	if (cbPropData < sizeof(GUID))  return E_UNEXPECTED;// The buffer is too small.

	*(GUID*)pPropData = PIN_CATEGORY_CAPTURE;
	return S_OK;
}

// QuerySupported: Query whether the pin supports the specified property.
HRESULT CVCamStream::QuerySupported(REFGUID guidPropSet, DWORD dwPropID, DWORD* pTypeSupport)
{
	if (guidPropSet != AMPROPSETID_Pin) return E_PROP_SET_UNSUPPORTED;
	if (dwPropID != AMPROPERTY_PIN_CATEGORY) return E_PROP_ID_UNSUPPORTED;
	// We support getting this property, but not setting it.
	if (pTypeSupport) *pTypeSupport = KSPROPERTY_SUPPORT_GET;
	return S_OK;
}

/**
Setup our video envirnoment.
Using the transport URL setup the environment
this most likely means connecting to a VSM and getting
a security token for the camera.  Note this can take some time
to complete, we made to revist.  It could be possible to get
the token in a background thread as long as we have the width
and height in the url
*/
int CVCamStream::QueryVideo(const char* streamUrl)
{
	TRACE_DEBUG("url=%s", streamUrl);
	int ret = -1;

	if (!(m_currentVideoState == VideoState::NoVideo || m_currentVideoState == VideoState::Lost)) {
		//TRACE_INFO("Video already configured, exiting");
		return 0;
	}

	if (!streamUrl) {
		//TRACE_INFO("Missing the URL");
		return ret;
	}

	try {
		//TRACE_INFO("Try to open the RTSP video stream");
		m_rtspManager = new RtspManager();
		m_rtspManager->start();
		m_currentVideoState = VideoState::Started;

		/*
		//TaskScheduler* scheduler = BasicTaskScheduler::createNew();
		UsageEnvironment* env = NULL;//UsageEnvironment::createNew(*scheduler);
		int verbosityLevel = 1;
		const char* progName = "CVCamStream";

		if (m_medium == NULL) {
			m_medium = createClient(*env, streamUrl, verbosityLevel, progName);
		}

		if (m_medium != NULL) {
			//continueAfterClientCreation1();
		}
		*/
	}
	catch (...) {
		//TRACE_CRITICAL("QueryVideo Failed");
		m_currentVideoState = VideoState::NoVideo;
		throw false;
	}
	/*
	//attempt to get the media info from the stream
	//we know that in 7.2 this does not work, but we are
	//hoping that 7.5 will enable width and height
	MediaInfo videoMediaInfo;
	try {
		TRACE_INFO("Get Media Info");
		ret = m_streamMedia->rtspClinetGetMediaInfo(CODEC_TYPE_VIDEO, videoMediaInfo);
		if (ret < 0)
		{
			TRACE_CRITICAL("Unable to get media info from RTSP stream.  ret=%d (url=%s)", ret, url->get_Url());
			return VFW_S_NO_MORE_ITEMS;
		}
	}
	catch (...)
	{
		TRACE_CRITICAL("QueryVideo Failed from ");
		m_currentVideoState = VideoState::NoVideo;
		throw false;
	}

	TRACE_INFO("Format: %d", videoMediaInfo.i_format);
	TRACE_INFO("Codec: %s", videoMediaInfo.codecName);
	if (videoMediaInfo.video.width > 0)
	{
		TRACE_INFO("Using video information directly from the stream");
		m_videoWidth = videoMediaInfo.video.width;
		m_videoHeight = videoMediaInfo.video.height;
		m_bitCount = videoMediaInfo.video.bitrate;
		if (videoMediaInfo.video.fps > 0)
			m_framerate = (REFERENCE_TIME)(10000000 / videoMediaInfo.video.fps);
	}
	else {
		TRACE_WARN("No video info in stream, using defaults from url");
		m_videoWidth = url->get_Width();
		m_videoHeight = url->get_Height();
		//m_videoWidth = 352;
		//m_videoHeight = 240;
		m_bitCount = 1;
		if (url->get_Framerate() > 0)
			m_framerate = (REFERENCE_TIME)(10000000 / url->get_Framerate());
	}

	TRACE_INFO("Width: %d", m_videoWidth);
	TRACE_INFO("Height: %d", m_videoHeight);
	TRACE_INFO("FPS: %d", 10000000 / m_framerate);
	TRACE_INFO("Bitrate: %d", m_bitCount);
	m_currentVideoState = VideoState::Reloading;
	*/
	return ret;
}

/**
Send our video buffer the live media provider who will copy
the next frame from the queue
*/
bool CVCamStream::ProcessVideo(IMediaSample* pSample)
{
	bool rc = true;
	long cbData;
	BYTE* pData;


	// Access the sample's data buffer
	pSample->GetPointer(&pData);
	cbData = pSample->GetSize();
	long bufferSize = cbData;
	/*
	rc = m_streamMedia->GetFrame(pData, bufferSize);

	if (rc)
	{
		m_lostFrameBufferCount = 0;
		m_currentVideoState = Playing;
	}
	else {
		//paint black video to indicate a lose
		int count = ((CPushSourceRTSP*)this->m_pFilter)->m_transportUrl->get_LostFrameCount();
		if (m_lostFrameBufferCount > count)
		{
			if (!(m_currentVideoState == VideoState::Lost))
			{
				TRACE_INFO("Lost frame count (%d) over limit {%d). Paint Black Frame", m_lostFrameBufferCount, count);
				//HelpLib::TraceHelper::WriteInfo("Lost frame count over limit. Paint Black Frame and shutdown.");
				memset(pData, 0, bufferSize);
				m_currentVideoState = VideoState::Lost;
				rc = true;
			}
			else {
				TRACE_INFO("Shutting Down");
				rc = false;
			}


		}
		else {
			m_lostFrameBufferCount++;
			rc = true;
			//if(m_currentVideoState==VideoState::Lost) Sleep(1000);

		}
	}
	*/
	return rc;
}
