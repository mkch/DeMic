
#include "../sdk/DeMicPlugin.h"
#include "../Util.h"

extern HINSTANCE hInstance;
extern StringRes* strRes;
extern DeMic_Host* demicHost;
extern void* demicState;

enum HOTKEY_TYPE {
    TYPE_NONE,
    TYPE_TOGGLE,
    TYPE_ON_OFF,
    TYPE_PTT,
    TYPE_PTM,
};

static const HOTKEY_TYPE hotkeyTypes[] = {
    TYPE_TOGGLE,
    TYPE_ON_OFF,
    TYPE_PTT,
    TYPE_PTM,
};

extern std::vector<std::wstring> hotkeyTypeNames;

struct Config {
	HOTKEY_TYPE Type = TYPE_NONE;
    WORD Hotkey = 0; // TYPE_TOGGLE, TYPE_PTT, TYPE_PTM, TYPE_ON_OFF(for ON)
    WORD Hotkey2 = 0; // TYPE_ON_OFF(for OFF)
};

extern Config config;

void ReadConfig();
void WriteConfig();
int GetHotKeyTypeIndex(HOTKEY_TYPE type);

