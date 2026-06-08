#define SECURITY_WIN32
#include "framework.h"
#include "TaskScheduler.h"
#include "Log.h"
#include "DeMic.h"

#include <initguid.h>
#include <taskschd.h>
#include <atlbase.h>
#include <comdef.h>
#include <sstream>

#pragma comment(lib, "taskschd.lib")
#pragma comment(lib, "comsuppw.lib")

#define FAILED_ERROR_LOG(msg, hr) \
    LOG(Logger::LevelError, (std::wstringstream() <<(msg) << L" failed. HRESULT: 0x" << std::hex << (hr)).str().c_str());

#include <windows.h>
#include <sddl.h>
#include <string>

std::wstring GetCurrentUserSid() {
    HANDLE hToken = nullptr;

    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &hToken)) {
        LOG_LAST_ERROR();
        return L""; 
    }

    DWORD size = 0;

    GetTokenInformation(hToken, TokenUser, nullptr, 0, &size);

    if (GetLastError() != ERROR_INSUFFICIENT_BUFFER) {
        LOG_LAST_ERROR();
        CloseHandle(hToken);
        return L"";
    }

    BYTE* buffer = new BYTE[size];

    if (!GetTokenInformation(hToken, TokenUser, buffer, size, &size)) {
        LOG_LAST_ERROR();
        delete[] buffer;
        CloseHandle(hToken);
        return L"";
    }

    PTOKEN_USER pUser = reinterpret_cast<PTOKEN_USER>(buffer);
    LPWSTR sidString = nullptr;
    if (!ConvertSidToStringSidW(pUser->User.Sid, &sidString)) {
		LOG_LAST_ERROR();
        delete[] buffer;
        CloseHandle(hToken);
        return L"";
    }

    std::wstring sid = sidString;

    LocalFree(sidString);
    delete[] buffer;
    CloseHandle(hToken);

    return sid;
}

static const std::wstring taskName(const std::wstring& sid) {
    return std::format(L"DeMic-{}", sid);
}

