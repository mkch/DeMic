#include "framework.h"

#include "MicCtrl.h"
#include <Audioclient.h>
#include <Audiopolicy.h>
#include <Functiondiscoverykeys_devpkey.h>
#include <string>
#include <vector>
#include <algorithm>

#define VERIFY(exp) { \
	if ((exp) != true) { \
		std::wstringstream buf; \
		buf << __FILE__ << ":" << __LINE__ << std::endl \
            << _CRT_WIDE(#exp); \
		MessageBoxW(NULL, buf.str().c_str(), appTitle.c_str(), MB_ICONERROR); \
		DebugBreak(); \
	} \
}

#define VERIFY_OK(exp) VERIFY((exp) == S_OK)
#define VERIFY_SUCCEEDED(expr) VERIFY(SUCCEEDED(expr))

static const CLSID CLSID_MMDeviceEnumerator = __uuidof(MMDeviceEnumerator);
static const IID IID_IMMDeviceEnumerator = __uuidof(IMMDeviceEnumerator);
static const IID IID_IAudioEndpointVolume = __uuidof(IAudioEndpointVolume);
static const IID IID_ISimpleAudioVolume = __uuidof(ISimpleAudioVolume);

//void test() {
//	const CLSID CLSID_MMDeviceEnumerator = __uuidof(MMDeviceEnumerator);
//	const IID IID_IMMDeviceEnumerator = __uuidof(IMMDeviceEnumerator);
//	const IID IID_IAudioEndpointVolume = __uuidof(IAudioEndpointVolume);
//	const IID IID_ISimpleAudioVolume = __uuidof(ISimpleAudioVolume);
//	const IID IID_IAudioSessionManager = __uuidof(IAudioSessionManager);
//
//	CComPtr<IMMDeviceEnumerator > m_pIMMEnumerator;
//	CComPtr<ISimpleAudioVolume>   m_pRenderSimpleVol;
//
//	CComPtr<IMMDevice> pIMMDeivce = NULL;
//	HRESULT hr = S_OK;
//	hr = CoCreateInstance(CLSID_MMDeviceEnumerator, NULL, CLSCTX_INPROC_SERVER,
//		IID_IMMDeviceEnumerator, (void**)&m_pIMMEnumerator);
//	//Audio
//	//hr = m_pIMMEnumerator->GetDefaultAudioEndpoint(eRender, eConsole, &pIMMDeivce);
//
//	CComPtr<IMMDeviceCollection> pDevCollection = NULL;
//	hr = m_pIMMEnumerator->EnumAudioEndpoints(eCapture, DEVICE_STATE_ACTIVE, &pDevCollection);
//	UINT devCnt = 0;
//	hr = pDevCollection->GetCount(&devCnt);
//	for (UINT i = 0; i < devCnt; i++) {
//		CComPtr<IMMDevice> pDev = NULL;
//		pDevCollection->Item(i, &pDev);
//		CComPtr<IPropertyStore> pProp;
//		hr = pDev->OpenPropertyStore(STGM_READ, &pProp);
//		PROPVARIANT name;
//		pProp->GetValue(PKEY_Device_FriendlyName, &name);
//
//		OutputDebugString(name.pwszVal);
//        OutputDebugString(L"\n");
//
//        volumes.push_back(NULL);
//        hr = pDev->Activate(IID_IAudioEndpointVolume, CLSCTX_ALL, NULL, (void**)&volumes.back());
//
//
//        callbacks.push_back(new CVolumeCallback(name.pwszVal));
//        volumes.back()->RegisterControlChangeNotify(callbacks.back());
//
//	}
//	//Microphone
//	hr = m_pIMMEnumerator->GetDefaultAudioEndpoint(eCapture, eConsole, &pIMMDeivce);
//    CComPtr<IAudioEndpointVolume> volume;
//	hr = pIMMDeivce->Activate(IID_IAudioEndpointVolume, CLSCTX_ALL, NULL, (void**)&volume);
//
//	BOOL bMute = FALSE;
//	hr = volume->GetMute(&bMute);
//	bMute = TRUE;
//	hr = volume->SetMute(bMute, NULL);
//}

