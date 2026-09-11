#define _CRT_SECURE_NO_WARNINGS
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "resource.h"
#include <commdlg.h>
#include <shlwapi.h>
#include <string>
#include <vector>
#include <map>
#include <set>
#include <sstream>
#include <cstdlib>
#include <cwctype>

#include "ThemeGenerator.h"
#include "BitmapEngine.h"

#pragma comment(lib,"comdlg32.lib")
#pragma comment(lib,"shlwapi.lib")

enum
{
    IDC_SOURCE=1001, IDC_SOURCE_BROWSE,
    IDC_DEF, IDC_LOAD_ROLES,
    IDC_FILTER, IDC_STATE_PREV, IDC_STATE_COUNTER, IDC_STATE_NEXT,
    IDC_ROLE_LIST, IDC_COLOR, IDC_GRADIENT, IDC_RESET_ROLE, IDC_COLOR_ALL, IDC_RESET_ALL,
    IDC_CLASSIC_COLORS, IDC_EXPORT_BITMAP, IDC_REPLACE_BITMAP,
    IDC_LOAD_PRESET, IDC_SAVE_PRESET,
    IDC_OUTPUT, IDC_OUTPUT_BROWSE,
    IDC_GENERATE, IDC_DARK_PRESET, IDC_DARK_PREFERENCE, IDC_APP_HELP, IDC_STATUS,
    IDC_ROLE_INFO
};

static HINSTANCE gInst=0;
static HWND gWnd=0;
static std::vector<ThemeRoleInfo> gRoles;
static ThemeSourceCache gSourceCache;
static std::map<std::wstring,ThemeRoleColor> gEdits;
static std::vector<ThemeClassicColor> gClassicColors;
static std::vector<ThemeClassicFont> gClassicFonts;
static std::vector<ThemeClassicMetric> gClassicMetrics;
static std::vector<BYTE> gOriginalPreviewDib;
static std::vector<BYTE> gEditedPreviewDib;
static BitmapInfoSummary gOriginalPreviewInfo;
static BitmapInfoSummary gEditedPreviewInfo;
static ThemeRoleStateInfo gPreviewStateInfo;
static bool gOriginalPreviewValid=false;
static bool gEditedPreviewValid=false;
static unsigned int gStateOrdinal=0;
static unsigned int gStateCount=0;
static std::wstring gRoleFilter=L"None";

static const RECT ORIGINAL_RECT={395,247,595,466};
static const RECT EDITED_RECT={605,247,805,466};
static const RECT THEME_PREVIEW_RECT={825,148,1249,466}; // 424 x 318 = exact 4:3

static std::wstring GetText(HWND h)
{
    int n=GetWindowTextLengthW(h);
    std::wstring s;
    if(n>0)
    {
        s.resize(n+1);
        GetWindowTextW(h,&s[0],n+1);
        s.resize(n);
    }
    return s;
}

static void SetStatus(const wchar_t* s)
{
    if(gWnd) SetWindowTextW(GetDlgItem(gWnd,IDC_STATUS),s);
}

static void RefreshLiveThemePreview()
{
    if(gWnd)
    {
        InvalidateRect(gWnd,&THEME_PREVIEW_RECT,TRUE);
        UpdateWindow(gWnd);
    }
}


static bool PickOpen(HWND owner,const wchar_t* filter,std::wstring& out)
{
    wchar_t buf[MAX_PATH]={0};
    OPENFILENAMEW ofn={0};
    ofn.lStructSize=sizeof(ofn); ofn.hwndOwner=owner; ofn.lpstrFilter=filter;
    ofn.lpstrFile=buf; ofn.nMaxFile=MAX_PATH;
    ofn.Flags=OFN_FILEMUSTEXIST|OFN_PATHMUSTEXIST;
    if(!GetOpenFileNameW(&ofn)) return false;
    out=buf; return true;
}

static bool PickSave(HWND owner,const wchar_t* filter,const wchar_t* defExt,std::wstring& out)
{
    wchar_t buf[MAX_PATH]={0};
    OPENFILENAMEW ofn={0};
    ofn.lStructSize=sizeof(ofn); ofn.hwndOwner=owner; ofn.lpstrFilter=filter;
    ofn.lpstrFile=buf; ofn.nMaxFile=MAX_PATH; ofn.lpstrDefExt=defExt;
    ofn.Flags=OFN_OVERWRITEPROMPT|OFN_PATHMUSTEXIST;
    if(!GetSaveFileNameW(&ofn)) return false;
    out=buf; return true;
}

static bool FileExists(const std::wstring& p)
{
    DWORD a=GetFileAttributesW(p.c_str());
    return a!=INVALID_FILE_ATTRIBUTES && !(a&FILE_ATTRIBUTE_DIRECTORY);
}


static std::wstring DefaultSystemLuna()
{
    wchar_t win[MAX_PATH]={0};
    UINT n=GetWindowsDirectoryW(win,MAX_PATH);
    if(!n || n>=MAX_PATH) return L"";

    std::wstring p=win;
    p+=L"\\Resources\\Themes\\Luna\\Luna.msstyles";
    return FileExists(p)?p:L"";
}