// RegisterHighestLogonTask registers a task to start this exe on logon with highest privileges.
bool RegisterLogonTask(const std::wstring& sid, bool asAdmin) {
    CComPtr<ITaskService> pService;
    HRESULT hr = CoCreateInstance(CLSID_TaskScheduler, NULL, CLSCTX_INPROC_SERVER, IID_ITaskService, (void**)&pService);
    if (FAILED(hr)) {
        FAILED_ERROR_LOG("CoCreateInstance", hr);
        return false;
    }

    hr = pService->Connect(_variant_t(), _variant_t(), _variant_t(), _variant_t());
    if (FAILED(hr)) {
		FAILED_ERROR_LOG("ITaskService::Connect", hr);
        return false;
    }
    CComPtr<ITaskFolder> pRootFolder;
    hr = pService->GetFolder(_bstr_t(L"\\"), &pRootFolder);
    if (FAILED(hr)) {
		FAILED_ERROR_LOG("ITaskService::GetFolder", hr)
        return false;
    }

    CComPtr<ITaskDefinition> pTask;
    hr = pService->NewTask(0, &pTask);
    if (FAILED(hr)) {
		FAILED_ERROR_LOG("ITaskService::NewTask", hr);
        return false;
    }

    CComPtr<IPrincipal> pPrincipal;
    hr = pTask->get_Principal(&pPrincipal);
    if (SUCCEEDED(hr)) {
        hr = pPrincipal->put_RunLevel(asAdmin ? TASK_RUNLEVEL_HIGHEST : TASK_RUNLEVEL_LUA); // !!!
        if (FAILED(hr)) {
            FAILED_ERROR_LOG("IPrincipal::put_RunLevel", hr);
            return false;
        }
        hr = pPrincipal->put_LogonType(TASK_LOGON_INTERACTIVE_TOKEN);
        if (FAILED(hr)) {
			FAILED_ERROR_LOG("IPrincipal::put_LogonType", hr);
            return false;
        }
		pPrincipal->put_UserId(_bstr_t(sid.c_str()));

    }

    CComPtr<ITaskSettings> pSettings;
    hr = pTask->get_Settings(&pSettings);
    if (SUCCEEDED(hr)) {
        pSettings->put_StartWhenAvailable(VARIANT_TRUE);
		pSettings->put_DisallowStartIfOnBatteries(VARIANT_FALSE);   // Allow start on battery power
        pSettings->put_StopIfGoingOnBatteries(VARIANT_FALSE);       // Do not stop if going on battery
        pSettings->put_ExecutionTimeLimit(_bstr_t(L"PT0S"));        // No limit
    }

    CComPtr<ITriggerCollection> pTriggerCollection;
    hr = pTask->get_Triggers(&pTriggerCollection);
    if (FAILED(hr)) {
		FAILED_ERROR_LOG("ITaskDefinition::get_Triggers", hr);
        return false;
    }

    CComPtr<ITrigger> pTrigger;
    hr = pTriggerCollection->Create(TASK_TRIGGER_LOGON, &pTrigger);
    if (FAILED(hr)) {
        FAILED_ERROR_LOG("ITriggerCollection::Create", hr);
        return false;
    }

    CComPtr<ILogonTrigger> pLogonTrigger;

    hr = pTrigger->QueryInterface(
        IID_ILogonTrigger,
        (void**)&pLogonTrigger);
    if (FAILED(hr)) {
        FAILED_ERROR_LOG("QueryInterface for ILogonTrigger", hr);
        return false;
    }
    pLogonTrigger->put_UserId(_bstr_t(sid.c_str()));


    CComPtr<IActionCollection> pActionCollection;
    hr = pTask->get_Actions(&pActionCollection);
    if (FAILED(hr)) {
        FAILED_ERROR_LOG("ITaskDefinition::get_Actions", hr);
        return false;
    }

    CComPtr<IAction> pAction;
    hr = pActionCollection->Create(TASK_ACTION_EXEC, &pAction);
    if (FAILED(hr)) {
        FAILED_ERROR_LOG("IActionCollection::Create", hr);
        return false;
    }

    CComPtr<IExecAction> pExecAction;
    hr = pAction->QueryInterface(IID_IExecAction, (void**)&pExecAction);
    if (SUCCEEDED(hr)) {
        hr = pExecAction->put_Path(_bstr_t(moduleFilePath.c_str()));
        if (FAILED(hr)) {
            FAILED_ERROR_LOG("IExecAction::put_Path", hr);
            return false;
        }
		pExecAction->put_Arguments(_bstr_t(L"/silent"));
    }


    CComPtr<IRegisteredTask> pRegisteredTask;
    hr = pRootFolder->RegisterTaskDefinition(
        _bstr_t(taskName(sid).c_str()),
        pTask,
        TASK_CREATE_OR_UPDATE,
        _variant_t(),
        _variant_t(),
        TASK_LOGON_INTERACTIVE_TOKEN,
        _variant_t(L""),
        &pRegisteredTask
    );

    if (FAILED(hr)) {
		FAILED_ERROR_LOG("ITaskFolder::RegisterTaskDefinition", hr);
        return false;
    }

    return true;
}

