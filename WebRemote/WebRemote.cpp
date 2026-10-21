#include "pch.h"
#include "resource.h"
#include "../Util.h"
#include "NetUtil.h"
#include "../sdk/DemicPluginUtil.h"

#include "WebRemote.h"
#include "Server.h"
#include "MessageWindow.h"
#include "VerificationCode.h"
#include "ConfigListenAddr.h"
#include "../TimeDebouncer.h"

#include <format>
#include <boost/json.hpp>
#include <fstream>

#include <openssl/ssl.h>

#include <shellapi.h>

HINSTANCE hInstance = NULL;
std::filesystem::path moduleFilePath;
std::filesystem::path configFilePath;
std::unique_ptr <StringRes> strRes;

extern DeMic_PluginInfo plugin;
std::vector<wchar_t> pluginName;


static const char* const CONFIG_ENABLED = "Enabled";
static const char* const CONFIG_SERVER_LISTEN_HOST = "ServerListenHost";
static const char* const CONFIG_SERVER_LISTEN_PORT = "ServerListenPort";
static const char* const CONFIG_ENABLE_HTTPS = "EnableHTTPS";
static const char* const CONFIG_HTTPS_CONFIG = "HTTPSConfig";
static const char* const CONFIG_HTTPS_CERT_PEM_FILE_PATH = "CertPem";
static const char* const CONFIG_HTTPS_KEY_PEM_FILE_PATH = "KeyPem";
static const char* const CONFIG_NO_VERIFICATION_CODE = "!NoVerificationCode!";

Configuration config;

static void ReadConfig() {
    namespace json = boost::json;
    std::ifstream in(configFilePath);
    if (!in) {
        return;
    }
    try {
        auto configJson = json::parse(in).as_object();
        config.Enabled = configJson[CONFIG_ENABLED].as_bool();
        config.ServerListenHost = configJson[CONFIG_SERVER_LISTEN_HOST].as_string();
        config.ServerListenPort = configJson[CONFIG_SERVER_LISTEN_PORT].as_string();
		config.EnableHTTPS = configJson[CONFIG_ENABLE_HTTPS].as_bool();
        if (configJson.contains(CONFIG_HTTPS_CONFIG)) {
            auto& httpsConfigJson = configJson[CONFIG_HTTPS_CONFIG].as_object();
            config.HTTPSConfig.CertPemFilePath = httpsConfigJson[CONFIG_HTTPS_CERT_PEM_FILE_PATH].as_string();
            config.HTTPSConfig.KeyPemFilePath = httpsConfigJson[CONFIG_HTTPS_KEY_PEM_FILE_PATH].as_string();
        }
		config.NoVerificationCode = configJson.contains(CONFIG_NO_VERIFICATION_CODE) && configJson[CONFIG_NO_VERIFICATION_CODE].as_bool();
    } catch (const std::exception&) {
        ShowError(host, state, (strRes->Load(IDS_READ_CONFIG_FAILED) + configFilePath.c_str()).c_str());
    }
}

void WriteConfig() {
    namespace json = boost::json;
    json::object configJson;
	configJson[CONFIG_ENABLED] = config.Enabled;
    configJson[CONFIG_SERVER_LISTEN_HOST] = config.ServerListenHost;
    configJson[CONFIG_SERVER_LISTEN_PORT] = config.ServerListenPort;
	configJson[CONFIG_ENABLE_HTTPS] = config.EnableHTTPS;
    configJson[CONFIG_HTTPS_CONFIG] = json::object{
        {CONFIG_HTTPS_CERT_PEM_FILE_PATH, config.HTTPSConfig.CertPemFilePath},
        {CONFIG_HTTPS_KEY_PEM_FILE_PATH, config.HTTPSConfig.KeyPemFilePath}
	};
    if (config.NoVerificationCode) {
		configJson[CONFIG_NO_VERIFICATION_CODE] = true;
    }
    std::ofstream out(configFilePath);
    out << std::setw(2) << json::serialize(configJson);
    if (out.fail()) {
        ShowError(host, state, strRes->Load(IDS_SAVE_CONFIG_FAILED).c_str());
    }
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD  ul_reason_for_call, LPVOID lpReserved) {
    switch (ul_reason_for_call) {
    case DLL_PROCESS_ATTACH: {
        hInstance = hModule;
		moduleFilePath = GetModuleFilePath(hModule);
		configFilePath =moduleFilePath.replace_extension(L".json").wstring();
        strRes = std::make_unique<StringRes>(hModule);
        pluginName = DupCStr(strRes->Load(IDS_APP_NAME));
        plugin.Name = &pluginName[0];
        break;
    }
	case DLL_THREAD_ATTACH:
        OPENSSL_thread_stop();
        break;
    case DLL_PROCESS_DETACH:
        if (lpReserved != nullptr) {
            break; // do not do cleanup if process termination scenario
        }
        break;
    }
    return TRUE;
}