MicCtrl::MicCtrl() {

}

MicCtrl::~MicCtrl() {
	if (devEnum) {
		devEnum->Release();
	}
}

void MicCtrl::Init() {
	VERIFY_OK(CoInitialize(NULL));
	VERIFY_OK(CoCreateInstance(CLSID_MMDeviceEnumerator, NULL, CLSCTX_INPROC_SERVER,
		IID_IMMDeviceEnumerator, (void**)&devEnum));
	ReloadDevices();
}

// Reload microphone devices. Should be called when device changed.
void MicCtrl::ReloadDevices() {
	std::for_each(audioCallbacks.begin(), audioCallbacks.end(), 
		[](auto& callback) {
			VERIFY_OK(callback.first->UnregisterControlChangeNotify(callback.second));
			callback.first->Release();
			callback.second->Release();
		});
	audioCallbacks.clear();

	IMMDeviceCollection* devCollection = NULL;
	VERIFY_OK(devEnum->EnumAudioEndpoints(eCapture, DEVICE_STATE_ACTIVE, &devCollection));
	UINT devCount = 0;
	VERIFY_OK(devCollection->GetCount(&devCount));
	for (UINT i = 0; i < devCount; i++) {
		IMMDevice* dev = NULL;
		VERIFY_OK(devCollection->Item(i, &dev));
		IPropertyStore* propStore = NULL;
		VERIFY_OK(dev->OpenPropertyStore(STGM_READ, &propStore));
		PROPVARIANT name;
		VERIFY_OK(propStore->GetValue(PKEY_Device_FriendlyName, &name));
		propStore->Release();

		IAudioEndpointVolume* volume = NULL;
		VERIFY_OK(dev->Activate(IID_IAudioEndpointVolume, CLSCTX_ALL, NULL, (void**)&volume));
		AudioEndpointVolumeCallback* callback = new AudioEndpointVolumeCallback(name.pwszVal);
		VERIFY_OK(volume->RegisterControlChangeNotify(callback));
		audioCallbacks.push_back(AudioVolumeCallback(volume, callback));
		dev->Release();
	}
	devCollection->Release();
}

bool MicCtrl::GetMuted() {
	IMMDeviceCollection* devCollection = NULL;
	VERIFY_OK(devEnum->EnumAudioEndpoints(eCapture, DEVICE_STATE_ACTIVE, &devCollection));
	UINT devCount = 0;
	VERIFY_OK(devCollection->GetCount(&devCount));
	BOOL bMute = TRUE;
	for (UINT i = 0; i < devCount; i++) {
		IMMDevice* dev = NULL;
		VERIFY_OK(devCollection->Item(i, &dev));
		IAudioEndpointVolume* volume = NULL;
		VERIFY_OK(dev->Activate(IID_IAudioEndpointVolume, CLSCTX_ALL, NULL, (void**)&volume));
		dev->Release();
		VERIFY_OK(volume->GetMute(&bMute));
		volume->Release();
		if (!bMute) {
			break;
		}
	}
	devCollection->Release();
	return bMute;
}

void MicCtrl::SetMuted(bool mute) {
	IMMDeviceCollection* devCollection = NULL;
	VERIFY_OK(devEnum->EnumAudioEndpoints(eCapture, DEVICE_STATE_ACTIVE, &devCollection));
	UINT devCount = 0;
	VERIFY_OK(devCollection->GetCount(&devCount));
	for (UINT i = 0; i < devCount; i++) {
		IMMDevice* dev = NULL;
		VERIFY_OK(devCollection->Item(i, &dev));
		IAudioEndpointVolume* volume = NULL;
		VERIFY_OK(dev->Activate(IID_IAudioEndpointVolume, CLSCTX_ALL, NULL, (void**)&volume));
		dev->Release();
		VERIFY_SUCCEEDED(volume->SetMute(mute, NULL));
		volume->Release();
	}
	devCollection->Release();
}