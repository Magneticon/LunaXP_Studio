#pragma once
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#ifdef LUNADARKMODE_EXPORTS
#define LUNA_DM_API
#else
#define LUNA_DM_API __declspec(dllimport)
#endif

#ifdef __cplusplus
extern "C" {
#endif

enum LUNA_PREFERRED_APP_MODE
{
    LunaAppModeDefault    = 0,
    LunaAppModeAllowDark  = 1,
    LunaAppModeForceDark  = 2,
    LunaAppModeForceLight = 3
};

/* Stable Luna-named API */
BOOL WINAPI LunaShouldAppsUseDarkMode(void);
BOOL WINAPI LunaShouldSystemUseDarkMode(void);
BOOL WINAPI LunaGetDarkModePreference(BOOL* appsDark, BOOL* systemDark);
LONG WINAPI LunaSetDarkModePreference(BOOL appsDark, BOOL systemDark);
int  WINAPI LunaSetPreferredAppMode(int mode);
BOOL WINAPI LunaAllowDarkModeForWindow(HWND hwnd, BOOL allow);
BOOL WINAPI LunaIsDarkModeAllowedForWindow(HWND hwnd);
void WINAPI LunaRefreshDarkModePolicy(void);

/*
    Compatibility-style aliases.

    These deliberately use names familiar from newer Windows dark-mode
    implementations, but they live in LunaDarkMode.dll, not in XP's uxtheme.dll.
    Source-compatible applications can dynamically resolve these names from
    LunaDarkMode.dll when running on XP.
*/
BOOL WINAPI ShouldAppsUseDarkMode(void);
BOOL WINAPI ShouldSystemUseDarkMode(void);
int  WINAPI SetPreferredAppMode(int mode);
BOOL WINAPI AllowDarkModeForWindow(HWND hwnd, BOOL allow);
BOOL WINAPI IsDarkModeAllowedForWindow(HWND hwnd);
void WINAPI RefreshImmersiveColorPolicyState(void);

#ifdef __cplusplus
}
#endif