DeMic_Host* host = NULL;
void* state = NULL;

static std::wstring formatErrorMessage(UINT resId, std::wstring& message) {
    auto& reason = strRes->Load(resId);
    if (message.empty()) {
        return reason;
    }
    return reason + L" " + message;
}

// static member of template class must be defined for each instantiation.
std::map<UINT_PTR, TimeDebouncer<>*> TimeDebouncer<>::sInstances;

static const UINT SILENT_STARTING_RETRY_INTERVAL = 3000;
static const int SILENT_STARTING_MAX_RETRY = 4;

static bool TrySilentStarting();

static TimeDebouncer<> retrySilentStartingDebouncer(
    SILENT_STARTING_RETRY_INTERVAL, [] {
        static int retryCount = 0;
        if (retryCount >= SILENT_STARTING_MAX_RETRY) {
			// Last retry, show prompt to user if failed.
            config.Enabled = StartHTTPServerWithPrompt(config);
            return;
        }
        retryCount++;

        if (TrySilentStarting()) {
			retrySilentStartingDebouncer.Cancel();
			return; // No need to retry.
		}
        retrySilentStartingDebouncer.Emit(); // Retry.
    },
    []() {LOG_LAST_ERROR(host, state); }
);

// Returns true if the HTTP server is started successfully or an error occured that should stop retrying.
static bool TrySilentStarting() {
    std::wstring errorMessage;
    const auto status = StartHTTPServer(config, errorMessage);
    if (status == SERVER_OK) {
        config.Enabled = true;
        return true; // Succeed.
    }
    std::wstring message;
    switch (status) {
    case SERVER_EMPTY_CERT_OR_KEY_FILE:
        message = strRes->Load(IDS_EMPTY_CERT_OR_KEY_FILE);
        break;
    case SERVER_INVALID_ADDRESS_FORMAT:
        message = formatErrorMessage(IDS_INVALID_ADDRESS_FORMAT, errorMessage);
        break;
    case SERVER_INVALID_PORT:
        message = formatErrorMessage(IDS_INVALID_PORT, errorMessage).c_str();
        break;
    case SERVER_ERROR:
        message = formatErrorMessage(IDS_SERVER_START_ERROR, errorMessage);
        break;
    }
    if (message.empty()) {
		return false; // Temporary network error, retry.
    }
    // Failed due to an error other than temporary network errors,
    // stop retrying and show error message to user.
    config.Enabled = false;
    ShowError(host, state, message.c_str());
    return true;
}

// Initiates the HTTP server starting in silent mode, with retrying on temporary network errors.
static void InitiateHTTPServerSilentStarting() {
    if(TrySilentStarting()) {
        return; // Succeed, no need to retry.
	}
    retrySilentStartingDebouncer.Emit();
}

// Cancels the HTTP server silent starting, if any.
static void CancelHTTPServerSilentStarting() {
    retrySilentStartingDebouncer.Cancel();
}