// IsTaskRegistered checks if a task with the specified exe path is registered in the root folder.
LogonTaskStatus GetLogonTaskStatus(const std::wstring& sid) {
    CComPtr<ITaskService> pService;
    HRESULT hr = CoCreateInstance(CLSID_TaskScheduler, NULL, CLSCTX_INPROC_SERVER, IID_ITaskService, (void**)&pService);
    if (FAILED(hr)) {
        FAILED_ERROR_LOG("CoCreateInstance", hr);
        return LTS_UNREGISTERED;
    }

    hr = pService->Connect(_variant_t(), _variant_t(), _variant_t(), _variant_t());
    if (FAILED(hr)) {
		FAILED_ERROR_LOG("ITaskService::Connect", hr);
        return LTS_UNREGISTERED;
    }

    CComPtr<ITaskFolder> pRootFolder;
    hr = pService->GetFolder(_bstr_t(L"\\"), &pRootFolder);
    if (FAILED(hr)) {
        FAILED_ERROR_LOG("ITaskService::GetFolder", hr);
        return LTS_UNREGISTERED;
    }

    CComPtr<IRegisteredTask> pRegisteredTask;
    hr = pRootFolder->GetTask(_bstr_t(taskName(sid).c_str()), &pRegisteredTask);
    if (!SUCCEEDED(hr)) {
        FAILED_ERROR_LOG("ITaskFolder::GetTask", hr);
        return LTS_UNREGISTERED;
    }

    TASK_STATE taskState = TASK_STATE_UNKNOWN;
    hr = pRegisteredTask->get_State(&taskState);

    if (SUCCEEDED(hr)) {
            switch (taskState) {
            case TASK_STATE_UNKNOWN:
            case TASK_STATE_DISABLED:
                return LTS_UNREGISTERED;
            default: {
                CComPtr<ITaskDefinition> pDefinition;
                hr = pRegisteredTask->get_Definition(&pDefinition);
                if (FAILED(hr)) {
                    FAILED_ERROR_LOG("IRegisteredTask::get_Definition", hr);
                    return LTS_REGISTERED; // Can't determine, but the task exists.
                }
                CComPtr<IPrincipal> pPrincipal;
                hr = pDefinition->get_Principal(&pPrincipal);
                if (FAILED(hr)) {
                    FAILED_ERROR_LOG("ITaskDefinition::get_Principal", hr);
                    return LTS_REGISTERED; // Can't determine, but the task exists.
                }
                TASK_RUNLEVEL_TYPE level = TASK_RUNLEVEL_LUA;
                pPrincipal->get_RunLevel(&level);
                return level == TASK_RUNLEVEL_HIGHEST ? LTS_REGISTERED_AS_ADMIN : LTS_REGISTERED;
            }
        }
    }
    return LTS_UNREGISTERED;
}

// IsCurrentProcessElevated checks if the current process is running with elevated privileges (as administrator).
bool IsCurrentProcessElevated() {
    bool isElevated = false;
    HANDLE hToken = NULL;

    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &hToken)) {
        return false;
    }
    TOKEN_ELEVATION elevation;
    DWORD cbSize = sizeof(TOKEN_ELEVATION);
    if (GetTokenInformation(hToken, TokenElevation, &elevation, sizeof(elevation), &cbSize)) {
        isElevated = (elevation.TokenIsElevated != 0);
    }
    CloseHandle(hToken);

    return isElevated;
}

// UnregisterTask unregisters the task with the specified exe path.
bool UnregisterLogonTask(const std::wstring& sid) {
    CComPtr<ITaskService> pService;
    HRESULT hr = CoCreateInstance(CLSID_TaskScheduler, NULL, CLSCTX_INPROC_SERVER, IID_ITaskService, (void**)&pService);
    if (FAILED(hr)) {
        FAILED_ERROR_LOG("CoCreateInstance", hr);
        return false;
    }

    hr = pService->Connect(_variant_t(), _variant_t(), _variant_t(), _variant_t());
    if (FAILED(hr)) {
        FAILED_ERROR_LOG("ITaskService::Connect", hr);
        return false;
    }

    CComPtr<ITaskFolder> pRootFolder;
    hr = pService->GetFolder(_bstr_t(L"\\"), &pRootFolder);
    if (FAILED(hr)) {
		FAILED_ERROR_LOG("ITaskService::GetFolder", hr);
        return false;
    }

    hr = pRootFolder->DeleteTask(_bstr_t(taskName(sid).c_str()), 0);

    if (SUCCEEDED(hr)) {
        return true;
    }

    // No such task.
    return hr == HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND);
}