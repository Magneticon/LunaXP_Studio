#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <string>

typedef BOOL (WINAPI *PFN_SHOULD_DARK)(void);
typedef LONG (WINAPI *PFN_SET_PREF)(BOOL,BOOL);
typedef void (WINAPI *PFN_REFRESH)(void);

static HINSTANCE gInst=0;
static HMODULE gDarkDll=0;
static PFN_SHOULD_DARK gShouldAppsDark=0;
static PFN_SHOULD_DARK gShouldSystemDark=0;
static PFN_SET_PREF gSetPreference=0;
static PFN_REFRESH gRefresh=0;

enum
{
    IDC_APPS=1001,
    IDC_SYSTEM,
    IDC_QUERY,
    IDC_APPLY,
    IDC_STATUS
};

static bool LoadDarkModeDll()
{
    if(gDarkDll)
        return true;

    /*
       A DLL with a distinct name follows normal application DLL search rules
       on XP. Keeping LunaDarkMode.dll beside this EXE therefore demonstrates
       the deployment model directly.
    */
    gDarkDll=LoadLibraryW(L"LunaDarkMode.dll");
    if(!gDarkDll)
        return false;

    gShouldAppsDark=(PFN_SHOULD_DARK)GetProcAddress(gDarkDll,"LunaShouldAppsUseDarkMode");
    gShouldSystemDark=(PFN_SHOULD_DARK)GetProcAddress(gDarkDll,"LunaShouldSystemUseDarkMode");
    gSetPreference=(PFN_SET_PREF)GetProcAddress(gDarkDll,"LunaSetDarkModePreference");
    gRefresh=(PFN_REFRESH)GetProcAddress(gDarkDll,"LunaRefreshDarkModePolicy");

    return gShouldAppsDark && gShouldSystemDark && gSetPreference;
}

static void QueryPreference(HWND h)
{
    if(!LoadDarkModeDll())
    {
        SetWindowTextW(GetDlgItem(h,IDC_STATUS),
                       L"LunaDarkMode.dll was not found beside the demo executable.");
        return;
    }

    BOOL apps=gShouldAppsDark();
    BOOL sys=gShouldSystemDark();

    SendMessageW(GetDlgItem(h,IDC_APPS),BM_SETCHECK,apps?BST_CHECKED:BST_UNCHECKED,0);
    SendMessageW(GetDlgItem(h,IDC_SYSTEM),BM_SETCHECK,sys?BST_CHECKED:BST_UNCHECKED,0);

    SetWindowTextW(GetDlgItem(h,IDC_STATUS),
                   apps ? L"Query result: applications prefer DARK."
                        : L"Query result: applications prefer LIGHT.");
}

static LRESULT CALLBACK WndProc(HWND h,UINT msg,WPARAM wp,LPARAM lp)
{
    switch(msg)
    {
    case WM_CREATE:
        {
            HFONT f=(HFONT)GetStockObject(DEFAULT_GUI_FONT);
            HWND info=CreateWindowW(L"STATIC",
                L"This sample dynamically loads LunaDarkMode.dll from the application "
                L"directory and queries the XP dark-mode compatibility preference.",
                WS_CHILD|WS_VISIBLE|SS_LEFT,16,16,430,48,h,NULL,gInst,NULL);
            SendMessageW(info,WM_SETFONT,(WPARAM)f,TRUE);

            CreateWindowW(L"BUTTON",L"Applications prefer dark mode",
                WS_CHILD|WS_VISIBLE|BS_AUTOCHECKBOX,20,74,280,25,
                h,(HMENU)(INT_PTR)IDC_APPS,gInst,NULL);
            CreateWindowW(L"BUTTON",L"System preference is dark",
                WS_CHILD|WS_VISIBLE|BS_AUTOCHECKBOX,20,104,280,25,
                h,(HMENU)(INT_PTR)IDC_SYSTEM,gInst,NULL);

            CreateWindowW(L"BUTTON",L"Query DLL",WS_CHILD|WS_VISIBLE|WS_TABSTOP,
                20,145,110,28,h,(HMENU)(INT_PTR)IDC_QUERY,gInst,NULL);
            CreateWindowW(L"BUTTON",L"Apply",WS_CHILD|WS_VISIBLE|WS_TABSTOP,
                140,145,110,28,h,(HMENU)(INT_PTR)IDC_APPLY,gInst,NULL);

            CreateWindowExW(WS_EX_CLIENTEDGE,L"STATIC",L"",
                WS_CHILD|WS_VISIBLE|SS_LEFT,20,188,425,48,
                h,(HMENU)(INT_PTR)IDC_STATUS,gInst,NULL);

            HWND child=GetWindow(h,GW_CHILD);
            while(child)
            {
                SendMessageW(child,WM_SETFONT,(WPARAM)f,TRUE);
                child=GetWindow(child,GW_HWNDNEXT);
            }

            QueryPreference(h);
        }
        return 0;

    case WM_COMMAND:
        switch(LOWORD(wp))
        {
        case IDC_QUERY:
            QueryPreference(h);
            return 0;

        case IDC_APPLY:
            if(!LoadDarkModeDll())
            {
                MessageBoxW(h,L"LunaDarkMode.dll was not found beside the EXE.",
                            L"Luna Dark Demo",MB_ICONERROR);
                return 0;
            }
            {
                BOOL apps=SendMessageW(GetDlgItem(h,IDC_APPS),BM_GETCHECK,0,0)==BST_CHECKED;
                BOOL sys=SendMessageW(GetDlgItem(h,IDC_SYSTEM),BM_GETCHECK,0,0)==BST_CHECKED;
                LONG rc=gSetPreference(apps,sys);
                if(rc==ERROR_SUCCESS)
                {
                    if(gRefresh) gRefresh();
                    QueryPreference(h);
                }
                else
                    MessageBoxW(h,L"Could not write the preference.",
                                L"Luna Dark Demo",MB_ICONERROR);
            }
            return 0;
        }
        break;

    case WM_DESTROY:
        if(gDarkDll)
        {
            FreeLibrary(gDarkDll);
            gDarkDll=0;
        }
        PostQuitMessage(0);
        return 0;
    }

    return DefWindowProcW(h,msg,wp,lp);
}

int APIENTRY wWinMain(HINSTANCE hInst,HINSTANCE,LPWSTR,int show)
{
    gInst=hInst;

    WNDCLASSEXW wc={0};
    wc.cbSize=sizeof(wc);
    wc.hInstance=hInst;
    wc.lpfnWndProc=WndProc;
    wc.lpszClassName=L"LunaDarkDemoWindow";
    wc.hCursor=LoadCursor(NULL,IDC_ARROW);
    wc.hbrBackground=(HBRUSH)(COLOR_BTNFACE+1);

    if(!RegisterClassExW(&wc))
        return 1;

    HWND h=CreateWindowW(wc.lpszClassName,L"Luna Dark Mode SDK Demo",
                         WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU|WS_MINIMIZEBOX,
                         CW_USEDEFAULT,CW_USEDEFAULT,485,290,
                         NULL,NULL,hInst,NULL);
    if(!h)
        return 1;

    ShowWindow(h,show);
    UpdateWindow(h);

    MSG msg;
    while(GetMessageW(&msg,NULL,0,0)>0)
    {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    return (int)msg.wParam;
}
