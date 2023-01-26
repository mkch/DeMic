#pragma once
#include <string>
#include <sstream>
#include <vector>
#include <cassert>
#include <Mmdeviceapi.h>
#include <Endpointvolume.h>

// The title of application.
extern std::wstring appTitle;
// The main window of application.
extern HWND mainWindow;

class MicCtrl {
public:
    // The window message posted to mainWindow when micophone muted state is changed.
    static const int WM_MUTED_STATE_CHANGED = WM_USER + 100;
private:
    class AudioEndpointVolumeCallback : public IAudioEndpointVolumeCallback {
        LONG _cRef;
        std::wstring devName;
        int muted = -1; // -1: uninitialized, 0: false, 1: true
    public:
        AudioEndpointVolumeCallback(const std::wstring& dev) :
            _cRef(1), devName(dev) {
        }

        ULONG STDMETHODCALLTYPE AddRef(){
            return InterlockedIncrement(&_cRef);
        }

        ULONG STDMETHODCALLTYPE Release(){
            ULONG ulRef = InterlockedDecrement(&_cRef);
            if (0 == ulRef){
                delete this;
            }
            return ulRef;
        }

        HRESULT STDMETHODCALLTYPE QueryInterface(REFIID  riid,VOID** ppvInterface){
            if (IID_IUnknown == riid){
                AddRef();
                *ppvInterface = (IUnknown*)this;
            }else if (__uuidof(IAudioEndpointVolumeCallback) == riid){
                AddRef();
                *ppvInterface = (IAudioEndpointVolumeCallback*)this;
            }else{
                *ppvInterface = NULL;
                return E_NOINTERFACE;
            }
            return S_OK;
        }

        HRESULT STDMETHODCALLTYPE OnNotify(PAUDIO_VOLUME_NOTIFICATION_DATA pNotify) {
            if (muted == pNotify->bMuted) {
                return S_OK; // State is not changed.
            }
            if (pNotify->bMuted) {
                //OutputDebugString(devName.c_str());
                //OutputDebugString(L" Muted\n");

                muted = 1;
            } else {
                //OutputDebugString(devName.c_str());
                //OutputDebugString(L" Unmuted\n");
                muted = 0;
            }
            SendMessage(mainWindow, WM_MUTED_STATE_CHANGED, 0, 0);
            return S_OK;
        }
    };
	typedef std::pair<IAudioEndpointVolume*, AudioEndpointVolumeCallback*> AudioVolumeCallback;
private:
    IMMDeviceEnumerator* devEnum = NULL;
    std::vector<AudioVolumeCallback> audioCallbacks;
    BOOL (*devFilter)(const wchar_t* devName);
public:
public:
	MicCtrl();
	~MicCtrl();
    void Init();
	// Reload microphone devices. Should be called when device changed.
	void ReloadDevices();
	// Returns whether all active microphone devices are muted.
	bool GetMuted();
	// Sets muted state of all active microphone devices.
	void SetMuted(bool mute);
    // Returns all active microphone device IDs.
    std::vector<std::wstring> GetActiveDevices();
    // Gets the friendly name of the endpoint device 
    // (for example, "Speakers (XYZ Audio Adapter)").
    // Returns empty string if devID is invalid.
    std::wstring GetDevName(const wchar_t* devID);
    // Get the friendly name of the audio adapter 
    // to which the endpoint device is attached (for example, "XYZ Audio Adapter").
    // Returns empty string if devID is invalid.
    std::wstring GetDevIfaceName(const wchar_t* devID);
    // Returns whether a microphone device is muted.
    // Returns 1 if the device is muted, 0 if the device is not muted,
    // -1 if the devID is invalid.
    int GetDevMuted(const wchar_t* devID);
    // Sets a filter function which defines the set of microphone devices
    // to operate.
    void SetDevFilter(BOOL (*f)(const wchar_t*)) { devFilter = f; }
};