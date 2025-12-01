#include "framework.h"

#include "MicCtrl.h"
#include <Audioclient.h>
#include <Audiopolicy.h>
#include <Functiondiscoverykeys_devpkey.h>
#include <string>
#include <vector>
#include <algorithm>
#include "Log.h"

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

MicCtrl::MicCtrl() :notifClient(new MMNotificationClient()) {
	VERIFY_SUCCEEDED(CoInitialize(NULL));
	VERIFY_SUCCEEDED(CoCreateInstance(CLSID_MMDeviceEnumerator, NULL, CLSCTX_INPROC_SERVER,
		IID_IMMDeviceEnumerator, (void**)&devEnum));
	VERIFY_SUCCEEDED(devEnum->RegisterEndpointNotificationCallback(notifClient));
	ReloadDevices();
}

MicCtrl::~MicCtrl() {
	UnregisterAudioCallbacks();
	VERIFY_SUCCEEDED(devEnum->UnregisterEndpointNotificationCallback(notifClient));
	notifClient->Release();
	devEnum->Release();
}

void MicCtrl::UnregisterAudioCallbacks() {
	std::for_each(audioCallbacks.begin(), audioCallbacks.end(),
		[](auto& callback) {
			VERIFY_SUCCEEDED(callback.first->UnregisterControlChangeNotify(callback.second));
			callback.first->Release();
			callback.second->Release();
	});
}

// Reload microphone devices. Should be called when device changed.
void MicCtrl::ReloadDevices() {
	UnregisterAudioCallbacks();
	audioCallbacks.clear();

	IMMDeviceCollection* devCollection = NULL;
	VERIFY_SUCCEEDED(devEnum->EnumAudioEndpoints(eCapture, DEVICE_STATE_ACTIVE, &devCollection));
	UINT devCount = 0;
	VERIFY_SUCCEEDED(devCollection->GetCount(&devCount));
	for (UINT i = 0; i < devCount; i++) {
		IMMDevice* dev = NULL;
		VERIFY_SUCCEEDED(devCollection->Item(i, &dev));
		IPropertyStore* propStore = NULL;
		VERIFY_SUCCEEDED(dev->OpenPropertyStore(STGM_READ, &propStore));
		PROPVARIANT name;
		VERIFY_SUCCEEDED(propStore->GetValue(PKEY_Device_FriendlyName, &name));
		propStore->Release();

		IAudioEndpointVolume* volume = NULL;
		VERIFY_SUCCEEDED(dev->Activate(IID_IAudioEndpointVolume, CLSCTX_ALL, NULL, (void**)&volume));
		AudioEndpointVolumeCallback* callback = new AudioEndpointVolumeCallback(name.pwszVal);
		VERIFY_SUCCEEDED(volume->RegisterControlChangeNotify(callback));
		audioCallbacks.push_back(AudioVolumeCallback(volume, callback));
		dev->Release();
	}
	devCollection->Release();
}