static bool SilentMode() {
    bool ret = false;
    int argc = 0;
    wchar_t** argv = CommandLineToArgvW(GetCommandLine(), &argc);
    if (argc > 1) {
        const auto argv1 = std::wstring(argv[1]);
        ret = (argv1 == L"/silent" || argv1 == L"-silent");
    }
    LocalFree(argv);
    return ret;
}

static HMENU subMenu = NULL;

static UINT enableMenuItemId = 0;
static UINT showVerificationCodeMenuItemId = 0;
static UINT configListenAddrMenuItemID = 0;

static BOOL OnLoaded(DeMic_Host* h, DeMic_OnLoadedArgs* args) {
    host = h;
    state = args->State;
    
    ReadConfig();

    host->SetMicMuteStateListener(state, [] {
        NotifyStateChange(host->GetMuteState());
        });
    NotifyStateChange(host->GetMuteState());

	subMenu = CreatePopupMenu();
    if (subMenu == NULL) {
        LOG_LAST_ERROR(host, state);
        return FALSE;
    }

	UINT nextUsableID = args->FirstMenuItemID;


    auto itemTitle = strRes->Load(IDS_ENABLE);
    MENUITEMINFOW item = { sizeof(item), 0 };


    item.fMask = MIIM_STRING | MIIM_ID;
    item.dwTypeData = itemTitle.data();
    item.cch = UINT(itemTitle.length());
    item.wID = (enableMenuItemId = nextUsableID++);
    if (!InsertMenuItemW(subMenu, 0, TRUE, &item)) {
        LOG_LAST_ERROR(host, state);
        return FALSE;
    }

    item = { sizeof(item), 0 };
    item.fMask = MIIM_FTYPE;
    item.fType = MFT_SEPARATOR;
    if (!InsertMenuItemW(subMenu, 1, TRUE, &item)) {
        LOG_LAST_ERROR(host, state);
        return FALSE;
    }

    itemTitle = strRes->Load(IDS_SHOW_VERIFICATION_CODE);
    item = { sizeof(item), 0 };
    item.fMask = MIIM_STRING | MIIM_ID;
    item.dwTypeData = itemTitle.data();
    item.cch = UINT(itemTitle.length());
    item.wID = (showVerificationCodeMenuItemId = nextUsableID++);
    if (!InsertMenuItemW(subMenu, 2, TRUE, &item)) {
        LOG_LAST_ERROR(host, state);
        return FALSE;
    }

    itemTitle = strRes->Load(IDS_CONFIG_LISTEN_ADDR);
    item = { sizeof(item), 0 };
    item.fMask = MIIM_STRING | MIIM_ID;
    item.dwTypeData = itemTitle.data();
    item.cch = UINT(itemTitle.length());
    item.wID = (configListenAddrMenuItemID = nextUsableID++);
    if (!InsertMenuItemW(subMenu, 3, TRUE, &item)) {
        LOG_LAST_ERROR(host, state);
        return FALSE;
    }

    MENUITEMINFOW rootMenuItem = { sizeof(rootMenuItem), 0 };
    rootMenuItem.fMask = MIIM_STRING | MIIM_SUBMENU;
    rootMenuItem.dwTypeData = &pluginName[0];
    rootMenuItem.cch = UINT(pluginName.size() - 1);
    rootMenuItem.hSubMenu = subMenu;
    if (!host->CreateRootMenuItem(state, &rootMenuItem)) {
        LOG_LAST_ERROR(host, state);
		DestroyMenu(subMenu);
        return FALSE;
    }
    host->SetInitMenuPopupListener(state, subMenu, [](HMENU menu) {
		EnableMenuItem(subMenu, showVerificationCodeMenuItemId, MF_BYCOMMAND | (HTTPServerRunning() ? MF_ENABLED : MF_DISABLED));

        auto u8HostPort = net_util::JoinHostPort(config.ServerListenHost, config.ServerListenPort);
		auto hostPort = FromUTF8(std::u8string_view((const char8_t*)u8HostPort.data(), u8HostPort.size()));
        ModifyMenuW(subMenu, 
            enableMenuItemId, MF_BYCOMMAND | MF_STRING 
                | (HTTPServerRunning() ? MF_CHECKED : MF_UNCHECKED)
                | (config.ServerListenPort.empty() ? MF_DISABLED : MF_ENABLED),
            enableMenuItemId, 
            config.ServerListenPort.empty() 
                ? strRes->Load(IDS_ENABLE).c_str() 
                : std::format(L"{} - http{}://{}", strRes->Load(IDS_ENABLE), config.EnableHTTPS ? L"s" : L"", hostPort).c_str()
        );
	});

    if (!CreateMessageWindow()) {
        LOG_LAST_ERROR(host, state);
		host->DeleteRootMenuItem(state);
        DestroyMenu(subMenu);
        return FALSE;
    }

    InitHTTPServer();

    if (config.Enabled) {
        if (config.ServerListenPort.empty()) {
            ShowError(host, state, strRes->Load(IDS_LACK_CONFIG).c_str());
            ShowConfigListenAddrDialog();
        }

        if (SilentMode()) {
            InitiateHTTPServerSilentStarting();
        } else {
            config.Enabled = StartHTTPServerWithPrompt(config);
        }
    }
    
    return TRUE;
}

