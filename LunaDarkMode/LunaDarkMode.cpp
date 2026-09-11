#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "LunaDarkMode.h"

static volatile LONG gPreferredMode=LunaAppModeDefault;
static const wchar_t* kAllowedProperty=L"LunaDarkModeAllowed";

static const wchar_t* PersonalizeKey()
{
    return L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize";
}

static bool ReadLightValue(const wchar_t* name,DWORD& value)
{
    value=1;
    HKEY key=0;
    LONG rc=RegOpenKeyExW(HKEY_CURRENT_USER,PersonalizeKey(),0,KEY_QUERY_VALUE,&key);
    if(rc!=ERROR_SUCCESS)
        return false;

    DWORD type=0;
    DWORD size=sizeof(value);
    rc=RegQueryValueExW(key,name,NULL,&type,(LPBYTE)&value,&size);
    RegCloseKey(key);

    if(rc!=ERROR_SUCCESS || type!=REG_DWORD)
    {
        value=1;
        return false;
    }
    return true;
}

static LONG WriteLightValue(const wchar_t* name,DWORD value)
{
    HKEY key=0;
    DWORD disposition=0;
    LONG rc=RegCreateKeyExW(HKEY_CURRENT_USER,PersonalizeKey(),0,NULL,0,
                            KEY_SET_VALUE,NULL,&key,&disposition);
    if(rc!=ERROR_SUCCESS)
        return rc;

    rc=RegSetValueExW(key,name,0,REG_DWORD,(const BYTE*)&value,sizeof(value));
    RegCloseKey(key);
    return rc;
}

static BOOL RegistryAppsDark()
{
    DWORD light=1;
    ReadLightValue(L"AppsUseLightTheme",light);
    return light==0 ? TRUE : FALSE;
}

static BOOL RegistrySystemDark()
{
    DWORD light=1;
    ReadLightValue(L"SystemUsesLightTheme",light);
    return light==0 ? TRUE : FALSE;
}

BOOL WINAPI LunaShouldAppsUseDarkMode(void)
{
    LONG mode=InterlockedCompareExchange(&gPreferredMode,0,0);
    if(mode==LunaAppModeForceDark)
        return TRUE;
    if(mode==LunaAppModeForceLight)
        return FALSE;
    return RegistryAppsDark();
}

BOOL WINAPI LunaShouldSystemUseDarkMode(void)
{
    return RegistrySystemDark();
}

BOOL WINAPI LunaGetDarkModePreference(BOOL* appsDark,BOOL* systemDark)
{
    if(!appsDark || !systemDark)
        return FALSE;

    *appsDark=RegistryAppsDark();
    *systemDark=RegistrySystemDark();
    return TRUE;
}

LONG WINAPI LunaSetDarkModePreference(BOOL appsDark,BOOL systemDark)
{
    LONG rc=WriteLightValue(L"AppsUseLightTheme",appsDark?0:1);
    if(rc!=ERROR_SUCCESS)
        return rc;

    rc=WriteLightValue(L"SystemUsesLightTheme",systemDark?0:1);
    if(rc!=ERROR_SUCCESS)
        return rc;

    LunaRefreshDarkModePolicy();
    return ERROR_SUCCESS;
}

int WINAPI LunaSetPreferredAppMode(int mode)
{
    if(mode<LunaAppModeDefault || mode>LunaAppModeForceLight)
        mode=LunaAppModeDefault;

    return (int)InterlockedExchange(&gPreferredMode,(LONG)mode);
}

BOOL WINAPI LunaAllowDarkModeForWindow(HWND hwnd,BOOL allow)
{
    if(!IsWindow(hwnd))
        return FALSE;

    if(allow)
        return SetPropW(hwnd,kAllowedProperty,(HANDLE)(INT_PTR)1) ? TRUE : FALSE;

    RemovePropW(hwnd,kAllowedProperty);
    return TRUE;
}

BOOL WINAPI LunaIsDarkModeAllowedForWindow(HWND hwnd)
{
    if(!IsWindow(hwnd))
        return FALSE;
    return GetPropW(hwnd,kAllowedProperty) ? TRUE : FALSE;
}

void WINAPI LunaRefreshDarkModePolicy(void)
{
    DWORD_PTR result=0;
    SendMessageTimeoutW(HWND_BROADCAST,WM_SETTINGCHANGE,0,
                        (LPARAM)L"ImmersiveColorSet",
                        SMTO_ABORTIFHUNG|SMTO_NORMAL,1000,&result);
    SendMessageTimeoutW(HWND_BROADCAST,WM_SETTINGCHANGE,0,
                        (LPARAM)L"WindowsThemeElement",
                        SMTO_ABORTIFHUNG|SMTO_NORMAL,1000,&result);
}

/* compatibility-style aliases */
BOOL WINAPI ShouldAppsUseDarkMode(void)
{
    return LunaShouldAppsUseDarkMode();
}

BOOL WINAPI ShouldSystemUseDarkMode(void)
{
    return LunaShouldSystemUseDarkMode();
}

int WINAPI SetPreferredAppMode(int mode)
{
    return LunaSetPreferredAppMode(mode);
}

BOOL WINAPI AllowDarkModeForWindow(HWND hwnd,BOOL allow)
{
    return LunaAllowDarkModeForWindow(hwnd,allow);
}

BOOL WINAPI IsDarkModeAllowedForWindow(HWND hwnd)
{
    return LunaIsDarkModeAllowedForWindow(hwnd);
}

void WINAPI RefreshImmersiveColorPolicyState(void)
{
    LunaRefreshDarkModePolicy();
}

BOOL APIENTRY DllMain(HMODULE module,DWORD reason,LPVOID reserved)
{
    (void)module;
    (void)reason;
    (void)reserved;
    return TRUE;
}