bool MicCtrl::GetMuted() {
	IMMDeviceCollection* devCollection = NULL;
	VERIFY_SUCCEEDED(devEnum->EnumAudioEndpoints(eCapture, DEVICE_STATE_ACTIVE, &devCollection));
	UINT devCount = 0;
	VERIFY_SUCCEEDED(devCollection->GetCount(&devCount));
	BOOL bMute = TRUE;
	for (UINT i = 0; i < devCount; i++) {
		IMMDevice* dev = NULL;
		VERIFY_SUCCEEDED(devCollection->Item(i, &dev));
		LPWSTR id = NULL;
		VERIFY_SUCCEEDED(dev->GetId(&id));
		if (devFilter && !devFilter(id)) {
			CoTaskMemFree(id);
			dev->Release();
			continue;
		}
		CoTaskMemFree(id);
		IAudioEndpointVolume* volume = NULL;
		VERIFY_SUCCEEDED(dev->Activate(IID_IAudioEndpointVolume, CLSCTX_ALL, NULL, (void**)&volume));
		dev->Release();
		VERIFY_SUCCEEDED(volume->GetMute(&bMute));
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
	VERIFY_SUCCEEDED(devEnum->EnumAudioEndpoints(eCapture, DEVICE_STATE_ACTIVE, &devCollection));
	UINT devCount = 0;
	VERIFY_SUCCEEDED(devCollection->GetCount(&devCount));
	for (UINT i = 0; i < devCount; i++) {
		IMMDevice* dev = NULL;
		VERIFY_SUCCEEDED(devCollection->Item(i, &dev));
		LPWSTR id = NULL;
		VERIFY_SUCCEEDED(dev->GetId(&id));
		if (devFilter && !devFilter(id)) {
			CoTaskMemFree(id);
			dev->Release();;
			continue;
		}
		CoTaskMemFree(id);
		IAudioEndpointVolume* volume = NULL;
		VERIFY_SUCCEEDED(dev->Activate(IID_IAudioEndpointVolume, CLSCTX_ALL, NULL, (void**)&volume));
		dev->Release();
		VERIFY_SUCCEEDED(volume->SetMute(mute, NULL));
		volume->Release();
	}
	devCollection->Release();
}

std::vector<std::wstring> MicCtrl::GetActiveDevices() {
	IMMDeviceCollection* devCollection = NULL;
	VERIFY_SUCCEEDED(devEnum->EnumAudioEndpoints(eCapture, DEVICE_STATE_ACTIVE, &devCollection));
	UINT devCount = 0;
	VERIFY_SUCCEEDED(devCollection->GetCount(&devCount));
	std::vector<std::wstring> ids;
	for (UINT i = 0; i < devCount; i++) {
		IMMDevice* dev = NULL;
		VERIFY_SUCCEEDED(devCollection->Item(i, &dev));
		
		LPWSTR id = NULL;
		VERIFY_SUCCEEDED(dev->GetId(&id));
		ids.push_back(id);
		CoTaskMemFree(id);

		dev->Release();
	}
	devCollection->Release();
	return ids;
}

std::wstring MicCtrl::GetDevName(const wchar_t* devID) {
	IMMDevice* dev = NULL;
	if (devEnum->GetDevice(devID, &dev) != S_OK) {
		return L"";
	}
	IPropertyStore* propStore = NULL;
	VERIFY_SUCCEEDED(dev->OpenPropertyStore(STGM_READ, &propStore));
	dev->Release();
	PROPVARIANT name;
	VERIFY_SUCCEEDED(propStore->GetValue(PKEY_Device_FriendlyName, &name));
	propStore->Release();
	return name.pwszVal;
}

std::wstring MicCtrl::GetDevIfaceName(const wchar_t* devID) {
	IMMDevice* dev = NULL;
	if (devEnum->GetDevice(devID, &dev) != S_OK) {
		return L"";
	}
	IPropertyStore* propStore = NULL;
	VERIFY_SUCCEEDED(dev->OpenPropertyStore(STGM_READ, &propStore));
	dev->Release();
	PROPVARIANT name;
	VERIFY_SUCCEEDED(propStore->GetValue(PKEY_Device_FriendlyName, &name));
	propStore->Release();
	return name.pwszVal;
}

int MicCtrl::GetDevMuted(const wchar_t* devID) {
	IMMDevice* dev = NULL;
	if (devEnum->GetDevice(devID, &dev) != S_OK) {
		return -1;
	}
	DWORD state = 0;
	VERIFY_SUCCEEDED(dev->GetState(&state));
	if (state != DEVICE_STATE_ACTIVE) {
		return -1;
	}
	IAudioEndpointVolume* volume = NULL;
	VERIFY_SUCCEEDED(dev->Activate(IID_IAudioEndpointVolume, CLSCTX_ALL, NULL, (void**)&volume));
	dev->Release();
	BOOL mute = FALSE;
	VERIFY_SUCCEEDED(volume->GetMute(&mute));
	volume->Release();
	return mute ? 1 : 0;
}

std::wstring MicCtrl::GetDefaultMicphone() {
	IMMDevice* dev = NULL;
	auto ret = devEnum->GetDefaultAudioEndpoint(eCapture, eConsole, &dev);
	if (ret == S_OK) {
		LPWSTR devID = NULL;
		VERIFY_SUCCEEDED(dev->GetId(&devID));
		dev->Release();
		std::wstring strDevID(devID);
		CoTaskMemFree(devID);
		return strDevID;
	}
	VERIFY(ret == E_NOTFOUND);
	return L"";
}