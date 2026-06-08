#pragma once

#include <string>

// RegisterLogonTask registers a task to start this program on user logon.
// The task will run with highest privileges if asAdmin is true.
bool RegisterLogonTask(const std::wstring& sid, bool asAdmin);
// UnregisterLogonTask unregisters the logon task for the user.
bool UnregisterLogonTask(const std::wstring& sid);
enum LogonTaskStatus{
	LTS_UNREGISTERED,			// No task registered.
	LTS_REGISTERED,				// A task is registered to start on logon.
	LTS_REGISTERED_AS_ADMIN,	// A task is registered to start on logon with highest privileges.
};
// GetLogonTaskStatus returns the logon task registration status for the user with the specified sid.
LogonTaskStatus GetLogonTaskStatus(const std::wstring& sid);
// IsCurrentProcessElevated returns true if the current process is running with elevated privileges(as administrator).
bool IsCurrentProcessElevated();
// GetCurrentUserSid returns the SID of the current user.
std::wstring GetCurrentUserSid();