bool StartHTTPServerWithPrompt(const Configuration& config, HWND parent) {
    std::wstring errorMessage;
    auto status = StartHTTPServer(config, errorMessage);
    if (status != SERVER_OK) {
        std::wstring message;
        switch (status) {
        case SERVER_EMPTY_CERT_OR_KEY_FILE:
            message = strRes->Load(IDS_EMPTY_CERT_OR_KEY_FILE);
			break;
        case SERVER_INVALID_ADDRESS_FORMAT:
            message = formatErrorMessage(IDS_INVALID_ADDRESS_FORMAT, errorMessage);
            break;
        case SERVER_INVALID_PORT:
            message = formatErrorMessage(IDS_INVALID_PORT, errorMessage).c_str();
            break;
        case SERVER_RESOLVE_ENDPOINT:
            message = formatErrorMessage(IDS_RESOLVE_ENDPOINT, errorMessage);
            break;
        case SERVER_BIND_ERROR:
            message = formatErrorMessage(IDS_SERVER_BIND_ERROR, errorMessage);
            break;
        case SERVER_LISTEN_ERROR:
            message = formatErrorMessage(IDS_SERVER_LISTEN_ERROR, errorMessage);
            break;
        case SERVER_ERROR:
            message = formatErrorMessage(IDS_SERVER_START_ERROR, errorMessage);
            break;
        }

        ShowError(host, state, message.c_str(), parent);
        return false;
    }
    return true;
}

static void OnMenuItemCmd(UINT id) {
	CancelHTTPServerSilentStarting(); // User interaction, cancel silent starting if any.

    if (id == enableMenuItemId) {
        if(HTTPServerRunning()) {
            StopHTTPServer();
			config.Enabled = false;
        } else {
            config.Enabled = StartHTTPServerWithPrompt(config);
		}
        WriteConfig();
    } else if(id == showVerificationCodeMenuItemId) {
		ShowVerificationCodeDialog();
    } else if(id == configListenAddrMenuItemID) {
        ShowConfigListenAddrDialog();
    }
}

static void OnUnload() {
    CancelHTTPServerSilentStarting();
    StopHTTPServer();
    DestroyVerificationCodeDialog();
    DestroyMessageWindow();
    DestroyMenu(subMenu);
    WriteConfig();
}


static DeMic_PluginInfo plugin = {
    DEMIC_CURRENT_SDK_VERSION,
    L"Web Remote",	/*Name*/
    {1, 3},			/*Version*/
    OnLoaded,		/*OnLoaded*/
    OnMenuItemCmd,	/*OnMenuItemCmd*/
	OnUnload,	    /*OnUnload*/

};

extern "C" DeMic_PluginInfo* GetPluginInfo(void) {
    return &plugin;
}