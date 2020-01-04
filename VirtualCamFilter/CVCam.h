#pragma once

#define DECLARE_PTR(type, ptr, expr) type* ptr = (type*)(expr);

DEFINE_GUID(CLSID_VirtualCamFilter,
	0x8e14549a, 0xdb61, 0x4309, 0xaf, 0xa1, 0x35, 0x78, 0xe9, 0x27, 0xe9, 0x33);

class CVCam : public CSource
{
public:
	//////////////////////////////////////////////////////////////////////////
	//  IUnknown
	//////////////////////////////////////////////////////////////////////////
	static CUnknown* WINAPI CreateInstance(LPUNKNOWN lpunk, HRESULT* phr);
	STDMETHODIMP QueryInterface(REFIID riid, void** ppv);

	IFilterGraph* GetGraph() { return m_pGraph; }

private:
	CVCam(LPUNKNOWN lpunk, HRESULT* phr);
};
