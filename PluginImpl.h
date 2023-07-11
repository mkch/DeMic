#pragma once
#include<string>

class Plugin {
public:
	Plugin(const std::wstring displayName, UINT firstMenuItemID, UINT lastMenuItemID)
		:DisplayName(displayName),
		RootMenuItemID(firstMenuItemID), 
		FirstMenuItemID(firstMenuItemID), 
		LastMenuItemID(lastMenuItemID) {}
public:
	const std::wstring DisplayName;
	bool RootMenuItemCreated = false;
	const UINT RootMenuItemID;
	const UINT FirstMenuItemID;
	const UINT LastMenuItemID;
public:
	virtual bool OnLoaded() = 0;
	virtual void Unload() = 0;
	virtual bool HasDevFilter() = 0;
	virtual BOOL CallDevFilter(const wchar_t*) = 0;
	virtual void CallMicMuteStateListener() = 0;
	virtual void CallInitMenuPopupListener(HMENU) = 0;
	virtual void CallDefaultDevChangedListener() = 0;
	virtual void CallOnMenuItemCmd(UINT) = 0;
	virtual ~Plugin() {}
};

class PluginName {
public:
	PluginName(const std::wstring& path, const std::wstring& displayName)
		:Path(path), DisplayName(displayName) {}
public:
	std::wstring Path;
	std::wstring DisplayName;
};