static HWND ShowProcessingSplash()
{
    HWND splash=CreateWindowExW(WS_EX_DLGMODALFRAME|WS_EX_TOPMOST|WS_EX_TOOLWINDOW,
                                L"STATIC",
                                L"LunaXP Studio",
                                WS_POPUP|WS_CAPTION|WS_VISIBLE,
                                CW_USEDEFAULT,CW_USEDEFAULT,440,145,
                                gWnd,NULL,gInst,NULL);
    if(splash)
    {
        HFONT f=(HFONT)GetStockObject(DEFAULT_GUI_FONT);

        HWND message=CreateWindowW(
            L"STATIC",
            L"Processing, please wait...\r\n\r\nLoading the semantic map and all bitmap resources into memory.",
            WS_CHILD|WS_VISIBLE|SS_CENTER,
            18,22,400,78,
            splash,NULL,gInst,NULL);
        if(message)
            SendMessageW(message,WM_SETFONT,(WPARAM)f,TRUE);

        RECT wr={0},sr={0};
        GetWindowRect(gWnd,&wr);
        GetWindowRect(splash,&sr);
        int w=sr.right-sr.left,h=sr.bottom-sr.top;
        int x=wr.left+((wr.right-wr.left)-w)/2;
        int y=wr.top+((wr.bottom-wr.top)-h)/2;
        SetWindowPos(splash,HWND_TOPMOST,x,y,w,h,SWP_SHOWWINDOW);
        UpdateWindow(splash);

        MSG msg;
        while(PeekMessageW(&msg,NULL,0,0,PM_REMOVE))
        {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }
    return splash;
}

static void CloseProcessingSplash(HWND splash)
{
    if(splash && IsWindow(splash))
        DestroyWindow(splash);
}

static HWND MakeLabel(HWND parent,const wchar_t* s,int x,int y,int w,int h)
{
    return CreateWindowW(L"STATIC",s,WS_CHILD|WS_VISIBLE,x,y,w,h,parent,NULL,gInst,NULL);
}

static HWND MakeEdit(HWND parent,int id,int x,int y,int w,int h,const wchar_t* text)
{
    return CreateWindowExW(WS_EX_CLIENTEDGE,L"EDIT",text,
                           WS_CHILD|WS_VISIBLE|WS_TABSTOP|ES_AUTOHSCROLL,
                           x,y,w,h,parent,(HMENU)(INT_PTR)id,gInst,NULL);
}

static HWND MakeButton(HWND parent,const wchar_t* s,int id,int x,int y,int w,int h)
{
    return CreateWindowW(L"BUTTON",s,WS_CHILD|WS_VISIBLE|WS_TABSTOP|BS_PUSHBUTTON,
                         x,y,w,h,parent,(HMENU)(INT_PTR)id,gInst,NULL);
}

static std::wstring SelectedDefinition()
{
    HWND hd=GetDlgItem(gWnd,IDC_DEF);
    int sel=(int)SendMessageW(hd,CB_GETCURSEL,0,0);
    if(sel==CB_ERR) sel=0;
    wchar_t def[64]={0};
    SendMessageW(hd,CB_GETLBTEXT,sel,(LPARAM)def);
    return def;
}

static ThemeRoleColor* FindEdit(const std::wstring& role)
{
    std::map<std::wstring,ThemeRoleColor>::iterator it=gEdits.find(role);
    return it==gEdits.end()?NULL:&it->second;
}

static int SelectedRoleIndex()
{
    HWND list=GetDlgItem(gWnd,IDC_ROLE_LIST);
    int sel=(int)SendMessageW(list,LB_GETCURSEL,0,0);
    if(sel==LB_ERR) return -1;
    LRESULT data=SendMessageW(list,LB_GETITEMDATA,sel,0);
    if(data==LB_ERR || data<0 || (size_t)data>=gRoles.size()) return -1;
    return (int)data;
}

static void RefreshRoleList(int preserveRoleIndex)
{
    HWND list=GetDlgItem(gWnd,IDC_ROLE_LIST);
    SendMessageW(list,WM_SETREDRAW,FALSE,0);
    SendMessageW(list,LB_RESETCONTENT,0,0);

    int selectRow=-1;
    for(size_t i=0;i<gRoles.size();++i)
    {
        if(_wcsicmp(gRoleFilter.c_str(),L"None")!=0)
        {
            std::wstring prefix=gRoleFilter+L".";
            if(gRoles[i].role.size()<prefix.size() ||
               _wcsnicmp(gRoles[i].role.c_str(),prefix.c_str(),prefix.size())!=0)
                continue;
        }

        std::wstringstream s;
        s << (FindEdit(gRoles[i].role)?L"* ":L"  ")
          << gRoles[i].role << L"  (" << gRoles[i].targetCount << L")";
        int row=(int)SendMessageW(list,LB_ADDSTRING,0,(LPARAM)s.str().c_str());
        SendMessageW(list,LB_SETITEMDATA,row,(LPARAM)i);
        if((int)i==preserveRoleIndex) selectRow=row;
    }

    if(selectRow<0 && !gRoles.empty()) selectRow=0;
    if(selectRow>=0) SendMessageW(list,LB_SETCURSEL,selectRow,0);
    SendMessageW(list,WM_SETREDRAW,TRUE,0);
    InvalidateRect(list,NULL,TRUE);
}

static void ClearPreview()
{
    gOriginalPreviewDib.clear();
    gEditedPreviewDib.clear();
    gOriginalPreviewValid=false;
    gEditedPreviewValid=false;
    gStateOrdinal=0; gStateCount=0;
    SetWindowTextW(GetDlgItem(gWnd,IDC_ROLE_INFO),L"No role selected.");
    InvalidateRect(gWnd,&ORIGINAL_RECT,TRUE);
    InvalidateRect(gWnd,&EDITED_RECT,TRUE);
    InvalidateRect(gWnd,&THEME_PREVIEW_RECT,TRUE);
}

static void UpdateStateButtons()
{
    HWND prev=GetDlgItem(gWnd,IDC_STATE_PREV);
    HWND next=GetDlgItem(gWnd,IDC_STATE_NEXT);
    EnableWindow(prev,gStateCount>1);
    EnableWindow(next,gStateCount>1);

    wchar_t counter[32]={0};
    if(gStateCount)
        wsprintfW(counter,L"%u of %u",gStateOrdinal+1,gStateCount);
    else
        lstrcpyW(counter,L"- of -");
    SetWindowTextW(GetDlgItem(gWnd,IDC_STATE_COUNTER),counter);
}

static void UpdateSelectedRolePreview()
{
    int idx=SelectedRoleIndex();
    if(idx<0 || (size_t)idx>=gRoles.size())
    {
        ClearPreview(); UpdateStateButtons(); return;
    }

    std::wstring source=GetText(GetDlgItem(gWnd,IDC_SOURCE));
    if(!FileExists(source))
    {
        ClearPreview(); UpdateStateButtons(); return;
    }

    std::vector<ThemeRoleStateInfo> states;
    std::wstring err;
    if(!ThemeGenerator::EnumerateRoleStates(gSourceCache,gRoles[idx].role,states,err))
    {
        ClearPreview(); UpdateStateButtons(); return;
    }
    gStateCount=(unsigned int)states.size();
    if(gStateOrdinal>=gStateCount) gStateOrdinal=0;

    ThemeRoleColor* edit=FindEdit(gRoles[idx].role);
    ThemeRoleStateInfo oi,ei;
    std::vector<BYTE> originalDib,editedDib;

    if(!ThemeGenerator::BuildRoleStatePreviewCached(gSourceCache,gRoles[idx].role,gStateOrdinal,
                                                    NULL,true,originalDib,oi,err))
    {
        gOriginalPreviewValid=false;
        SetWindowTextW(GetDlgItem(gWnd,IDC_ROLE_INFO),err.c_str());
        return;
    }
    if(!ThemeGenerator::BuildRoleStatePreviewCached(gSourceCache,gRoles[idx].role,gStateOrdinal,
                                                    edit,false,editedDib,ei,err))
    {
        gEditedPreviewValid=false;
        SetWindowTextW(GetDlgItem(gWnd,IDC_ROLE_INFO),err.c_str());
        return;
    }

    BitmapInfoSummary obi,ebi;
    if(!BitmapEngine::ParseDib(originalDib,obi,err) || !BitmapEngine::ParseDib(editedDib,ebi,err))
    {
        SetWindowTextW(GetDlgItem(gWnd,IDC_ROLE_INFO),err.c_str());
        return;
    }

    gOriginalPreviewDib.swap(originalDib);
    gEditedPreviewDib.swap(editedDib);
    gOriginalPreviewInfo=obi; gEditedPreviewInfo=ebi;
    gPreviewStateInfo=ei;
    gOriginalPreviewValid=gEditedPreviewValid=true;

    std::wstringstream s;
    s << L"Role: " << gRoles[idx].role
      << L"    Target " << (gStateOrdinal+1) << L" / " << gStateCount << L"\r\n"
      << L"Resource: " << ei.resource << L"\r\n"
      << L"State: " << ei.stateIndex;
    if(!ei.stateName.empty()) s << L" (" << ei.stateName << L")";
    if(edit)
    {
        wchar_t rgb[16]={0};
        wsprintfW(rgb,L"#%02X%02X%02X",(unsigned int)edit->r,
                  (unsigned int)edit->g,(unsigned int)edit->b);
        s << L"    Target color: " << rgb;
        if(edit->gradient)
        {
            wchar_t rgb2[16]={0};
            wsprintfW(rgb2,L"#%02X%02X%02X",(unsigned int)edit->r2,(unsigned int)edit->g2,(unsigned int)edit->b2);
            s << L" -> " << rgb2 << L" (gradient)";
        }
    }
    if(ThemeGenerator::IsBitmapOverridden(ei.resource))
        s << L"\r\nBitmap source: CUSTOM replacement";

    SetWindowTextW(GetDlgItem(gWnd,IDC_ROLE_INFO),s.str().c_str());
    UpdateStateButtons();
    InvalidateRect(gWnd,&ORIGINAL_RECT,TRUE);
    InvalidateRect(gWnd,&EDITED_RECT,TRUE);
}

static void PreviousState()
{
    if(gStateCount<2) return;
    if(gStateOrdinal==0) gStateOrdinal=gStateCount-1; else --gStateOrdinal;
    UpdateSelectedRolePreview();
}

static void NextState()
{
    if(gStateCount<2) return;
    gStateOrdinal=(gStateOrdinal+1)%gStateCount;
    UpdateSelectedRolePreview();
}


static void LoadRoles()
{
    std::wstring source=GetText(GetDlgItem(gWnd,IDC_SOURCE));
    if(!FileExists(source))
    {
        MessageBoxW(gWnd,L"Choose an existing Luna .msstyles source first.",
                    L"LunaXP Studio",MB_ICONWARNING);
        return;
    }

    SetStatus(L"Processing source and preloading all bitmaps...");
    EnableWindow(gWnd,FALSE);
    HWND splash=ShowProcessingSplash();

    ThemeSourceCache cache;
    std::wstring err;
    bool loaded=ThemeGenerator::BuildSourceCache(source,SelectedDefinition(),cache,err);

    CloseProcessingSplash(splash);
    EnableWindow(gWnd,TRUE);
    SetForegroundWindow(gWnd);

    if(!loaded)
    {
        SetStatus(L"Could not load semantic roles.");
        MessageBoxW(gWnd,err.c_str(),L"LunaXP Studio",MB_ICONERROR);
        return;
    }

    gSourceCache=cache;
    gRoles=gSourceCache.roles;
    gEdits.clear();
    gClassicColors.clear();
    gClassicFonts.clear();
    gClassicMetrics.clear();
    {
        std::wstring ce;
        ThemeGenerator::GetClassicColors(gSourceCache,gClassicColors,ce);
        ThemeGenerator::GetClassicFonts(gSourceCache,gClassicFonts,ce);
        ThemeGenerator::GetClassicMetrics(gSourceCache,gClassicMetrics,ce);
    }
    gStateOrdinal=0;

    HWND filter=GetDlgItem(gWnd,IDC_FILTER);
    SendMessageW(filter,CB_RESETCONTENT,0,0);
    SendMessageW(filter,CB_ADDSTRING,0,(LPARAM)L"None");
    std::set<std::wstring> groups;
    for(size_t gi=0;gi<gRoles.size();++gi)
    {
        size_t dot=gRoles[gi].role.find(L'.');
        std::wstring group=dot==std::wstring::npos?gRoles[gi].role:gRoles[gi].role.substr(0,dot);
        if(!group.empty()) groups.insert(group);
    }
    for(std::set<std::wstring>::const_iterator it=groups.begin();it!=groups.end();++it)
        SendMessageW(filter,CB_ADDSTRING,0,(LPARAM)it->c_str());
    SendMessageW(filter,CB_SETCURSEL,0,0);
    gRoleFilter=L"None";

    RefreshRoleList(0);
    UpdateSelectedRolePreview();

    std::wstringstream s;
    s << L"Loaded " << gRoles.size() << L" semantic roles and "
      << gSourceCache.bitmapResourceCount << L" bitmaps ("
      << (unsigned long)(gSourceCache.bitmapBytesLoaded/1024ULL)
      << L" KB) into memory.";
    SetStatus(s.str().c_str());
}

static void ChangeRoleColor()
{
    int idx=SelectedRoleIndex();
    if(idx<0)
    {
        MessageBoxW(gWnd,L"Select a semantic role first.",L"LunaXP Studio",MB_ICONWARNING);
        return;
    }

    static COLORREF custom[16]={0};
    CHOOSECOLORW cc={0};
    cc.lStructSize=sizeof(cc);
    cc.hwndOwner=gWnd;
    cc.lpCustColors=custom;
    cc.Flags=CC_FULLOPEN|CC_RGBINIT;

    ThemeRoleColor* existing=FindEdit(gRoles[idx].role);
    cc.rgbResult=existing?RGB(existing->r,existing->g,existing->b):GetSysColor(COLOR_ACTIVECAPTION);

    if(!ChooseColorW(&cc)) return;

    ThemeRoleColor e;
    e.role=gRoles[idx].role;
    e.r=GetRValue(cc.rgbResult);
    e.g=GetGValue(cc.rgbResult);
    e.b=GetBValue(cc.rgbResult);
    e.bulkSafeOnly=false;
    gEdits[e.role]=e;

    RefreshRoleList(idx);
    UpdateSelectedRolePreview();
    SetStatus(L"Role color edited. The preview shows the recolored first matching state.");
}



enum
{
    IDC_GRAD_C1=4101,
    IDC_GRAD_C2,
    IDC_GRAD_SWAP,
    IDC_GRAD_OK,
    IDC_GRAD_CANCEL
};

static COLORREF gGradientColor1=RGB(0,128,128);
static COLORREF gGradientColor2=RGB(0,64,96);
static bool gGradientAccepted=false;
static HWND gGradientWnd=0;

static void PaintGradientEditor(HWND h,HDC dc)
{
    RECT rc;
    GetClientRect(h,&rc);

    HFONT font=(HFONT)GetStockObject(DEFAULT_GUI_FONT);
    HFONT old=(HFONT)SelectObject(dc,font);
    SetBkMode(dc,TRANSPARENT);

    RECT l1={18,18,180,40};
    DrawTextW(dc,L"Color 1  (left side)",-1,&l1,DT_LEFT|DT_VCENTER|DT_SINGLELINE);
    RECT l2={245,18,430,40};
    DrawTextW(dc,L"Color 2  (right side)",-1,&l2,DT_LEFT|DT_VCENTER|DT_SINGLELINE);

    RECT s1={18,44,210,82};
    RECT s2={245,44,437,82};
    HBRUSH b1=CreateSolidBrush(gGradientColor1);
    HBRUSH b2=CreateSolidBrush(gGradientColor2);
    FillRect(dc,&s1,b1);
    FillRect(dc,&s2,b2);
    DeleteObject(b1);
    DeleteObject(b2);
    FrameRect(dc,&s1,(HBRUSH)GetStockObject(BLACK_BRUSH));
    FrameRect(dc,&s2,(HBRUSH)GetStockObject(BLACK_BRUSH));

    RECT pt={18,102,437,122};
    DrawTextW(dc,L"Gradient preview",-1,&pt,DT_LEFT|DT_VCENTER|DT_SINGLELINE);

    RECT gr={18,126,437,174};
    for(int x=gr.left;x<gr.right;++x)
    {
        double t=(gr.right-gr.left>1)?(double)(x-gr.left)/(double)(gr.right-gr.left-1):0.0;
        BYTE r=(BYTE)(GetRValue(gGradientColor1)+(GetRValue(gGradientColor2)-GetRValue(gGradientColor1))*t);
        BYTE g=(BYTE)(GetGValue(gGradientColor1)+(GetGValue(gGradientColor2)-GetGValue(gGradientColor1))*t);
        BYTE b=(BYTE)(GetBValue(gGradientColor1)+(GetBValue(gGradientColor2)-GetBValue(gGradientColor1))*t);
        HPEN pen=CreatePen(PS_SOLID,1,RGB(r,g,b));
        HPEN oldPen=(HPEN)SelectObject(dc,pen);
        MoveToEx(dc,x,gr.top,NULL);
        LineTo(dc,x,gr.bottom);
        SelectObject(dc,oldPen);
        DeleteObject(pen);
    }
    FrameRect(dc,&gr,(HBRUSH)GetStockObject(BLACK_BRUSH));

    RECT help={18,184,437,224};
    DrawTextW(dc,
              L"Color 1 is used at the left edge and Color 2 at the right edge.\r\n"
              L"For Stretch parts, the full ramp is placed in the stretchable center.",
              -1,&help,DT_LEFT|DT_WORDBREAK);

    SelectObject(dc,old);
}

static void ChooseGradientEndpoint(HWND owner,bool second)
{
    static COLORREF custom[16]={0};
    CHOOSECOLORW cc={0};
    cc.lStructSize=sizeof(cc);
    cc.hwndOwner=owner;
    cc.lpCustColors=custom;
    cc.Flags=CC_FULLOPEN|CC_RGBINIT;
    cc.rgbResult=second?gGradientColor2:gGradientColor1;

    if(ChooseColorW(&cc))
    {
        if(second) gGradientColor2=cc.rgbResult;
        else gGradientColor1=cc.rgbResult;
        InvalidateRect(owner,NULL,TRUE);
    }
}

static LRESULT CALLBACK GradientWndProc(HWND h,UINT msg,WPARAM wp,LPARAM lp)
{
    switch(msg)
    {
    case WM_CREATE:
        gGradientWnd=h;
        MakeButton(h,L"Choose Color 1...",IDC_GRAD_C1,18,236,120,28);
        MakeButton(h,L"Choose Color 2...",IDC_GRAD_C2,148,236,120,28);
        MakeButton(h,L"Swap",IDC_GRAD_SWAP,278,236,70,28);
        MakeButton(h,L"OK",IDC_GRAD_OK,276,280,78,28);
        MakeButton(h,L"Cancel",IDC_GRAD_CANCEL,360,280,78,28);
        {
            HFONT f=(HFONT)GetStockObject(DEFAULT_GUI_FONT);
            HWND ch=GetWindow(h,GW_CHILD);
            while(ch)
            {
                SendMessageW(ch,WM_SETFONT,(WPARAM)f,TRUE);
                ch=GetWindow(ch,GW_HWNDNEXT);
            }
        }
        return 0;

    case WM_PAINT:
        {
            PAINTSTRUCT ps;
            HDC dc=BeginPaint(h,&ps);
            PaintGradientEditor(h,dc);
            EndPaint(h,&ps);
        }
        return 0;

    case WM_COMMAND:
        switch(LOWORD(wp))
        {
        case IDC_GRAD_C1:
            ChooseGradientEndpoint(h,false);
            return 0;
        case IDC_GRAD_C2:
            ChooseGradientEndpoint(h,true);
            return 0;
        case IDC_GRAD_SWAP:
            {
                COLORREF t=gGradientColor1;
                gGradientColor1=gGradientColor2;
                gGradientColor2=t;
                InvalidateRect(h,NULL,TRUE);
            }
            return 0;
        case IDC_GRAD_OK:
            gGradientAccepted=true;
            DestroyWindow(h);
            return 0;
        case IDC_GRAD_CANCEL:
            gGradientAccepted=false;
            DestroyWindow(h);
            return 0;
        }
        break;

    case WM_CLOSE:
        gGradientAccepted=false;
        DestroyWindow(h);
        return 0;

    case WM_DESTROY:
        gGradientWnd=0;
        return 0;
    }

    return DefWindowProcW(h,msg,wp,lp);
}

static bool PickGradientColors(HWND owner,COLORREF& first,COLORREF& second)
{
    static bool registered=false;
    if(!registered)
    {
        WNDCLASSEXW wc={0};
        wc.cbSize=sizeof(wc);
        wc.hInstance=gInst;
        wc.lpfnWndProc=GradientWndProc;
        wc.lpszClassName=L"LunaGradientEditorWindow";
        wc.hCursor=LoadCursor(NULL,IDC_ARROW);
        wc.hbrBackground=(HBRUSH)(COLOR_BTNFACE+1);
        if(!RegisterClassExW(&wc) && GetLastError()!=ERROR_CLASS_ALREADY_EXISTS)
            return false;
        registered=true;
    }

    gGradientColor1=first;
    gGradientColor2=second;
    gGradientAccepted=false;

    RECT pr={0};
    GetWindowRect(owner,&pr);
    int w=470,h=350;
    int x=pr.left+((pr.right-pr.left)-w)/2;
    int y=pr.top+((pr.bottom-pr.top)-h)/2;

    HWND dlg=CreateWindowExW(WS_EX_DLGMODALFRAME|WS_EX_TOOLWINDOW,
                             L"LunaGradientEditorWindow",
                             L"Gradient colors",
                             WS_POPUP|WS_CAPTION|WS_SYSMENU,
                             x,y,w,h,owner,NULL,gInst,NULL);
    if(!dlg) return false;

    EnableWindow(owner,FALSE);
    ShowWindow(dlg,SW_SHOW);
    UpdateWindow(dlg);

    MSG msg;
    while(IsWindow(dlg) && GetMessageW(&msg,NULL,0,0)>0)
    {
        if(!IsDialogMessageW(dlg,&msg))
        {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }

    EnableWindow(owner,TRUE);
    SetForegroundWindow(owner);

    if(!gGradientAccepted) return false;

    first=gGradientColor1;
    second=gGradientColor2;
    return true;
}

static void ChangeRoleGradient()
{
    int idx=SelectedRoleIndex();
    if(idx<0)
    {
        MessageBoxW(gWnd,L"Select a semantic role first.",L"LunaXP Studio",MB_ICONWARNING);
        return;
    }

    ThemeRoleColor* ex=FindEdit(gRoles[idx].role);
    COLORREF first=ex?RGB(ex->r,ex->g,ex->b):GetSysColor(COLOR_ACTIVECAPTION);
    COLORREF second=(ex&&ex->gradient)?
        RGB(ex->r2,ex->g2,ex->b2):
        GetSysColor(COLOR_GRADIENTACTIVECAPTION);

    if(!PickGradientColors(gWnd,first,second))
        return;

    ThemeRoleColor e;
    e.role=gRoles[idx].role;
    e.r=GetRValue(first);
    e.g=GetGValue(first);
    e.b=GetBValue(first);
    e.r2=GetRValue(second);
    e.g2=GetGValue(second);
    e.b2=GetBValue(second);
    e.gradient=true;
    e.bulkSafeOnly=false;
    gEdits[e.role]=e;

    // Keep classic caption gradient colors coherent with themed caption edits.
    // This helps XP dialogs/fallback drawing that consult SysMetrics.
    if(_wcsicmp(e.role.c_str(),L"Window.ActiveCaption")==0)
    {
        for(size_t i=0;i<gClassicColors.size();++i)
        {
            if(_wcsicmp(gClassicColors[i].key.c_str(),L"ActiveCaption")==0)
            {
                gClassicColors[i].r=e.r; gClassicColors[i].g=e.g; gClassicColors[i].b=e.b;
                gClassicColors[i].edited=true;
            }
            else if(_wcsicmp(gClassicColors[i].key.c_str(),L"GradientActiveCaption")==0)
            {
                gClassicColors[i].r=e.r2; gClassicColors[i].g=e.g2; gClassicColors[i].b=e.b2;
                gClassicColors[i].edited=true;
            }
        }
    }
    else if(_wcsicmp(e.role.c_str(),L"Window.InactiveCaption")==0)
    {
        for(size_t i=0;i<gClassicColors.size();++i)
        {
            if(_wcsicmp(gClassicColors[i].key.c_str(),L"InactiveCaption")==0)
            {
                gClassicColors[i].r=e.r; gClassicColors[i].g=e.g; gClassicColors[i].b=e.b;
                gClassicColors[i].edited=true;
            }
            else if(_wcsicmp(gClassicColors[i].key.c_str(),L"GradientInactiveCaption")==0)
            {
                gClassicColors[i].r=e.r2; gClassicColors[i].g=e.g2; gClassicColors[i].b=e.b2;
                gClassicColors[i].edited=true;
            }
        }
    }

    RefreshRoleList(idx);
    UpdateSelectedRolePreview();
    SetStatus(L"Gradient edited: Color 1 = left edge, Color 2 = right edge.");
    RefreshLiveThemePreview();
}

static void ResetRole()
{
    int idx=SelectedRoleIndex();
    if(idx<0) return;
    gEdits.erase(gRoles[idx].role);
    RefreshRoleList(idx);
    UpdateSelectedRolePreview();
    SetStatus(L"Selected role reset to source appearance.");
    RefreshLiveThemePreview();
}


static void ChangeAllColors()
{
    if(gRoles.empty())
    {
        MessageBoxW(gWnd,L"Load semantic roles first.",L"LunaXP Studio",MB_ICONWARNING);
        return;
    }

    static COLORREF custom[16]={0};
    CHOOSECOLORW cc={0};
    cc.lStructSize=sizeof(cc);
    cc.hwndOwner=gWnd;
    cc.lpCustColors=custom;
    cc.Flags=CC_FULLOPEN|CC_RGBINIT;
    cc.rgbResult=GetSysColor(COLOR_ACTIVECAPTION);

    if(!ChooseColorW(&cc)) return;

    BYTE r=GetRValue(cc.rgbResult);
    BYTE g=GetGValue(cc.rgbResult);
    BYTE b=GetBValue(cc.rgbResult);

    for(size_t i=0;i<gRoles.size();++i)
    {
        ThemeRoleColor e;
        e.role=gRoles[i].role;
        e.r=r; e.g=g; e.b=b;
        e.bulkSafeOnly=true;
        gEdits[e.role]=e;
    }

    int keep=SelectedRoleIndex();
    RefreshRoleList(keep);
    UpdateSelectedRolePreview();

    std::wstringstream s;
    s << L"Applied one color to major theme surfaces. "
      << L"Buttons, scrollbar/trackbar controls, tabs, toolbar/rebar sprites, glyphs and masks remain stock; "
      << L"they can still be edited individually.";
    SetStatus(s.str().c_str());
    RefreshLiveThemePreview();
}

static void ResetAllColors()
{
    if(gEdits.empty()) return;
    gEdits.clear();
    int keep=SelectedRoleIndex();
    RefreshRoleList(keep);
    UpdateSelectedRolePreview();
    SetStatus(L"All edited role colors were reset to source appearance.");
    RefreshLiveThemePreview();
}


static bool GetSelectedResource(std::wstring& resourceName)
{
    int idx=SelectedRoleIndex();
    if(idx<0 || (size_t)idx>=gRoles.size())
    {
        MessageBoxW(gWnd,L"Select a semantic role first.",L"LunaXP Studio",MB_ICONWARNING);
        return false;
    }
    resourceName=gRoles[idx].firstResource;
    if(resourceName.empty())
    {
        MessageBoxW(gWnd,L"The selected role has no BITMAP resource.",L"LunaXP Studio",MB_ICONWARNING);
        return false;
    }
    return true;
}

static void ExportSelectedBitmap()
{
    if(!gSourceCache.ready)
    {
        MessageBoxW(gWnd,L"Load semantic roles first.",L"LunaXP Studio",MB_ICONWARNING);
        return;
    }

    std::wstring resourceName;
    if(!GetSelectedResource(resourceName)) return;

    std::wstring path;
    if(!PickSave(gWnd,L"Windows Bitmap (*.bmp)\0*.bmp\0All files\0*.*\0",L"bmp",path))
        return;

    int idx=SelectedRoleIndex();
    if(idx<0 || (size_t)idx>=gRoles.size()) return;

    std::wstring err;
    if(!ThemeGenerator::ExportBitmapStateForRole(gSourceCache,gRoles[idx].role,path,err))
    {
        MessageBoxW(gWnd,err.c_str(),L"LunaXP Studio",MB_ICONERROR);
        return;
    }

    std::wstringstream s;
    s << L"Exported selected state for " << gRoles[idx].role << L" as 24-bit RGB BMP.";
    SetStatus(s.str().c_str());
}

static void ReplaceSelectedBitmap()
{
    if(!gSourceCache.ready)
    {
        MessageBoxW(gWnd,L"Load semantic roles first.",L"LunaXP Studio",MB_ICONWARNING);
        return;
    }

    std::wstring resourceName;
    if(!GetSelectedResource(resourceName)) return;

    std::wstring path;
    if(!PickOpen(gWnd,L"Windows Bitmap (*.bmp)\0*.bmp\0All files\0*.*\0",path))
        return;

    int idx=SelectedRoleIndex();
    if(idx<0 || (size_t)idx>=gRoles.size()) return;

    unsigned int replaced=0, skipped=0;
    std::wstring err;
    if(!ThemeGenerator::ReplaceBitmapForRole(gSourceCache,gRoles[idx].role,path,
                                             replaced,skipped,err))
    {
        MessageBoxW(gWnd,err.c_str(),L"LunaXP Studio",MB_ICONERROR);
        return;
    }

    UpdateSelectedRolePreview();

    std::wstringstream s;
    s << L"Custom bitmap applied to " << replaced << L" resource(s) for "
      << gRoles[idx].role;
    if(skipped)
        s << L"; " << skipped << L" incompatible resource(s) skipped";
    s << L". Generate the theme to write the changes.";
    SetStatus(s.str().c_str());
    RefreshLiveThemePreview();
}


static void LoadPreset()
{
    std::wstring path;
    if(!PickOpen(gWnd,L"Palette presets (*.ini)\0*.ini\0All files\0*.*\0",path))
        return;

    std::vector<ThemeRoleColor> colors;
    std::wstring err;
    if(!ThemeGenerator::LoadPreset(path,colors,err))
    {
        MessageBoxW(gWnd,err.c_str(),L"LunaXP Studio",MB_ICONERROR);
        return;
    }

    gEdits.clear();
    unsigned int matched=0;
    for(size_t i=0;i<colors.size();++i)
    {
        for(size_t r=0;r<gRoles.size();++r)
        {
            if(_wcsicmp(colors[i].role.c_str(),gRoles[r].role.c_str())==0)
            {
                colors[i].role=gRoles[r].role;
                gEdits[colors[i].role]=colors[i];
                ++matched;
                break;
            }
        }
    }

    RefreshRoleList(SelectedRoleIndex());
    UpdateSelectedRolePreview();

    std::wstringstream s;
    s << L"Loaded preset: " << matched << L" matching semantic role colors.";
    SetStatus(s.str().c_str());
}

static std::vector<ThemeRoleColor> CurrentEdits()
{
    std::vector<ThemeRoleColor> v;
    for(std::map<std::wstring,ThemeRoleColor>::const_iterator it=gEdits.begin();it!=gEdits.end();++it)
        v.push_back(it->second);
    return v;
}




static void SetDarkRoleIfPresent(const wchar_t* role,BYTE r,BYTE g,BYTE b,
                                 unsigned int& matched)
{
    for(size_t i=0;i<gRoles.size();++i)
    {
        if(_wcsicmp(gRoles[i].role.c_str(),role)==0)
        {
            ThemeRoleColor e;
            e.role=gRoles[i].role;
            e.r=r;e.g=g;e.b=b;
            e.bulkSafeOnly=false;
            e.surfaceFill=false;
            gEdits[e.role]=e;
            ++matched;
            return;
        }
    }
}

static void SetDarkSurfaceRoleIfPresent(const wchar_t* role,BYTE r,BYTE g,BYTE b,
                                        unsigned int& matched)
{
    for(size_t i=0;i<gRoles.size();++i)
    {
        if(_wcsicmp(gRoles[i].role.c_str(),role)==0)
        {
            ThemeRoleColor e;
            e.role=gRoles[i].role;
            e.r=r;e.g=g;e.b=b;
            e.bulkSafeOnly=false;
            e.surfaceFill=true;
            gEdits[e.role]=e;
            ++matched;
            return;
        }
    }
}

static void SetClassicDarkColor(const wchar_t* key,BYTE r,BYTE g,BYTE b,
                                unsigned int& matched)
{
    for(size_t i=0;i<gClassicColors.size();++i)
    {
        if(_wcsicmp(gClassicColors[i].key.c_str(),key)==0)
        {
            gClassicColors[i].r=r;
            gClassicColors[i].g=g;
            gClassicColors[i].b=b;
            gClassicColors[i].edited=true;
            ++matched;
            return;
        }
    }
}

static void ApplyDarkControlsPreset()
{
    if(!gSourceCache.ready || gClassicColors.empty())
    {
        MessageBoxW(gWnd,
                    L"Load semantic roles first, then apply the Dark Controls preset.",
                    L"LunaXP Studio",MB_ICONWARNING);
        return;
    }

    unsigned int classicMatched=0;
    unsigned int semanticMatched=0;

    /*
       Keep the general control path schema-safe: only stock Luna SysMetrics
       values are changed.

       For the Start menu we use a deliberately tiny semantic whitelist of
       BACKGROUND resources only. Buttons, glyphs, arrows, separators, user
       picture, Start button and other sprite/mask resources are not touched.
    */

    SetClassicDarkColor(L"Window",         58,58,58,classicMatched);
    SetClassicDarkColor(L"WindowText",    245,245,245,classicMatched);
    SetClassicDarkColor(L"MenuBar",        62,62,62,classicMatched);
    SetClassicDarkColor(L"Menu",           68,68,68,classicMatched);
    SetClassicDarkColor(L"MenuText",      245,245,245,classicMatched);
    SetClassicDarkColor(L"Btnface",        72,72,72,classicMatched);
    SetClassicDarkColor(L"BtnText",       245,245,245,classicMatched);
    SetClassicDarkColor(L"Scrollbar",      78,78,78,classicMatched);

    SetClassicDarkColor(L"Highlight",      52,96,150,classicMatched);
    SetClassicDarkColor(L"HighlightText", 255,255,255,classicMatched);
    SetClassicDarkColor(L"MenuHilight",    70,88,112,classicMatched);

    SetClassicDarkColor(L"BtnShadow",      48,48,48,classicMatched);
    SetClassicDarkColor(L"GrayText",      232,232,232,classicMatched);
    SetClassicDarkColor(L"BtnHighlight",  150,150,150,classicMatched);
    SetClassicDarkColor(L"DkShadow3d",     30,30,30,classicMatched);
    SetClassicDarkColor(L"Light3d",       128,128,128,classicMatched);

    // Known-safe themed surfaces. surfaceFill also recolors neutral/white
    // pixels, which is required for Luna's originally white Start-menu/program
    // panel and several classic control faces.
    SetDarkSurfaceRoleIfPresent(L"StartMenu.ProgramList",             46,46,46,semanticMatched);
    SetDarkSurfaceRoleIfPresent(L"StartMenu.PlacesList",              42,48,56,semanticMatched);
    SetDarkSurfaceRoleIfPresent(L"StartMenu.UserPane",                38,52,70,semanticMatched);
    SetDarkSurfaceRoleIfPresent(L"StartMenu.Logoff.Background",       38,42,48,semanticMatched);
    SetDarkSurfaceRoleIfPresent(L"StartMenu.MorePrograms.Background", 48,48,48,semanticMatched);
    SetDarkSurfaceRoleIfPresent(L"StartMenu.Toolbar.Background",      46,46,46,semanticMatched);

    SetDarkSurfaceRoleIfPresent(L"Controls.Button",                   62,62,62,semanticMatched);
    SetDarkSurfaceRoleIfPresent(L"Controls.PushButton",               62,62,62,semanticMatched);
    SetDarkSurfaceRoleIfPresent(L"Controls.GroupBox",                 54,54,54,semanticMatched);
    SetDarkSurfaceRoleIfPresent(L"ComboBox",                          58,58,58,semanticMatched);
    SetDarkSurfaceRoleIfPresent(L"Controls.Tabs",                     54,54,54,semanticMatched);
    SetDarkSurfaceRoleIfPresent(L"Controls.GroupBox",                 54,54,54,semanticMatched);
    SetDarkSurfaceRoleIfPresent(L"List/Header",                       58,58,58,semanticMatched);
    SetDarkSurfaceRoleIfPresent(L"Status Bar",                       152,152,152,semanticMatched);
    SetDarkSurfaceRoleIfPresent(L"Toolbar/Rebar",                     52,52,52,semanticMatched);
    SetDarkSurfaceRoleIfPresent(L"Controls.Progress",                 56,56,56,semanticMatched);
    SetDarkSurfaceRoleIfPresent(L"Controls.Scrollbar",                54,54,54,semanticMatched);
    SetDarkSurfaceRoleIfPresent(L"Scrollbar",                         54,54,54,semanticMatched);

    // Arrow resource contains a white arrow glyph. Use ordinary recolor here:
    // blue button tint becomes gray while neutral/white glyph pixels stay intact.
    SetDarkRoleIfPresent(L"Controls.Scrollbar.Arrow",                  92,92,92,semanticMatched);

    // Explorer task-pane/background roles. surfaceFill changes only resources
    // on ThemeGenerator's safe-background whitelist and leaves collapse/expand
    // glyph companions untouched.
    SetDarkSurfaceRoleIfPresent(L"ExplorerBar",                        46,52,58,semanticMatched);
    SetDarkSurfaceRoleIfPresent(L"ExplorerBar.Header",                 44,50,56,semanticMatched);
    SetDarkSurfaceRoleIfPresent(L"ExplorerBar.NormalGroup",            50,50,50,semanticMatched);
    SetDarkSurfaceRoleIfPresent(L"ExplorerBar.SpecialGroup",           48,48,52,semanticMatched);
    SetDarkSurfaceRoleIfPresent(L"ExplorerBar.Toolbar",                48,48,48,semanticMatched);

    // Intentionally untouched:
    // StartMenu.StartButton, user picture, arrows, buttons, separators,
    // caption/titlebar resources, caption buttons and taskbar.

    int keep=SelectedRoleIndex();
    RefreshRoleList(keep);
    UpdateSelectedRolePreview();
    RefreshLiveThemePreview();

    std::wstringstream s;
    s << L"Dark Controls applied: "
      << classicMatched << L" stock SysMetrics colors and "
      << semanticMatched << L" Start-menu background roles. "
      << L"Tab/page surfaces and scrollbar-arrow contrast receive a final stock-Luna dark pass.";
    SetStatus(s.str().c_str());
}


enum
{
    IDC_DP_APPS=5201,
    IDC_DP_SYSTEM,
    IDC_DP_APPLY,
    IDC_DP_ALL_DARK,
    IDC_DP_ALL_LIGHT,
    IDC_DP_APPLY_XP_COLORS,
    IDC_DP_RESTORE_XP_COLORS,
    IDC_DP_CLOSE
};

static HWND gDarkPrefWnd=0;

static const wchar_t* LunaPersonalizeKey()
{
    return L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize";
}

static bool ReadLightThemeValue(const wchar_t* valueName,DWORD& value)
{
    value=1;
    HKEY key=0;
    if(RegOpenKeyExW(HKEY_CURRENT_USER,LunaPersonalizeKey(),0,KEY_QUERY_VALUE,&key)!=ERROR_SUCCESS)
        return false;

    DWORD type=0;
    DWORD size=sizeof(value);
    LONG rc=RegQueryValueExW(key,valueName,NULL,&type,(LPBYTE)&value,&size);
    RegCloseKey(key);

    if(rc!=ERROR_SUCCESS || type!=REG_DWORD)
    {
        value=1;
        return false;
    }

    return true;
}

static bool WriteLightThemeValue(const wchar_t* valueName,DWORD value)
{
    HKEY key=0;
    DWORD disposition=0;
    LONG rc=RegCreateKeyExW(HKEY_CURRENT_USER,LunaPersonalizeKey(),0,NULL,0,
                            KEY_SET_VALUE,NULL,&key,&disposition);
    if(rc!=ERROR_SUCCESS)
        return false;

    rc=RegSetValueExW(key,valueName,0,REG_DWORD,(const BYTE*)&value,sizeof(value));
    RegCloseKey(key);
    return rc==ERROR_SUCCESS;
}

static void BroadcastDarkPreferenceChange()
{
    DWORD_PTR result=0;
    SendMessageTimeoutW(HWND_BROADCAST,WM_SETTINGCHANGE,0,
                        (LPARAM)L"ImmersiveColorSet",
                        SMTO_ABORTIFHUNG|SMTO_NORMAL,1000,&result);
    SendMessageTimeoutW(HWND_BROADCAST,WM_SETTINGCHANGE,0,
                        (LPARAM)L"WindowsThemeElement",
                        SMTO_ABORTIFHUNG|SMTO_NORMAL,1000,&result);
}

static void RefreshDarkPreferenceControls(HWND h)
{
    DWORD appsLight=1,systemLight=1;
    ReadLightThemeValue(L"AppsUseLightTheme",appsLight);
    ReadLightThemeValue(L"SystemUsesLightTheme",systemLight);

    SendMessageW(GetDlgItem(h,IDC_DP_APPS),BM_SETCHECK,
                 appsLight==0?BST_CHECKED:BST_UNCHECKED,0);
    SendMessageW(GetDlgItem(h,IDC_DP_SYSTEM),BM_SETCHECK,
                 systemLight==0?BST_CHECKED:BST_UNCHECKED,0);
}

static bool SaveDarkPreferenceControls(HWND h)
{
    bool appsDark=SendMessageW(GetDlgItem(h,IDC_DP_APPS),BM_GETCHECK,0,0)==BST_CHECKED;
    bool systemDark=SendMessageW(GetDlgItem(h,IDC_DP_SYSTEM),BM_GETCHECK,0,0)==BST_CHECKED;

    if(!WriteLightThemeValue(L"AppsUseLightTheme",appsDark?0:1) ||
       !WriteLightThemeValue(L"SystemUsesLightTheme",systemDark?0:1))
    {
        MessageBoxW(h,L"Unable to write the per-user dark-mode preference.",
                    L"LunaXP Studio",MB_ICONERROR);
        return false;
    }

    BroadcastDarkPreferenceChange();

    std::wstringstream s;
    s << L"Application dark-mode preference saved. Apps: "
      << (appsDark?L"Dark":L"Light")
      << L"; System: "
      << (systemDark?L"Dark":L"Light")
      << L".";
    SetStatus(s.str().c_str());
    return true;
}


static const wchar_t* LunaColorBackupKey()
{
    return L"Software\\LunaXPStudio\\DarkModeBackup\\Colors";
}

static bool ReadColorString(const wchar_t* name,std::wstring& value)
{
    value.clear();
    HKEY key=0;
    if(RegOpenKeyExW(HKEY_CURRENT_USER,L"Control Panel\\Colors",0,KEY_QUERY_VALUE,&key)!=ERROR_SUCCESS)
        return false;

    wchar_t buf[128]={0};
    DWORD type=0;
    DWORD bytes=sizeof(buf);
    LONG rc=RegQueryValueExW(key,name,NULL,&type,(LPBYTE)buf,&bytes);
    RegCloseKey(key);

    if(rc!=ERROR_SUCCESS || (type!=REG_SZ && type!=REG_EXPAND_SZ))
        return false;

    value=buf;
    return true;
}

static bool WriteColorString(const wchar_t* name,const wchar_t* value)
{
    HKEY key=0;
    if(RegOpenKeyExW(HKEY_CURRENT_USER,L"Control Panel\\Colors",0,KEY_SET_VALUE,&key)!=ERROR_SUCCESS)
        return false;

    DWORD bytes=((DWORD)wcslen(value)+1u)*sizeof(wchar_t);
    LONG rc=RegSetValueExW(key,name,0,REG_SZ,(const BYTE*)value,bytes);
    RegCloseKey(key);
    return rc==ERROR_SUCCESS;
}

static bool BackupColorIfNeeded(const wchar_t* name)
{
    HKEY backup=0;
    DWORD disposition=0;
    if(RegCreateKeyExW(HKEY_CURRENT_USER,LunaColorBackupKey(),0,NULL,0,
                       KEY_QUERY_VALUE|KEY_SET_VALUE,NULL,&backup,&disposition)!=ERROR_SUCCESS)
        return false;

    std::wstring presentName=L"Present_";
    presentName+=name;

    DWORD existingType=0,existingBytes=0;
    if(RegQueryValueExW(backup,presentName.c_str(),NULL,&existingType,NULL,&existingBytes)==ERROR_SUCCESS)
    {
        RegCloseKey(backup);
        return true; // already backed up
    }

    std::wstring oldValue;
    bool present=ReadColorString(name,oldValue);

    DWORD flag=present?1u:0u;
    LONG rc=RegSetValueExW(backup,presentName.c_str(),0,REG_DWORD,
                           (const BYTE*)&flag,sizeof(flag));

    if(rc==ERROR_SUCCESS && present)
    {
        DWORD bytes=((DWORD)oldValue.size()+1u)*sizeof(wchar_t);
        rc=RegSetValueExW(backup,name,0,REG_SZ,(const BYTE*)oldValue.c_str(),bytes);
    }

    RegCloseKey(backup);
    return rc==ERROR_SUCCESS;
}


static bool ParseXpRgb(const wchar_t* value,COLORREF& color)
{
    if(!value) return false;
    int r=0,g=0,b=0;
    if(swscanf(value,L"%d %d %d",&r,&g,&b)!=3) return false;
    if(r<0)r=0;if(r>255)r=255;
    if(g<0)g=0;if(g>255)g=255;
    if(b<0)b=0;if(b>255)b=255;
    color=RGB(r,g,b);
    return true;
}

static int XpColorIndex(const wchar_t* name)
{
    if(_wcsicmp(name,L"Scrollbar")==0) return COLOR_SCROLLBAR;
    if(_wcsicmp(name,L"Window")==0) return COLOR_WINDOW;
    if(_wcsicmp(name,L"WindowText")==0) return COLOR_WINDOWTEXT;
    if(_wcsicmp(name,L"Menu")==0) return COLOR_MENU;
    if(_wcsicmp(name,L"MenuText")==0) return COLOR_MENUTEXT;
    if(_wcsicmp(name,L"ButtonFace")==0) return COLOR_BTNFACE;
    if(_wcsicmp(name,L"ButtonText")==0) return COLOR_BTNTEXT;
    if(_wcsicmp(name,L"ButtonShadow")==0) return COLOR_BTNSHADOW;
    if(_wcsicmp(name,L"ButtonHilight")==0) return COLOR_BTNHIGHLIGHT;
    if(_wcsicmp(name,L"GrayText")==0) return COLOR_GRAYTEXT;
    if(_wcsicmp(name,L"Hilight")==0) return COLOR_HIGHLIGHT;
    if(_wcsicmp(name,L"HilightText")==0) return COLOR_HIGHLIGHTTEXT;
    if(_wcsicmp(name,L"AppWorkSpace")==0) return COLOR_APPWORKSPACE;
    if(_wcsicmp(name,L"InfoText")==0) return COLOR_INFOTEXT;
    if(_wcsicmp(name,L"InfoWindow")==0) return COLOR_INFOBK;
#ifdef COLOR_HOTLIGHT
    if(_wcsicmp(name,L"HotTrackingColor")==0) return COLOR_HOTLIGHT;
#endif
#ifdef COLOR_MENUHILIGHT
    if(_wcsicmp(name,L"MenuHilight")==0) return COLOR_MENUHILIGHT;
#endif
#ifdef COLOR_MENUBAR
    if(_wcsicmp(name,L"MenuBar")==0) return COLOR_MENUBAR;
#endif
    return -1;
}

static void ApplyCurrentXpColor(const wchar_t* name,const wchar_t* value)
{
    int index=XpColorIndex(name);
    COLORREF c=0;
    if(index>=0 && ParseXpRgb(value,c))
        SetSysColors(1,&index,&c);
}


static void ApplyXpColorsToCurrentSession(const wchar_t* const* names,
                                          const wchar_t* const* values,
                                          size_t count)
{
    std::vector<int> indexes;
    std::vector<COLORREF> colors;

    for(size_t i=0;i<count;++i)
    {
        int index=XpColorIndex(names[i]);
        COLORREF c=0;
        if(index>=0 && ParseXpRgb(values[i],c))
        {
            indexes.push_back(index);
            colors.push_back(c);
        }
    }

    if(!indexes.empty())
        SetSysColors((int)indexes.size(),&indexes[0],&colors[0]);
}

static BOOL CALLBACK RefreshXpColorsChildProc(HWND hwnd,LPARAM)
{
    SendMessageTimeoutW(hwnd,WM_SYSCOLORCHANGE,0,0,
                        SMTO_ABORTIFHUNG|SMTO_NORMAL,150,NULL);
    InvalidateRect(hwnd,NULL,TRUE);
    return TRUE;
}

static BOOL CALLBACK RefreshXpColorsTopProc(HWND hwnd,LPARAM)
{
    SendMessageTimeoutW(hwnd,WM_SYSCOLORCHANGE,0,0,
                        SMTO_ABORTIFHUNG|SMTO_NORMAL,250,NULL);
    EnumChildWindows(hwnd,RefreshXpColorsChildProc,0);
    RedrawWindow(hwnd,NULL,NULL,
                 RDW_INVALIDATE|RDW_ERASE|RDW_ALLCHILDREN|RDW_FRAME);
    return TRUE;
}

static void BroadcastXpColorChange()
{
    DWORD_PTR result=0;
    SendMessageTimeoutW(HWND_BROADCAST,WM_SYSCOLORCHANGE,0,0,
                        SMTO_ABORTIFHUNG|SMTO_NORMAL,1000,&result);
    SendMessageTimeoutW(HWND_BROADCAST,WM_SETTINGCHANGE,0,
                        (LPARAM)L"Control Panel\\Colors",
                        SMTO_ABORTIFHUNG|SMTO_NORMAL,1000,&result);

    // XP Explorer and several common controls cache their brushes/text
    // colors. Refresh the existing window tree as well as broadcasting.
    EnumWindows(RefreshXpColorsTopProc,0);
    RedrawWindow(GetDesktopWindow(),NULL,NULL,
                 RDW_INVALIDATE|RDW_ERASE|RDW_ALLCHILDREN|RDW_FRAME);
}

static bool ApplyXpDarkForegroundColors()
{
    struct Pair { const wchar_t* name; const wchar_t* value; };
    static const Pair values[]={
        {L"Window",L"58 58 58"},
        {L"WindowText",L"245 245 245"},
        {L"Menu",L"68 68 68"},
        {L"MenuText",L"245 245 245"},
        {L"MenuBar",L"62 62 62"},
        {L"MenuHilight",L"62 86 118"},
        {L"ButtonFace",L"72 72 72"},
        {L"ButtonText",L"238 238 238"},
        {L"ButtonShadow",L"20 20 20"},
        {L"ButtonHilight",L"88 88 88"},
        {L"GrayText",L"232 232 232"},
        {L"Hilight",L"52 96 150"},
        {L"HilightText",L"255 255 255"},
        {L"AppWorkSpace",L"44 44 44"},
        {L"Scrollbar",L"64 64 64"},
        {L"InfoText",L"240 240 240"},
        {L"InfoWindow",L"64 64 64"},
        {L"HotTrackingColor",L"105 170 255"}
    };

    for(size_t i=0;i<sizeof(values)/sizeof(values[0]);++i)
    {
        if(!BackupColorIfNeeded(values[i].name))
            return false;
        if(!WriteColorString(values[i].name,values[i].value))
            return false;
    }

    {
        const size_t count=sizeof(values)/sizeof(values[0]);
        std::vector<const wchar_t*> names(count);
        std::vector<const wchar_t*> colorValues(count);
        for(size_t i=0;i<count;++i)
        {
            names[i]=values[i].name;
            colorValues[i]=values[i].value;
        }
        ApplyXpColorsToCurrentSession(&names[0],&colorValues[0],count);
    }

    BroadcastXpColorChange();
    return true;
}

static bool RestoreXpDarkForegroundColors()
{
    HKEY backup=0;
    if(RegOpenKeyExW(HKEY_CURRENT_USER,LunaColorBackupKey(),0,
                     KEY_QUERY_VALUE,&backup)!=ERROR_SUCCESS)
        return false;

    static const wchar_t* names[]={
        L"Window",L"WindowText",L"Menu",L"MenuText",L"MenuBar",L"MenuHilight",
        L"ButtonFace",L"ButtonText",L"ButtonShadow",L"ButtonHilight",L"GrayText",
        L"Hilight",L"HilightText",L"AppWorkSpace",L"Scrollbar",
        L"InfoText",L"InfoWindow",L"HotTrackingColor"
    };

    HKEY colors=0;
    if(RegOpenKeyExW(HKEY_CURRENT_USER,L"Control Panel\\Colors",0,
                     KEY_SET_VALUE,&colors)!=ERROR_SUCCESS)
    {
        RegCloseKey(backup);
        return false;
    }

    bool ok=true;

    for(size_t i=0;i<sizeof(names)/sizeof(names[0]);++i)
    {
        std::wstring presentName=L"Present_";
        presentName+=names[i];

        DWORD flag=0,type=0,bytes=sizeof(flag);
        if(RegQueryValueExW(backup,presentName.c_str(),NULL,&type,
                            (LPBYTE)&flag,&bytes)!=ERROR_SUCCESS ||
           type!=REG_DWORD)
            continue;

        if(flag)
        {
            wchar_t buf[128]={0};
            type=0;bytes=sizeof(buf);
            if(RegQueryValueExW(backup,names[i],NULL,&type,(LPBYTE)buf,&bytes)==ERROR_SUCCESS &&
               (type==REG_SZ || type==REG_EXPAND_SZ))
            {
                DWORD outBytes=((DWORD)wcslen(buf)+1u)*sizeof(wchar_t);
                if(RegSetValueExW(colors,names[i],0,REG_SZ,(const BYTE*)buf,outBytes)!=ERROR_SUCCESS)
                    ok=false;
                else
                    ApplyCurrentXpColor(names[i],buf);
            }
        }
        else
        {
            LONG rc=RegDeleteValueW(colors,names[i]);
            if(rc!=ERROR_SUCCESS && rc!=ERROR_FILE_NOT_FOUND)
                ok=false;
        }
    }

    RegCloseKey(colors);
    RegCloseKey(backup);

    // Delete backup only after a successful restore, so the user can retry.
    if(ok)
        RegDeleteKeyW(HKEY_CURRENT_USER,LunaColorBackupKey());

    BroadcastXpColorChange();
    return ok;
}

static LRESULT CALLBACK DarkPreferenceWndProc(HWND h,UINT msg,WPARAM wp,LPARAM lp)
{
    switch(msg)
    {
    case WM_CREATE:
        {
            HFONT f=(HFONT)GetStockObject(DEFAULT_GUI_FONT);

            HWND title=CreateWindowW(L"STATIC",
                L"Windows-compatible application dark-mode preference",
                WS_CHILD|WS_VISIBLE,18,16,390,22,h,NULL,gInst,NULL);
            SendMessageW(title,WM_SETFONT,(WPARAM)f,TRUE);

            HWND note=CreateWindowW(L"STATIC",
                L"These settings are stored per-user using the modern "
                L"AppsUseLightTheme / SystemUsesLightTheme registry convention. "
                L"XP itself does not consume them, but LunaDarkMode.dll and "
                L"compatible applications can.",
                WS_CHILD|WS_VISIBLE|SS_LEFT,18,43,430,72,h,NULL,gInst,NULL);
            SendMessageW(note,WM_SETFONT,(WPARAM)f,TRUE);

            HWND apps=CreateWindowW(L"BUTTON",L"Applications prefer dark mode",
                WS_CHILD|WS_VISIBLE|WS_TABSTOP|BS_AUTOCHECKBOX,
                24,122,280,26,h,(HMENU)(INT_PTR)IDC_DP_APPS,gInst,NULL);
            HWND sys=CreateWindowW(L"BUTTON",L"System UI preference is dark",
                WS_CHILD|WS_VISIBLE|WS_TABSTOP|BS_AUTOCHECKBOX,
                24,153,280,26,h,(HMENU)(INT_PTR)IDC_DP_SYSTEM,gInst,NULL);
            SendMessageW(apps,WM_SETFONT,(WPARAM)f,TRUE);
            SendMessageW(sys,WM_SETFONT,(WPARAM)f,TRUE);

            MakeButton(h,L"All dark",IDC_DP_ALL_DARK,18,198,92,28);
            MakeButton(h,L"All light",IDC_DP_ALL_LIGHT,116,198,92,28);
            MakeButton(h,L"Apply preference",IDC_DP_APPLY,220,198,126,28);

            HWND xpNote=CreateWindowW(L"STATIC",
                L"Optional XP classic-text colors:",
                WS_CHILD|WS_VISIBLE,18,242,250,20,h,NULL,gInst,NULL);
            SendMessageW(xpNote,WM_SETFONT,(WPARAM)f,TRUE);

            MakeButton(h,L"Apply XP dark colors",IDC_DP_APPLY_XP_COLORS,18,266,190,28);
            MakeButton(h,L"Restore previous colors",IDC_DP_RESTORE_XP_COLORS,216,266,190,28);
            MakeButton(h,L"Close",IDC_DP_CLOSE,354,310,88,28);

            HWND child=GetWindow(h,GW_CHILD);
            while(child)
            {
                SendMessageW(child,WM_SETFONT,(WPARAM)f,TRUE);
                child=GetWindow(child,GW_HWNDNEXT);
            }

            RefreshDarkPreferenceControls(h);
        }
        return 0;

    case WM_COMMAND:
        switch(LOWORD(wp))
        {
        case IDC_DP_ALL_DARK:
            SendMessageW(GetDlgItem(h,IDC_DP_APPS),BM_SETCHECK,BST_CHECKED,0);
            SendMessageW(GetDlgItem(h,IDC_DP_SYSTEM),BM_SETCHECK,BST_CHECKED,0);
            return 0;

        case IDC_DP_ALL_LIGHT:
            SendMessageW(GetDlgItem(h,IDC_DP_APPS),BM_SETCHECK,BST_UNCHECKED,0);
            SendMessageW(GetDlgItem(h,IDC_DP_SYSTEM),BM_SETCHECK,BST_UNCHECKED,0);
            return 0;

        case IDC_DP_APPLY:
            SaveDarkPreferenceControls(h);
            MessageBoxW(h,L"Preferences were applied.",
                        L"LunaXP Studio",MB_ICONINFORMATION);
            return 0;

        case IDC_DP_APPLY_XP_COLORS:
            if(ApplyXpDarkForegroundColors())
                MessageBoxW(h,
                    L"XP dark colors applied. Text boxes, list/tree views, combo fields and classic "
                    L"control surfaces now use a lighter dark gray for readability.\r\n\r\n"
                    L"Some running applications may need to be restarted.",
                    L"LunaXP Studio",MB_ICONINFORMATION);
            else
                MessageBoxW(h,L"Could not apply XP foreground colors.",
                            L"LunaXP Studio",MB_ICONERROR);
            return 0;

        case IDC_DP_RESTORE_XP_COLORS:
            if(RestoreXpDarkForegroundColors())
                MessageBoxW(h,
                    L"Previous XP foreground colors restored.\r\n\r\n"
                    L"Some running applications may need to be restarted.",
                    L"LunaXP Studio",MB_ICONINFORMATION);
            else
                MessageBoxW(h,
                    L"No LunaXP Studio color backup was found, or the restore failed.",
                    L"LunaXP Studio",MB_ICONWARNING);
            return 0;

        case IDC_DP_CLOSE:
            DestroyWindow(h);
            return 0;
        }
        break;

    case WM_CLOSE:
        DestroyWindow(h);
        return 0;

    case WM_DESTROY:
        gDarkPrefWnd=0;
        return 0;
    }

    return DefWindowProcW(h,msg,wp,lp);
}

static void OpenDarkPreferenceWindow()
{
    if(gDarkPrefWnd)
    {
        SetForegroundWindow(gDarkPrefWnd);
        return;
    }

    static bool registered=false;
    if(!registered)
    {
        WNDCLASSEXW wc={0};
        wc.cbSize=sizeof(wc);
        wc.hInstance=gInst;
        wc.lpfnWndProc=DarkPreferenceWndProc;
        wc.lpszClassName=L"LunaDarkPreferenceWindow";
        wc.hCursor=LoadCursor(NULL,IDC_ARROW);
        wc.hbrBackground=(HBRUSH)(COLOR_BTNFACE+1);

        if(!RegisterClassExW(&wc) && GetLastError()!=ERROR_CLASS_ALREADY_EXISTS)
            return;
        registered=true;
    }

    gDarkPrefWnd=CreateWindowExW(WS_EX_TOOLWINDOW,L"LunaDarkPreferenceWindow",
                                 L"Application dark mode",
                                 WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU,
                                 CW_USEDEFAULT,CW_USEDEFAULT,480,390,
                                 gWnd,NULL,gInst,NULL);
    if(gDarkPrefWnd)
    {
        ShowWindow(gDarkPrefWnd,SW_SHOW);
        UpdateWindow(gDarkPrefWnd);
    }
}


static void SavePreset()
{
    if(gEdits.empty())
    {
        MessageBoxW(gWnd,L"There are no edited role colors to save.",L"LunaXP Studio",MB_ICONWARNING);
        return;
    }

    std::wstring path;
    if(!PickSave(gWnd,L"Palette presets (*.ini)\0*.ini\0All files\0*.*\0",L"ini",path))
        return;

    std::wstring err;
    if(!ThemeGenerator::SavePreset(path,CurrentEdits(),err))
    {
        MessageBoxW(gWnd,err.c_str(),L"LunaXP Studio",MB_ICONERROR);
        return;
    }
    SetStatus(L"Palette preset saved.");
}


enum
{
    IDC_CA_ITEM=3001,
    IDC_CA_COLOR1,
    IDC_CA_COLOR2,
    IDC_CA_TEXTCOLOR,
    IDC_CA_FONT,
    IDC_CA_FONTSIZE,
    IDC_CA_BOLD,
    IDC_CA_ITALIC,
    IDC_CA_ITEMSIZE,
    IDC_CA_RESET,
    IDC_CA_RESETALL,
    IDC_CA_CLOSE
};

struct ClassicAppearanceItem
{
    const wchar_t* label;
    const wchar_t* color1;
    const wchar_t* color2;
    const wchar_t* textColor;
    const wchar_t* font;
    const wchar_t* metric;
};

static const ClassicAppearanceItem gClassicItems[] =
{
    {L"Active Title Bar",   L"ActiveCaption",   L"GradientActiveCaption",   L"CaptionText",         L"CaptionFont",      L"CaptionBarHeight"},
    {L"Inactive Title Bar", L"InactiveCaption", L"GradientInactiveCaption", L"InactiveCaptionText", L"CaptionFont",      L"CaptionBarHeight"},
    {L"Small Caption",      L"InactiveCaption", NULL,                       L"InactiveCaptionText", L"SmallCaptionFont", L"SMCaptionBarHeight"},
    {L"Window",             L"Window",          NULL,                       NULL,                    NULL,                NULL},
    {L"Desktop",            L"Background",      NULL,                       NULL,                    NULL,                NULL},
    {L"Menu",               L"Menu",            NULL,                       NULL,                    L"MenuFont",         NULL},
    {L"Menu Bar",           L"MenuBar",         NULL,                       NULL,                    L"MenuFont",         NULL},
    {L"Selected Items",     L"Highlight",       NULL,                       L"HighlightText",        NULL,                NULL},
    {L"Menu Highlight",     L"MenuHilight",     NULL,                       L"HighlightText",        L"MenuFont",         NULL},
    {L"Button Face",        L"Btnface",         NULL,                       NULL,                    NULL,                NULL},
    {L"Message Box",        L"Btnface",         NULL,                       NULL,                    L"MsgBoxFont",       NULL},
    {L"Status Bar",         L"Btnface",         NULL,                       NULL,                    L"StatusFont",       NULL},
    {L"Icon Text",          NULL,                NULL,                       NULL,                    L"IconTitleFont",    NULL},
    {L"Scrollbar",          NULL,                NULL,                       NULL,                    NULL,                L"ScrollbarWidth"}
};

static HWND gClassicWnd=0;
static int gClassicItemIndex=0;

static ThemeClassicColor* CaColor(const wchar_t* key)
{
    if(!key) return NULL;
    for(size_t i=0;i<gClassicColors.size();++i)
        if(_wcsicmp(gClassicColors[i].key.c_str(),key)==0) return &gClassicColors[i];
    return NULL;
}

static ThemeClassicFont* CaFont(const wchar_t* key)
{
    if(!key) return NULL;
    for(size_t i=0;i<gClassicFonts.size();++i)
        if(_wcsicmp(gClassicFonts[i].key.c_str(),key)==0) return &gClassicFonts[i];
    return NULL;
}

static ThemeClassicMetric* CaMetric(const wchar_t* key)
{
    if(!key) return NULL;
    for(size_t i=0;i<gClassicMetrics.size();++i)
        if(_wcsicmp(gClassicMetrics[i].key.c_str(),key)==0) return &gClassicMetrics[i];
    return NULL;
}

static void CaSetButtonColorText(HWND h,int id,const wchar_t* label,ThemeClassicColor* c)
{
    std::wstringstream s;s<<label;
    if(c)
    {
        wchar_t rgb[16]={0};wsprintfW(rgb,L"  #%02X%02X%02X",c->r,c->g,c->b);
        s<<rgb;
    }
    SetWindowTextW(GetDlgItem(h,id),s.str().c_str());
    EnableWindow(GetDlgItem(h,id),c!=NULL);
}

static void CaPopulateFontCombo(HWND h,ThemeClassicFont* f)
{
    HWND cb=GetDlgItem(h,IDC_CA_FONT);
    SendMessageW(cb,CB_RESETCONTENT,0,0);
    const wchar_t* names[]={L"Tahoma",L"Trebuchet MS",L"Microsoft Sans Serif",L"MS Sans Serif",L"Arial",L"Verdana"};
    int select=0;
    bool insertedCurrent=false;
    if(f && !f->face.empty())
    {
        SendMessageW(cb,CB_ADDSTRING,0,(LPARAM)f->face.c_str());
        insertedCurrent=true;
    }
    for(size_t i=0;i<sizeof(names)/sizeof(names[0]);++i)
    {
        if(f && _wcsicmp(names[i],f->face.c_str())==0) continue;
        SendMessageW(cb,CB_ADDSTRING,0,(LPARAM)names[i]);
    }
    SendMessageW(cb,CB_SETCURSEL,select,0);
    EnableWindow(cb,f!=NULL);
}

static void CaRefreshControls(HWND h)
{
    if(gClassicItemIndex<0 || gClassicItemIndex>=(int)(sizeof(gClassicItems)/sizeof(gClassicItems[0])))
        gClassicItemIndex=0;
    const ClassicAppearanceItem& item=gClassicItems[gClassicItemIndex];

    ThemeClassicColor* c1=CaColor(item.color1);
    ThemeClassicColor* c2=CaColor(item.color2);
    ThemeClassicColor* tc=CaColor(item.textColor);
    ThemeClassicFont* font=CaFont(item.font);
    ThemeClassicMetric* metric=CaMetric(item.metric);

    CaSetButtonColorText(h,IDC_CA_COLOR1,L"Color 1...",c1);
    CaSetButtonColorText(h,IDC_CA_COLOR2,L"Color 2...",c2);
    CaSetButtonColorText(h,IDC_CA_TEXTCOLOR,L"Text color...",tc);

    CaPopulateFontCombo(h,font);

    wchar_t n[32]={0};
    if(font) wsprintfW(n,L"%u",font->pointSize); else n[0]=0;
    SetWindowTextW(GetDlgItem(h,IDC_CA_FONTSIZE),n);
    EnableWindow(GetDlgItem(h,IDC_CA_FONTSIZE),font!=NULL);
    EnableWindow(GetDlgItem(h,IDC_CA_BOLD),font!=NULL);
    EnableWindow(GetDlgItem(h,IDC_CA_ITALIC),font!=NULL);
    SendMessageW(GetDlgItem(h,IDC_CA_BOLD),BM_SETCHECK,font&&font->bold?BST_CHECKED:BST_UNCHECKED,0);
    SendMessageW(GetDlgItem(h,IDC_CA_ITALIC),BM_SETCHECK,font&&font->italic?BST_CHECKED:BST_UNCHECKED,0);

    if(metric) wsprintfW(n,L"%d",metric->value); else n[0]=0;
    SetWindowTextW(GetDlgItem(h,IDC_CA_ITEMSIZE),n);
    EnableWindow(GetDlgItem(h,IDC_CA_ITEMSIZE),metric!=NULL);

    InvalidateRect(h,NULL,TRUE);
}

static void CaCommitTextFields(HWND h)
{
    const ClassicAppearanceItem& item=gClassicItems[gClassicItemIndex];
    ThemeClassicFont* font=CaFont(item.font);
    ThemeClassicMetric* metric=CaMetric(item.metric);

    if(font)
    {
        wchar_t face[LF_FACESIZE]={0};
        int sel=(int)SendMessageW(GetDlgItem(h,IDC_CA_FONT),CB_GETCURSEL,0,0);
        if(sel!=CB_ERR)
            SendMessageW(GetDlgItem(h,IDC_CA_FONT),CB_GETLBTEXT,sel,(LPARAM)face);
        if(face[0] && _wcsicmp(face,font->face.c_str())!=0){font->face=face;font->edited=true;}

        wchar_t sz[32]={0};GetWindowTextW(GetDlgItem(h,IDC_CA_FONTSIZE),sz,31);
        unsigned int ps=0;if(swscanf(sz,L"%u",&ps)==1 && ps>=5 && ps<=72 && ps!=font->pointSize)
        {font->pointSize=ps;font->edited=true;}

        bool bold=SendMessageW(GetDlgItem(h,IDC_CA_BOLD),BM_GETCHECK,0,0)==BST_CHECKED;
        bool italic=SendMessageW(GetDlgItem(h,IDC_CA_ITALIC),BM_GETCHECK,0,0)==BST_CHECKED;
        if(bold!=font->bold || italic!=font->italic){font->bold=bold;font->italic=italic;font->edited=true;}
    }

    if(metric)
    {
        wchar_t sz[32]={0};GetWindowTextW(GetDlgItem(h,IDC_CA_ITEMSIZE),sz,31);
        int v=0;if(swscanf(sz,L"%d",&v)==1 && v>=8 && v<=100 && v!=metric->value)
        {metric->value=v;metric->edited=true;}

        // Classic Appearance treats scrollbar size as a square dimension.
        if(_wcsicmp(metric->key.c_str(),L"ScrollbarWidth")==0)
        {
            ThemeClassicMetric* mh=CaMetric(L"ScrollbarHeight");
            if(mh && mh->value!=metric->value){mh->value=metric->value;mh->edited=true;}
        }
        if(_wcsicmp(metric->key.c_str(),L"SMCaptionBarHeight")==0)
        {
            ThemeClassicMetric* mw=CaMetric(L"SMCaptionBarWidth");
            if(mw && mw->value!=metric->value){mw->value=metric->value;mw->edited=true;}
        }
    }
    RefreshLiveThemePreview();
}

static void CaChooseColor(HWND h,ThemeClassicColor* c)
{
    if(!c) return;
    static COLORREF custom[16]={0};
    CHOOSECOLORW cc={0};cc.lStructSize=sizeof(cc);cc.hwndOwner=h;cc.lpCustColors=custom;
    cc.Flags=CC_FULLOPEN|CC_RGBINIT;cc.rgbResult=RGB(c->r,c->g,c->b);
    if(ChooseColorW(&cc))
    {
        c->r=GetRValue(cc.rgbResult);c->g=GetGValue(cc.rgbResult);c->b=GetBValue(cc.rgbResult);c->edited=true;
        CaRefreshControls(h);
        RefreshLiveThemePreview();
    }
}

static void CaPaintPreview(HWND h,HDC dc)
{
    RECT box={18,54,602,205};
    HBRUSH white=CreateSolidBrush(RGB(255,255,255));FillRect(dc,&box,white);DeleteObject(white);
    FrameRect(dc,&box,(HBRUSH)GetStockObject(BLACK_BRUSH));

    const ClassicAppearanceItem& item=gClassicItems[gClassicItemIndex];
    ThemeClassicColor* c1=CaColor(item.color1);
    ThemeClassicColor* c2=CaColor(item.color2);
    ThemeClassicColor* tc=CaColor(item.textColor);
    ThemeClassicFont* font=CaFont(item.font);

    RECT sample={34,78,586,129};
    COLORREF a=c1?RGB(c1->r,c1->g,c1->b):GetSysColor(COLOR_ACTIVECAPTION);
    COLORREF b=c2?RGB(c2->r,c2->g,c2->b):a;

    for(int x=sample.left;x<sample.right;++x)
    {
        double t=(double)(x-sample.left)/(double)(sample.right-sample.left-1);
        BYTE r=(BYTE)(GetRValue(a)+(GetRValue(b)-GetRValue(a))*t);
        BYTE g=(BYTE)(GetGValue(a)+(GetGValue(b)-GetGValue(a))*t);
        BYTE bl=(BYTE)(GetBValue(a)+(GetBValue(b)-GetBValue(a))*t);
        HPEN p=CreatePen(PS_SOLID,1,RGB(r,g,bl));HPEN old=(HPEN)SelectObject(dc,p);
        MoveToEx(dc,x,sample.top,NULL);LineTo(dc,x,sample.bottom);SelectObject(dc,old);DeleteObject(p);
    }
    FrameRect(dc,&sample,(HBRUSH)GetStockObject(BLACK_BRUSH));

    LOGFONTW lf={0};lf.lfCharSet=DEFAULT_CHARSET;
    if(font)
    {
        lstrcpynW(lf.lfFaceName,font->face.c_str(),LF_FACESIZE);
        HDC sdc=GetDC(h);
        lf.lfHeight=-MulDiv((int)font->pointSize,GetDeviceCaps(sdc,LOGPIXELSY),72);
        ReleaseDC(h,sdc);
        lf.lfWeight=font->bold?FW_BOLD:FW_NORMAL;
        lf.lfItalic=font->italic?TRUE:FALSE;
    }
    else
    {
        lstrcpynW(lf.lfFaceName,L"Tahoma",LF_FACESIZE);
        lf.lfHeight=-11;
    }

    HFONT f=CreateFontIndirectW(&lf);HFONT oldF=(HFONT)SelectObject(dc,f);
    SetBkMode(dc,TRANSPARENT);
    SetTextColor(dc,tc?RGB(tc->r,tc->g,tc->b):RGB(0,0,0));
    RECT textRc=sample;InflateRect(&textRc,-8,-4);
    DrawTextW(dc,item.label,-1,&textRc,DT_LEFT|DT_VCENTER|DT_SINGLELINE);
    SelectObject(dc,oldF);DeleteObject(f);
}

static LRESULT CALLBACK ClassicWndProc(HWND h,UINT msg,WPARAM wp,LPARAM lp)
{
    switch(msg)
    {
    case WM_CREATE:
        gClassicWnd=h;
        MakeLabel(h,L"Item:",18,18,70,20);
        {
            HWND cb=CreateWindowW(L"COMBOBOX",L"",WS_CHILD|WS_VISIBLE|WS_TABSTOP|CBS_DROPDOWNLIST,
                                  84,14,250,300,h,(HMENU)(INT_PTR)IDC_CA_ITEM,gInst,NULL);
            for(size_t i=0;i<sizeof(gClassicItems)/sizeof(gClassicItems[0]);++i)
                SendMessageW(cb,CB_ADDSTRING,0,(LPARAM)gClassicItems[i].label);
            SendMessageW(cb,CB_SETCURSEL,0,0);
        }

        MakeLabel(h,L"Item size:",350,18,68,20);
        MakeEdit(h,IDC_CA_ITEMSIZE,420,14,62,24,L"");

        MakeButton(h,L"Color 1...",IDC_CA_COLOR1,18,218,170,28);
        MakeButton(h,L"Color 2...",IDC_CA_COLOR2,198,218,170,28);
        MakeButton(h,L"Text color...",IDC_CA_TEXTCOLOR,378,218,170,28);

        MakeLabel(h,L"Font:",18,260,55,20);
        CreateWindowW(L"COMBOBOX",L"",WS_CHILD|WS_VISIBLE|WS_TABSTOP|CBS_DROPDOWNLIST,
                      76,256,220,220,h,(HMENU)(INT_PTR)IDC_CA_FONT,gInst,NULL);
        MakeLabel(h,L"Size:",308,260,42,20);
        MakeEdit(h,IDC_CA_FONTSIZE,350,256,55,24,L"");
        CreateWindowW(L"BUTTON",L"B",WS_CHILD|WS_VISIBLE|WS_TABSTOP|BS_AUTOCHECKBOX|BS_PUSHLIKE,
                      418,256,42,26,h,(HMENU)(INT_PTR)IDC_CA_BOLD,gInst,NULL);
        CreateWindowW(L"BUTTON",L"I",WS_CHILD|WS_VISIBLE|WS_TABSTOP|BS_AUTOCHECKBOX|BS_PUSHLIKE,
                      468,256,42,26,h,(HMENU)(INT_PTR)IDC_CA_ITALIC,gInst,NULL);

        MakeButton(h,L"Reset item",IDC_CA_RESET,18,304,105,28);
        MakeButton(h,L"Reset all",IDC_CA_RESETALL,132,304,105,28);
        MakeButton(h,L"Close",IDC_CA_CLOSE,497,304,85,28);

        {
            HFONT f=(HFONT)GetStockObject(DEFAULT_GUI_FONT);
            HWND ch=GetWindow(h,GW_CHILD);while(ch){SendMessageW(ch,WM_SETFONT,(WPARAM)f,TRUE);ch=GetWindow(ch,GW_HWNDNEXT);}
        }
        CaRefreshControls(h);
        return 0;

    case WM_PAINT:
        {
            PAINTSTRUCT ps;HDC dc=BeginPaint(h,&ps);CaPaintPreview(h,dc);EndPaint(h,&ps);
        }
        return 0;

    case WM_COMMAND:
        if(HIWORD(wp)==CBN_SELCHANGE && LOWORD(wp)==IDC_CA_ITEM)
        {
            CaCommitTextFields(h);
            int sel=(int)SendMessageW(GetDlgItem(h,IDC_CA_ITEM),CB_GETCURSEL,0,0);
            if(sel!=CB_ERR) gClassicItemIndex=sel;
            CaRefreshControls(h);return 0;
        }
        switch(LOWORD(wp))
        {
        case IDC_CA_COLOR1: CaChooseColor(h,CaColor(gClassicItems[gClassicItemIndex].color1));return 0;
        case IDC_CA_COLOR2: CaChooseColor(h,CaColor(gClassicItems[gClassicItemIndex].color2));return 0;
        case IDC_CA_TEXTCOLOR: CaChooseColor(h,CaColor(gClassicItems[gClassicItemIndex].textColor));return 0;
        case IDC_CA_CLOSE: CaCommitTextFields(h);DestroyWindow(h);return 0;
        case IDC_CA_RESET:
            {
                std::vector<ThemeClassicColor> cs;std::vector<ThemeClassicFont> fs;std::vector<ThemeClassicMetric> ms;std::wstring err;
                ThemeGenerator::GetClassicColors(gSourceCache,cs,err);
                ThemeGenerator::GetClassicFonts(gSourceCache,fs,err);
                ThemeGenerator::GetClassicMetrics(gSourceCache,ms,err);
                const ClassicAppearanceItem& item=gClassicItems[gClassicItemIndex];
                for(size_t i=0;i<cs.size();++i)
                    for(size_t j=0;j<gClassicColors.size();++j)
                        if((_wcsicmp(cs[i].key.c_str(),gClassicColors[j].key.c_str())==0) &&
                           ((item.color1&&_wcsicmp(item.color1,cs[i].key.c_str())==0) ||
                            (item.color2&&_wcsicmp(item.color2,cs[i].key.c_str())==0) ||
                            (item.textColor&&_wcsicmp(item.textColor,cs[i].key.c_str())==0)))
                            gClassicColors[j]=cs[i];
                if(item.font)
                    for(size_t i=0;i<fs.size();++i)for(size_t j=0;j<gClassicFonts.size();++j)
                        if(_wcsicmp(fs[i].key.c_str(),item.font)==0 && _wcsicmp(gClassicFonts[j].key.c_str(),item.font)==0)
                            gClassicFonts[j]=fs[i];
                if(item.metric)
                    for(size_t i=0;i<ms.size();++i)for(size_t j=0;j<gClassicMetrics.size();++j)
                        if(_wcsicmp(ms[i].key.c_str(),item.metric)==0 && _wcsicmp(gClassicMetrics[j].key.c_str(),item.metric)==0)
                            gClassicMetrics[j]=ms[i];
                CaRefreshControls(h);return 0;
            }
        case IDC_CA_RESETALL:
            {
                std::wstring err;
                ThemeGenerator::GetClassicColors(gSourceCache,gClassicColors,err);
                ThemeGenerator::GetClassicFonts(gSourceCache,gClassicFonts,err);
                ThemeGenerator::GetClassicMetrics(gSourceCache,gClassicMetrics,err);
                CaRefreshControls(h);return 0;
            }
        }
        break;

    case WM_CLOSE:
        CaCommitTextFields(h);DestroyWindow(h);return 0;
    case WM_DESTROY:
        gClassicWnd=0;return 0;
    }
    return DefWindowProcW(h,msg,wp,lp);
}

static void OpenClassicColors()
{
    if(!gSourceCache.ready)
    {
        MessageBoxW(gWnd,L"Load semantic roles first.",L"LunaXP Studio",MB_ICONWARNING);return;
    }

    if(gClassicColors.empty())
    {
        std::wstring err;
        ThemeGenerator::GetClassicColors(gSourceCache,gClassicColors,err);
        ThemeGenerator::GetClassicFonts(gSourceCache,gClassicFonts,err);
        ThemeGenerator::GetClassicMetrics(gSourceCache,gClassicMetrics,err);
    }

    if(gClassicWnd){SetForegroundWindow(gClassicWnd);return;}

    WNDCLASSEXW wc={0};wc.cbSize=sizeof(wc);wc.hInstance=gInst;wc.lpfnWndProc=ClassicWndProc;
    wc.lpszClassName=L"LunaClassicAppearanceWindow";wc.hCursor=LoadCursor(NULL,IDC_ARROW);
    wc.hbrBackground=(HBRUSH)(COLOR_BTNFACE+1);
    RegisterClassExW(&wc);

    gClassicItemIndex=0;
    gClassicWnd=CreateWindowExW(WS_EX_DLGMODALFRAME,L"LunaClassicAppearanceWindow",
                                L"Advanced Appearance",
                                WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU,
                                CW_USEDEFAULT,CW_USEDEFAULT,630,380,gWnd,NULL,gInst,NULL);
    if(gClassicWnd){ShowWindow(gClassicWnd,SW_SHOW);UpdateWindow(gClassicWnd);}
}


static HWND gHelpWnd=0;

static LRESULT CALLBACK HelpWndProc(HWND h,UINT msg,WPARAM wp,LPARAM lp)
{
    switch(msg)
    {
    case WM_CREATE:
        {
            const wchar_t* text=
                L"LunaXP Studio - Quick Help\r\n\r\n"
                L"1. Source Luna.msstyles\r\n"
                L"   The system Luna theme is selected automatically when available. "
                L"Use Browse to choose another .msstyles file.\r\n\r\n"
                L"2. Color Scheme\r\n"
                L"   Select Blue, Homestead, or Metallic, then click Load semantic roles. "
                L"Themer loads the semantic map and bitmap resources into memory.\r\n\r\n"
                L"3. Semantic Roles\r\n"
                L"   Select a role on the left. Original source shows the stock state; "
                L"Edited / Working shows the current edit. Use Previous / Next to move among "
                L"the role's targets.\r\n\r\n"
                L"4. Set Color / Set Gradient\r\n"
                L"   Change color applies one color to the selected role. Gradient lets "
                L"you choose left and right colors. Color All changes the safe large theme "
                L"surfaces while avoiding fragile glyph/mask resources.\r\n\r\n"
                L"5. Advanced\r\n"
                L"   Opens Advanced Appearance-style settings for classic colors, fonts, "
                L"font sizes, caption sizes, scrollbar sizes, and related SysMetrics.\r\n\r\n"
                L"6. Save BMP / Replace BMP\r\n"
                L"   Export the selected bitmap state for editing in an external image editor. "
                L"Replace BMP imports the edited state back into the working theme.\r\n\r\n"
                L"7. Live Luna Theme Preview\r\n"
                L"   The preview window updates from the working theme and shows captions, "
                L"window borders, caption buttons, menus, client area, message box, and taskbar.\r\n\r\n"
                L"8. Make Dark Theme\r\n"
                L"   Applies a dark UI preset using Classic/System colors only. "
                L"It does not recolor Luna bitmap/sprite resources, so titlebars, caption buttons, "
                L"taskbar, Start menu and themed glyph geometry remain untouched.\r\n\r\n"
                L"Some legacy Control Panel pages (for example parts of System Properties) "
                L"paint their client area themselves. If such a page remains white after both "
                L"the dark .msstyles and Apply XP dark colors, that surface is application-owned "
                L"rather than controlled by Luna/SysMetrics.\r\n\r\n"
                L"Important on Windows XP: first generate/load the dark .msstyles theme, "
                L"then use Set Dark Mode -> Apply XP dark colors. "
                L"The XP color step supplies classic/system foreground and fallback colors "
                L"that the visual style alone cannot reliably override.\r\n\r\n"
                L"9. Set Dark Mode\r\n"
                L"   Stores AppsUseLightTheme and SystemUsesLightTheme under the current user. "
                L"The included LunaDarkMode.dll reads these values and exposes an XP-compatible "
                L"dark-mode query API for applications.\r\n\r\n"
                L"10. Generate Theme\r\n"
                L"   Choose an output .msstyles path and click Generate theme. "
                L"The source file is never modified in place.\r\n\r\n"
                L"Dark mode\r\n"
                L"   LunaXP Studio can create a dark XP visual style by recoloring themed assets and "
                L"Classic/System colors. Legacy applications that use Windows system colors will often "
                L"follow those dark colors automatically. XP does not provide the modern Windows 10+ "
                L"AppsUseLightTheme/SystemUsesLightTheme convention, so applications with their own "
                L"hard-coded or custom UI cannot be forced into dark mode by .msstyles alone.\r\n\r\n"
                L"Notes\r\n"
                L"   Some XP theme resources contain masks, sprite strips, and sizing metadata. "
                L"LunaXP Studio preserves those structures where possible. Always test generated "
                L"themes in a Virtual Machine before using them on live installation. Use on your own risk.";

            HWND e=CreateWindowExW(WS_EX_CLIENTEDGE,L"EDIT",text,
                WS_CHILD|WS_VISIBLE|WS_VSCROLL|ES_MULTILINE|ES_READONLY|ES_AUTOVSCROLL,
                12,12,576,430,h,NULL,gInst,NULL);
            if(e)
                SendMessageW(e,WM_SETFONT,(WPARAM)GetStockObject(DEFAULT_GUI_FONT),TRUE);
        }
        return 0;

    case WM_CLOSE:
        DestroyWindow(h);
        return 0;

    case WM_DESTROY:
        gHelpWnd=0;
        return 0;
    }
    return DefWindowProcW(h,msg,wp,lp);
}

static void OpenHelpWindow()
{
    if(gHelpWnd)
    {
        SetForegroundWindow(gHelpWnd);
        return;
    }

    static bool registered=false;
    if(!registered)
    {
        WNDCLASSEXW wc={0};
        wc.cbSize=sizeof(wc);
        wc.hInstance=gInst;
        wc.lpfnWndProc=HelpWndProc;
        wc.lpszClassName=L"LunaXPStudioHelpWindow";
        wc.hCursor=LoadCursor(NULL,IDC_ARROW);
        wc.hbrBackground=(HBRUSH)(COLOR_BTNFACE+1);
        if(!RegisterClassExW(&wc) && GetLastError()!=ERROR_CLASS_ALREADY_EXISTS)
            return;
        registered=true;
    }

    gHelpWnd=CreateWindowExW(WS_EX_TOOLWINDOW,L"LunaXPStudioHelpWindow",
                             L"LunaXP Studio Help",
                             WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU|WS_THICKFRAME,
                             CW_USEDEFAULT,CW_USEDEFAULT,620,500,
                             gWnd,NULL,gInst,NULL);
    if(gHelpWnd)
    {
        ShowWindow(gHelpWnd,SW_SHOW);
        UpdateWindow(gHelpWnd);
    }
}


static void GenerateTheme()
{
    std::wstring source=GetText(GetDlgItem(gWnd,IDC_SOURCE));
    std::wstring output=GetText(GetDlgItem(gWnd,IDC_OUTPUT));
    if(!FileExists(source))
    {
        MessageBoxW(gWnd,L"Choose an existing Luna .msstyles source file.",
                    L"LunaXP Studio",MB_ICONWARNING);
        return;
    }
    if(output.empty())
    {
        MessageBoxW(gWnd,L"Choose an output .msstyles file.",
                    L"LunaXP Studio",MB_ICONWARNING);
        return;
    }
    bool classicEdited=false;
    for(size_t ci=0;ci<gClassicColors.size();++ci)if(gClassicColors[ci].edited){classicEdited=true;break;}
    if(!classicEdited)for(size_t fi=0;fi<gClassicFonts.size();++fi)if(gClassicFonts[fi].edited){classicEdited=true;break;}
    if(!classicEdited)for(size_t mi=0;mi<gClassicMetrics.size();++mi)if(gClassicMetrics[mi].edited){classicEdited=true;break;}
    if(gEdits.empty() && !ThemeGenerator::HasBitmapOverrides() && !classicEdited)
    {
        MessageBoxW(gWnd,L"Edit a semantic role, classic/system color, or bitmap before generating.",
                    L"LunaXP Studio",MB_ICONWARNING);return;
    }

    EnableWindow(GetDlgItem(gWnd,IDC_GENERATE),FALSE);
    SetStatus(L"Generating theme file...");

    ThemeGenerateResult result;
    std::wstring err;
    bool ok=ThemeGenerator::GenerateFromColorsAndClassic(source,SelectedDefinition(),CurrentEdits(),
                                                         gClassicColors,gClassicFonts,gClassicMetrics,
                                                         output,180.0,result,err);

    EnableWindow(GetDlgItem(gWnd,IDC_GENERATE),TRUE);

    if(!ok)
    {
        SetStatus(L"Generation failed.");
        MessageBoxW(gWnd,err.c_str(),L"LunaXP Studio",MB_ICONERROR);
        return;
    }

    std::wstringstream summary;
    summary << L"Theme generated successfully.";

    SetStatus(L"Theme generated successfully.");
    MessageBoxW(gWnd,summary.str().c_str(),L"LunaXP Studio",MB_ICONINFORMATION);
}



static bool GetRoleStatePreview(const wchar_t* role,
                                std::vector<BYTE>& dib,
                                BitmapInfoSummary& bi,
                                ThemeRoleStateInfo& si)
{
    if(!gSourceCache.ready) return false;
    ThemeRoleColor* edit=FindEdit(role);
    std::wstring err;
    if(!ThemeGenerator::BuildRoleStatePreviewCached(gSourceCache,role,0,edit,false,dib,si,err))
        return false;
    if(!BitmapEngine::ParseDib(dib,bi,err))
        return false;
    return true;
}

static void ParsePreviewMargins(const std::wstring& text,
                                unsigned int& l,unsigned int& r,
                                unsigned int& t,unsigned int& b)
{
    l=r=t=b=0;
    unsigned int ll=0,rr=0,tt=0,bb=0;
    if(swscanf(text.c_str(),L"%u , %u , %u , %u",&ll,&rr,&tt,&bb)==4 ||
       swscanf(text.c_str(),L"%u,%u,%u,%u",&ll,&rr,&tt,&bb)==4)
    {l=ll;r=rr;t=tt;b=bb;}
}

static void DrawDibSlice(HDC dc,const std::vector<BYTE>& dib,const BitmapInfoSummary& bi,
                         const RECT& dst,int sx,int sw)
{
    if(sw<=0 || dst.right<=dst.left || dst.bottom<=dst.top) return;
    const BYTE* bits=&dib[0]+bi.pixelOffset;
    const BITMAPINFO* bmi=reinterpret_cast<const BITMAPINFO*>(&dib[0]);
    LONG sh=bi.height<0?-bi.height:bi.height;
    SetStretchBltMode(dc,COLORONCOLOR);
    StretchDIBits(dc,dst.left,dst.top,dst.right-dst.left,dst.bottom-dst.top,
                  sx,0,sw,sh,bits,bmi,DIB_RGB_COLORS,SRCCOPY);
}

static bool DrawRoleAssetStretched(HDC dc,const wchar_t* role,const RECT& dst)
{
    std::vector<BYTE> dib;BitmapInfoSummary bi;ThemeRoleStateInfo si;
    if(!GetRoleStatePreview(role,dib,bi,si)) return false;

    LONG sw=bi.width;
    if(sw<=0) return false;

    unsigned int l=0,r=0,t=0,b=0;
    ParsePreviewMargins(si.sizingMargins,l,r,t,b);

    ThemeRoleColor* edit=FindEdit(role);
    if(edit && edit->gradient && _wcsicmp(si.sizingType.c_str(),L"stretch")==0)
    {
        // Match generator's effective gradient margins.
        if(l>6) l=6;
        if(r>6) r=6;
    }

    if(_wcsicmp(si.sizingType.c_str(),L"stretch")!=0 || l+r+2u>=(unsigned int)sw)
    {
        DrawDibSlice(dc,dib,bi,dst,0,sw);
        return true;
    }

    int dw=dst.right-dst.left;
    int dl=(int)l, dr=(int)r;
    if(dl+dr>dw){dl=dr=0;}

    RECT left={dst.left,dst.top,dst.left+dl,dst.bottom};
    RECT center={dst.left+dl,dst.top,dst.right-dr,dst.bottom};
    RECT right={dst.right-dr,dst.top,dst.right,dst.bottom};

    if(dl) DrawDibSlice(dc,dib,bi,left,0,(int)l);
    DrawDibSlice(dc,dib,bi,center,(int)l,(int)sw-(int)l-(int)r);
    if(dr) DrawDibSlice(dc,dib,bi,right,(int)sw-(int)r,(int)r);
    return true;
}

static COLORREF PreviewClassicColor(const wchar_t* key,COLORREF fallback)
{
    for(size_t i=0;i<gClassicColors.size();++i)
        if(_wcsicmp(gClassicColors[i].key.c_str(),key)==0)
            return RGB(gClassicColors[i].r,gClassicColors[i].g,gClassicColors[i].b);
    return fallback;
}

static ThemeRoleColor* PreviewRole(const wchar_t* role)
{
    std::map<std::wstring,ThemeRoleColor>::iterator it=gEdits.find(role);
    return it==gEdits.end()?NULL:&it->second;
}

static void FillGradientRectSimple(HDC dc,const RECT& rc,COLORREF a,COLORREF b)
{
    int width=rc.right-rc.left;
    if(width<=0) return;
    for(int x=0;x<width;++x)
    {
        double t=width>1?(double)x/(double)(width-1):0.0;
        BYTE r=(BYTE)(GetRValue(a)+(GetRValue(b)-GetRValue(a))*t);
        BYTE g=(BYTE)(GetGValue(a)+(GetGValue(b)-GetGValue(a))*t);
        BYTE bl=(BYTE)(GetBValue(a)+(GetBValue(b)-GetBValue(a))*t);
        HPEN p=CreatePen(PS_SOLID,1,RGB(r,g,bl));
        HPEN old=(HPEN)SelectObject(dc,p);
        MoveToEx(dc,rc.left+x,rc.top,NULL);
        LineTo(dc,rc.left+x,rc.bottom);
        SelectObject(dc,old);
        DeleteObject(p);
    }
}

static void PreviewRoleFill(HDC dc,const RECT& rc,const wchar_t* role,
                            COLORREF fallback1,COLORREF fallback2)
{
    ThemeRoleColor* e=PreviewRole(role);
    if(e)
    {
        COLORREF a=RGB(e->r,e->g,e->b);
        COLORREF b=e->gradient?RGB(e->r2,e->g2,e->b2):a;
        FillGradientRectSimple(dc,rc,a,b);
    }
    else
        FillGradientRectSimple(dc,rc,fallback1,fallback2);
}


static COLORREF PreviewRoleColor(const wchar_t* role,COLORREF fallback)
{
    ThemeRoleColor* e=PreviewRole(role);
    if(e) return RGB(e->r,e->g,e->b);
    return fallback;
}

static void DrawPreviewBorder(HDC dc,const RECT& rc,const wchar_t* role,COLORREF fallback)
{
    COLORREF c=PreviewRoleColor(role,fallback);
    HPEN pen=CreatePen(PS_SOLID,2,c);
    HPEN old=(HPEN)SelectObject(dc,pen);
    HBRUSH oldBrush=(HBRUSH)SelectObject(dc,GetStockObject(NULL_BRUSH));
    Rectangle(dc,rc.left,rc.top,rc.right,rc.bottom);
    SelectObject(dc,oldBrush);
    SelectObject(dc,old);
    DeleteObject(pen);
}

static bool DrawPreviewRoleResource(HDC dc,
                                    const wchar_t* role,
                                    const wchar_t* resourceToken,
                                    const RECT& dst)
{
    if(!gSourceCache.ready) return false;

    std::vector<ThemeRoleStateInfo> states;
    std::wstring err;
    if(!ThemeGenerator::EnumerateRoleStates(gSourceCache,role,states,err))
        return false;

    unsigned int ordinal=(unsigned int)-1;
    for(size_t i=0;i<states.size();++i)
    {
        std::wstring resourceUpper=states[i].resource;
        std::wstring tokenUpper=resourceToken;
        for(size_t j=0;j<resourceUpper.size();++j)
            resourceUpper[j]=(wchar_t)towupper(resourceUpper[j]);
        for(size_t j=0;j<tokenUpper.size();++j)
            tokenUpper[j]=(wchar_t)towupper(tokenUpper[j]);

        std::wstring stateUpper=states[i].stateName;
        for(size_t j=0;j<stateUpper.size();++j)
            stateUpper[j]=(wchar_t)towupper(stateUpper[j]);

        if(resourceUpper.find(tokenUpper)!=std::wstring::npos &&
           (stateUpper.find(L"NORMAL")!=std::wstring::npos || states[i].stateIndex==0))
        {
            ordinal=(unsigned int)i;
            break;
        }
    }

    if(ordinal==(unsigned int)-1)
        return false;

    ThemeRoleColor* edit=FindEdit(role);
    std::vector<BYTE> dib;
    BitmapInfoSummary bi;
    ThemeRoleStateInfo si;

    if(!ThemeGenerator::BuildRoleStatePreviewCached(gSourceCache,role,ordinal,edit,
                                                    false,dib,si,err))
        return false;
    if(!BitmapEngine::ParseDib(dib,bi,err))
        return false;

    DrawDibSlice(dc,dib,bi,dst,0,bi.width);
    return true;
}

static void DrawFallbackCaptionGlyph(HDC dc,const RECT& b,int which)
{
    FrameRect(dc,&b,(HBRUSH)GetStockObject(WHITE_BRUSH));

    HPEN p=CreatePen(PS_SOLID,1,RGB(255,255,255));
    HPEN old=(HPEN)SelectObject(dc,p);

    if(which==0) // close
    {
        MoveToEx(dc,b.left+5,b.top+4,NULL);
        LineTo(dc,b.right-4,b.bottom-4);
        MoveToEx(dc,b.right-5,b.top+4,NULL);
        LineTo(dc,b.left+4,b.bottom-4);
    }
    else if(which==1) // maximize
    {
        Rectangle(dc,b.left+5,b.top+4,b.right-4,b.bottom-4);
    }
    else // minimize
    {
        MoveToEx(dc,b.left+5,b.bottom-5,NULL);
        LineTo(dc,b.right-4,b.bottom-5);
    }

    SelectObject(dc,old);
    DeleteObject(p);
}

static void DrawPreviewCaptionButtons(HDC dc,const RECT& cap,bool active)
{
    const wchar_t* role=active?L"Window.CaptionButtons.Active":L"Window.CaptionButtons.Inactive";

    const int bw=21;
    const int bh=(cap.bottom-cap.top)-5;
    const int gap=1;
    int right=cap.right-3;

    // XP caption order, right-to-left: Close, Maximize, Minimize.
    const wchar_t* tokens[3]={L"CLOSEBUTTON",L"MAXBUTTON",L"MINBUTTON"};

    for(int which=0;which<3;++which)
    {
        RECT b={right-bw,cap.top+2,right,cap.top+2+bh};

        if(!DrawPreviewRoleResource(dc,role,tokens[which],b))
        {
            COLORREF face=PreviewRoleColor(role,
                active?PreviewClassicColor(L"ActiveCaption",RGB(0,84,227)):
                       PreviewClassicColor(L"InactiveCaption",RGB(122,150,223)));
            HBRUSH br=CreateSolidBrush(face);
            FillRect(dc,&b,br);
            DeleteObject(br);
            DrawFallbackCaptionGlyph(dc,b,which);
        }

        right=b.left-gap;
    }
}

static void DrawPreviewMenuBar(HDC dc,const RECT& win,int top)
{
    RECT menu={win.left+2,top,win.right-2,top+20};
    HBRUSH mb=CreateSolidBrush(PreviewClassicColor(L"MenuBar",
                  PreviewClassicColor(L"Menu",RGB(236,233,216))));
    FillRect(dc,&menu,mb);
    DeleteObject(mb);

    SetBkMode(dc,TRANSPARENT);
    SetTextColor(dc,PreviewClassicColor(L"MenuText",RGB(0,0,0)));
    RECT t=menu;
    t.left+=7;
    DrawTextW(dc,L"File     Edit     View     Help",-1,&t,
              DT_LEFT|DT_VCENTER|DT_SINGLELINE);

    HPEN p=CreatePen(PS_SOLID,1,PreviewClassicColor(L"BtnShadow",RGB(172,168,153)));
    HPEN old=(HPEN)SelectObject(dc,p);
    MoveToEx(dc,menu.left,menu.bottom-1,NULL);
    LineTo(dc,menu.right,menu.bottom-1);
    SelectObject(dc,old);
    DeleteObject(p);
}

static void PaintWholeThemePreview(HDC dc)
{
    RECT box=THEME_PREVIEW_RECT;
    HBRUSH frame=CreateSolidBrush(GetSysColor(COLOR_BTNFACE));
    FillRect(dc,&box,frame);DeleteObject(frame);
    FrameRect(dc,&box,(HBRUSH)GetStockObject(BLACK_BRUSH));

    RECT inner=box;InflateRect(&inner,-6,-6);

    COLORREF desktop=PreviewClassicColor(L"Background",RGB(58,110,165));
    HBRUSH db=CreateSolidBrush(desktop);FillRect(dc,&inner,db);DeleteObject(db);

    // Inactive window.
    RECT back={inner.left+20,inner.top+16,inner.right-55,inner.bottom-55};
    HBRUSH wb=CreateSolidBrush(PreviewClassicColor(L"Window",RGB(255,255,255)));
    FillRect(dc,&back,wb);DeleteObject(wb);
    DrawPreviewBorder(dc,back,L"Window.InactiveFrame",
                      PreviewClassicColor(L"InactiveCaption",RGB(122,150,223)));

    RECT backCap={back.left+2,back.top+2,back.right-2,back.top+27};
    if(!DrawRoleAssetStretched(dc,L"Window.InactiveCaption",backCap))
        PreviewRoleFill(dc,backCap,L"Window.InactiveCaption",
                        PreviewClassicColor(L"InactiveCaption",RGB(122,150,223)),
                        PreviewClassicColor(L"GradientInactiveCaption",RGB(192,192,192)));

    SetBkMode(dc,TRANSPARENT);
    SetTextColor(dc,PreviewClassicColor(L"InactiveCaptionText",RGB(216,228,248)));
    RECT bt=backCap;bt.left+=8;bt.right-=70;
    DrawTextW(dc,L"Inactive Window",-1,&bt,DT_LEFT|DT_VCENTER|DT_SINGLELINE);
    DrawPreviewCaptionButtons(dc,backCap,false);
    DrawPreviewMenuBar(dc,back,backCap.bottom);

    // Active window.
    RECT win={inner.left+62,inner.top+52,inner.right-20,inner.bottom-30};
    HBRUSH ww=CreateSolidBrush(PreviewClassicColor(L"Window",RGB(255,255,255)));
    FillRect(dc,&win,ww);DeleteObject(ww);
    DrawPreviewBorder(dc,win,L"Window.ActiveFrame",
                      PreviewClassicColor(L"ActiveCaption",RGB(0,84,227)));

    RECT cap={win.left+2,win.top+2,win.right-2,win.top+30};
    if(!DrawRoleAssetStretched(dc,L"Window.ActiveCaption",cap))
        PreviewRoleFill(dc,cap,L"Window.ActiveCaption",
                        PreviewClassicColor(L"ActiveCaption",RGB(0,84,227)),
                        PreviewClassicColor(L"GradientActiveCaption",RGB(61,149,255)));

    SetTextColor(dc,PreviewClassicColor(L"CaptionText",RGB(255,255,255)));
    RECT ct=cap;ct.left+=9;ct.right-=70;
    DrawTextW(dc,L"Active Window",-1,&ct,DT_LEFT|DT_VCENTER|DT_SINGLELINE);
    DrawPreviewCaptionButtons(dc,cap,true);

    DrawPreviewMenuBar(dc,win,cap.bottom);

    RECT body={win.left+2,cap.bottom+20,win.right-2,win.bottom-2};
    HBRUSH bodyBrush=CreateSolidBrush(PreviewClassicColor(L"Window",RGB(255,255,255)));
    FillRect(dc,&body,bodyBrush);DeleteObject(bodyBrush);

    SetTextColor(dc,RGB(0,0,0));
    RECT wt={body.left+7,body.top+5,body.right-8,body.top+23};
    DrawTextW(dc,L"Window Text",-1,&wt,DT_LEFT|DT_VCENTER|DT_SINGLELINE);

    // Message box.
    RECT msg={win.left+72,win.top+92,win.left+265,win.top+168};
    HBRUSH mb=CreateSolidBrush(PreviewClassicColor(L"Btnface",RGB(236,233,216)));
    FillRect(dc,&msg,mb);DeleteObject(mb);
    DrawPreviewBorder(dc,msg,L"Window.ActiveFrame",
                      PreviewClassicColor(L"ActiveCaption",RGB(0,84,227)));

    RECT mcap={msg.left+2,msg.top+2,msg.right-2,msg.top+24};
    if(!DrawRoleAssetStretched(dc,L"Window.ActiveCaption",mcap))
        PreviewRoleFill(dc,mcap,L"Window.ActiveCaption",
                        PreviewClassicColor(L"ActiveCaption",RGB(0,84,227)),
                        PreviewClassicColor(L"GradientActiveCaption",RGB(61,149,255)));

    SetTextColor(dc,PreviewClassicColor(L"CaptionText",RGB(255,255,255)));
    RECT mt=mcap;mt.left+=7;mt.right-=26;
    DrawTextW(dc,L"Message Box",-1,&mt,DT_LEFT|DT_VCENTER|DT_SINGLELINE);

    RECT cb={mcap.right-22,mcap.top+2,mcap.right-2,mcap.bottom-2};
    if(!DrawPreviewRoleResource(dc,L"Window.CaptionButtons.Active",L"CLOSEBUTTON",cb))
    {
        HBRUSH cbb=CreateSolidBrush(PreviewRoleColor(L"Window.CaptionButtons.Active",
                                                     PreviewClassicColor(L"ActiveCaption",RGB(0,84,227))));
        FillRect(dc,&cb,cbb);DeleteObject(cbb);
        DrawFallbackCaptionGlyph(dc,cb,0);
    }

    RECT button={msg.left+66,msg.top+43,msg.left+128,msg.top+65};
    HBRUSH bb=CreateSolidBrush(PreviewClassicColor(L"Btnface",RGB(236,233,216)));
    FillRect(dc,&button,bb);DeleteObject(bb);
    FrameRect(dc,&button,(HBRUSH)GetStockObject(BLACK_BRUSH));
    SetTextColor(dc,RGB(0,0,0));
    DrawTextW(dc,L"OK",-1,&button,DT_CENTER|DT_VCENTER|DT_SINGLELINE);

    // Taskbar.
    RECT task={inner.left,inner.bottom-25,inner.right,inner.bottom};
    if(!DrawRoleAssetStretched(dc,L"Taskbar.Background",task))
    {
        ThemeRoleColor* tb=PreviewRole(L"Taskbar.Background");
        COLORREF tc=tb?RGB(tb->r,tb->g,tb->b):RGB(36,94,220);
        HBRUSH tbr=CreateSolidBrush(tc);FillRect(dc,&task,tbr);DeleteObject(tbr);
    }

    RECT startBtn={task.left+4,task.top+2,task.left+72,task.bottom-2};
    HBRUSH sb=CreateSolidBrush(RGB(58,166,45));FillRect(dc,&startBtn,sb);DeleteObject(sb);
    SetTextColor(dc,RGB(255,255,255));
    DrawTextW(dc,L"start",-1,&startBtn,DT_CENTER|DT_VCENTER|DT_SINGLELINE);
}

static void PaintOnePreview(HDC dc,const RECT& box,
                            const std::vector<BYTE>& dib,
                            const BitmapInfoSummary& info,bool valid)
{
    HBRUSH bg=CreateSolidBrush(GetSysColor(COLOR_WINDOW));
    FillRect(dc,&box,bg); DeleteObject(bg);

    HBRUSH frameBrush=CreateSolidBrush(GetSysColor(COLOR_WINDOWFRAME));
    FrameRect(dc,&box,frameBrush);
    DeleteObject(frameBrush);

    RECT inner=box; InflateRect(&inner,-8,-8);
    if(!valid || dib.empty())
    {
        int oldBkMode=SetBkMode(dc,TRANSPARENT);
        COLORREF oldText=SetTextColor(dc,GetSysColor(COLOR_WINDOWTEXT));
        DrawTextW(dc,L"No preview",-1,&inner,DT_CENTER|DT_VCENTER|DT_SINGLELINE);
        SetTextColor(dc,oldText);
        SetBkMode(dc,oldBkMode);
        return;
    }

    LONG sw=info.width;
    LONG sh=info.height<0?-info.height:info.height;
    if(sw<=0 || sh<=0) return;

    int dw=inner.right-inner.left, dh=inner.bottom-inner.top;
    double sx=(double)dw/(double)sw, sy=(double)dh/(double)sh;
    double scale=sx<sy?sx:sy;
    if(scale>8.0) scale=8.0;

    int rw=(int)(sw*scale), rh=(int)(sh*scale);
    int dx=inner.left+(dw-rw)/2, dy=inner.top+(dh-rh)/2;

    const BYTE* bits=&dib[0]+info.pixelOffset;
    const BITMAPINFO* bmi=reinterpret_cast<const BITMAPINFO*>(&dib[0]);
    SetStretchBltMode(dc,COLORONCOLOR);
    StretchDIBits(dc,dx,dy,rw,rh,0,0,sw,sh,bits,bmi,DIB_RGB_COLORS,SRCCOPY);
}

static void PaintPreview(HDC dc)
{
    PaintOnePreview(dc,ORIGINAL_RECT,gOriginalPreviewDib,gOriginalPreviewInfo,gOriginalPreviewValid);
    PaintOnePreview(dc,EDITED_RECT,gEditedPreviewDib,gEditedPreviewInfo,gEditedPreviewValid);
    PaintWholeThemePreview(dc);
}


static LRESULT CALLBACK WndProc(HWND h,UINT msg,WPARAM wp,LPARAM lp)
{
    switch(msg)
    {
    case WM_CREATE:
        {
            gWnd=h;
            HFONT f=(HFONT)GetStockObject(DEFAULT_GUI_FONT);

            // Brand image: the same fairy artwork is also compiled as the application icon.
            HWND brand=CreateWindowW(L"STATIC",L"",WS_CHILD|WS_VISIBLE|SS_ICON|SS_CENTERIMAGE,
                                     16,14,110,100,h,(HMENU)(INT_PTR)IDC_BRAND_IMAGE,gInst,NULL);
            HICON brandIcon=(HICON)LoadImageW(gInst,MAKEINTRESOURCEW(IDI_LUNATHEMER),
                                                IMAGE_ICON,96,96,LR_DEFAULTCOLOR);
            if(!brandIcon)
                brandIcon=(HICON)LoadImageW(gInst,MAKEINTRESOURCEW(IDI_LUNATHEMER),
                                            IMAGE_ICON,48,48,LR_DEFAULTCOLOR);
            if(brandIcon)
                SendMessageW(brand,STM_SETIMAGE,IMAGE_ICON,(LPARAM)brandIcon);

            MakeLabel(h,L"Source Luna.msstyles:",140,18,138,20);
            MakeEdit(h,IDC_SOURCE,282,14,778,24,L"");
            MakeButton(h,L"Browse",IDC_SOURCE_BROWSE,1071,13,178,26);

            MakeLabel(h,L"Color Scheme:",140,55,138,20);
            HWND cb=CreateWindowW(L"COMBOBOX",L"",
                                  WS_CHILD|WS_VISIBLE|WS_TABSTOP|CBS_DROPDOWNLIST,
                                  282,51,400,200,h,(HMENU)(INT_PTR)IDC_DEF,gInst,NULL);
            SendMessageW(cb,CB_ADDSTRING,0,(LPARAM)L"NORMALBLUE_INI");
            SendMessageW(cb,CB_ADDSTRING,0,(LPARAM)L"NORMALHOMESTEAD_INI");
            SendMessageW(cb,CB_ADDSTRING,0,(LPARAM)L"NORMALMETALLIC_INI");
            SendMessageW(cb,CB_SETCURSEL,0,0);
            MakeButton(h,L"Load Semantic Roles",IDC_LOAD_ROLES,692,50,178,26);
            MakeButton(h,L"Load Preset",IDC_LOAD_PRESET,882,50,178,26);
            MakeButton(h,L"Save Preset",IDC_SAVE_PRESET,1071,50,178,26);

            MakeLabel(h,L"Output .msstyles:",140,93,138,20);
            MakeEdit(h,IDC_OUTPUT,282,89,778,24,L"");
            MakeButton(h,L"Set Target File",IDC_OUTPUT_BROWSE,1071,88,178,26);

            MakeButton(h,L"Generate Theme",IDC_GENERATE,1071,120,178,30);

            MakeLabel(h,L"Semantic Roles:   (* = edited)",16,126,210,20);
            MakeLabel(h,L"Filter:",230,126,45,20);
            HWND fc=CreateWindowW(L"COMBOBOX",L"",WS_CHILD|WS_VISIBLE|WS_TABSTOP|CBS_DROPDOWNLIST,
                                  275,122,91,180,h,(HMENU)(INT_PTR)IDC_FILTER,gInst,NULL);
            SendMessageW(fc,CB_ADDSTRING,0,(LPARAM)L"None"); SendMessageW(fc,CB_SETCURSEL,0,0);
            CreateWindowExW(WS_EX_CLIENTEDGE,L"LISTBOX",L"",
                            WS_CHILD|WS_VISIBLE|WS_TABSTOP|LBS_NOTIFY|WS_VSCROLL,
                            16,148,350,ORIGINAL_RECT.bottom-142,h,(HMENU)(INT_PTR)IDC_ROLE_LIST,gInst,NULL);

            MakeButton(h,L"Set Color",IDC_COLOR,16,482,100,30);
            MakeButton(h,L"Set Gradient",IDC_GRADIENT,121,482,108,30);
            MakeButton(h,L"Reset",IDC_RESET_ROLE,234,482,80,30);
            MakeButton(h,L"Color All",IDC_COLOR_ALL,319,482,90,30);
            MakeButton(h,L"Reset All",IDC_RESET_ALL,414,482,72,30);
            MakeButton(h,L"Advanced",IDC_CLASSIC_COLORS,491,482,106,30);
            MakeButton(h,L"Save BMP",IDC_EXPORT_BITMAP,602,482,92,30);
            MakeButton(h,L"Replace BMP",IDC_REPLACE_BITMAP,699,482,106,30);
            MakeButton(h,L"Set Dark Mode",IDC_DARK_PREFERENCE,825,482,165,30);
            MakeButton(h,L"Make Dark Theme",IDC_DARK_PRESET,1000,482,140,30);
            MakeButton(h,L"Help",IDC_APP_HELP,1149,482,100,30);

            MakeLabel(h,L"Selected Role Preview:",395,126,183,20);
            CreateWindowW(L"STATIC",L"- of -",
                          WS_CHILD|WS_VISIBLE|SS_CENTER|SS_CENTERIMAGE,
                          582,122,62,27,h,(HMENU)(INT_PTR)IDC_STATE_COUNTER,gInst,NULL);
            MakeButton(h,L"Previous",IDC_STATE_PREV,650,122,72,27);
            MakeButton(h,L"Next",IDC_STATE_NEXT,728,122,77,27);
            CreateWindowExW(WS_EX_CLIENTEDGE,L"STATIC",L"No role selected.",
                            WS_CHILD|WS_VISIBLE|SS_LEFT,
                            395,148,410,72,h,(HMENU)(INT_PTR)IDC_ROLE_INFO,gInst,NULL);
            MakeLabel(h,L"Original Source:",395,225,150,18);
            MakeLabel(h,L"Edited / Working:",605,225,150,18);
            MakeLabel(h,L"Live Luna Theme Preview:",825,126,220,20);

            CreateWindowExW(WS_EX_CLIENTEDGE,L"STATIC",
                L"Ready. Choose a source, load semantic roles, then edit colors. The source file is never modified in place.",
                WS_CHILD|WS_VISIBLE|SS_LEFT,
                16,520,1233,46,h,(HMENU)(INT_PTR)IDC_STATUS,gInst,NULL);

            HWND child=GetWindow(h,GW_CHILD);
            while(child)
            {
                SendMessageW(child,WM_SETFONT,(WPARAM)f,TRUE);
                child=GetWindow(child,GW_HWNDNEXT);
            }

            std::wstring systemLuna=DefaultSystemLuna();
            if(!systemLuna.empty())
            {
                SetWindowTextW(GetDlgItem(h,IDC_SOURCE),systemLuna.c_str());
                SetStatus(L"System Luna.msstyles detected. Click Load semantic roles.");
            }
        }
        return 0;

    case WM_COMMAND:
        if(HIWORD(wp)==CBN_SELCHANGE && LOWORD(wp)==IDC_DEF)
        {
            gRoles.clear();
            gEdits.clear();
            gSourceCache=ThemeSourceCache();
            RefreshRoleList(-1);
            ClearPreview();
            SetStatus(L"Color scheme changed. Click Load semantic roles.");
            return 0;
        }

        if(HIWORD(wp)==LBN_SELCHANGE && LOWORD(wp)==IDC_ROLE_LIST)
        {
            gStateOrdinal=0;
            UpdateSelectedRolePreview();
            return 0;
        }

        if(HIWORD(wp)==CBN_SELCHANGE && LOWORD(wp)==IDC_FILTER)
        {
            int fs=(int)SendMessageW(GetDlgItem(gWnd,IDC_FILTER),CB_GETCURSEL,0,0);
            wchar_t buf[128]={0};
            if(fs!=CB_ERR) SendMessageW(GetDlgItem(gWnd,IDC_FILTER),CB_GETLBTEXT,fs,(LPARAM)buf);
            gRoleFilter=buf[0]?buf:L"None";
            gStateOrdinal=0;
            RefreshRoleList(-1);
            UpdateSelectedRolePreview();
            return 0;
        }

        switch(LOWORD(wp))
        {
        case IDC_SOURCE_BROWSE:
            {
                std::wstring p;
                if(PickOpen(h,L"Visual styles (*.msstyles)\0*.msstyles\0All files\0*.*\0",p))
                {
                    SetWindowTextW(GetDlgItem(h,IDC_SOURCE),p.c_str());
                    gRoles.clear(); gEdits.clear(); gSourceCache=ThemeSourceCache();
                    RefreshRoleList(-1); ClearPreview();
                    SetStatus(L"Source selected. Click Load semantic roles.");
                }
            }
            return 0;

        case IDC_OUTPUT_BROWSE:
            {
                std::wstring p;
                if(PickSave(h,L"Visual styles (*.msstyles)\0*.msstyles\0All files\0*.*\0",
                            L"msstyles",p))
                    SetWindowTextW(GetDlgItem(h,IDC_OUTPUT),p.c_str());
            }
            return 0;

        case IDC_LOAD_ROLES: LoadRoles(); return 0;
        case IDC_COLOR: ChangeRoleColor(); return 0;
        case IDC_GRADIENT: ChangeRoleGradient(); return 0;
        case IDC_RESET_ROLE: ResetRole(); return 0;
        case IDC_COLOR_ALL: ChangeAllColors(); return 0;
        case IDC_RESET_ALL: ResetAllColors(); return 0;
        case IDC_CLASSIC_COLORS: OpenClassicColors(); return 0;
        case IDC_EXPORT_BITMAP: ExportSelectedBitmap(); return 0;
        case IDC_REPLACE_BITMAP: ReplaceSelectedBitmap(); return 0;
        case IDC_STATE_PREV: PreviousState(); return 0;
        case IDC_STATE_NEXT: NextState(); return 0;
        case IDC_LOAD_PRESET: LoadPreset(); return 0;
        case IDC_SAVE_PRESET: SavePreset(); return 0;
        case IDC_GENERATE: GenerateTheme(); return 0;
        case IDC_DARK_PRESET: ApplyDarkControlsPreset(); return 0;
        case IDC_DARK_PREFERENCE: OpenDarkPreferenceWindow(); return 0;
        case IDC_APP_HELP: OpenHelpWindow(); return 0;
        }
        break;

    case WM_PAINT:
        {
            PAINTSTRUCT ps;
            HDC dc=BeginPaint(h,&ps);
            PaintPreview(dc);
            EndPaint(h,&ps);
        }
        return 0;

    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }

    return DefWindowProcW(h,msg,wp,lp);
}

int WINAPI wWinMain(HINSTANCE hi,HINSTANCE,LPWSTR,int show)
{
    gInst=hi;

    WNDCLASSEXW wc={0};
    wc.cbSize=sizeof(wc);
    wc.hInstance=hi;
    wc.lpfnWndProc=WndProc;
    wc.lpszClassName=L"LunaXPStudioWindow";
    wc.hCursor=LoadCursor(NULL,IDC_ARROW);
    wc.hIcon=LoadIconW(hi,MAKEINTRESOURCEW(IDI_LUNATHEMER));
    wc.hIconSm=(HICON)LoadImageW(hi,MAKEINTRESOURCEW(IDI_LUNATHEMER),IMAGE_ICON,16,16,LR_DEFAULTCOLOR);
    wc.hbrBackground=(HBRUSH)(COLOR_BTNFACE+1);

    if(!RegisterClassExW(&wc)) return 2;

    HWND wnd=CreateWindowExW(0,wc.lpszClassName,L"LunaXP Studio 1.00",
                             WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU|WS_MINIMIZEBOX,
                             CW_USEDEFAULT,CW_USEDEFAULT,1265,614,
                             NULL,NULL,hi,NULL);
    if(!wnd) return 3;

    gWnd=wnd;
    ShowWindow(wnd,show);
    UpdateWindow(wnd);

    MSG msg;
    while(GetMessageW(&msg,NULL,0,0)>0)
    {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return (int)msg.wParam;
}
