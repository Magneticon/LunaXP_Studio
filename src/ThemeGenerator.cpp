#define _CRT_SECURE_NO_WARNINGS
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <string>
#include <vector>
#include <map>
#include <set>
#include <sstream>
#include <cwctype>
#include <algorithm>

#include "ThemeGenerator.h"
#include "ResourceProbe.h"
#include "LunaIni.h"
#include "LunaMapper.h"
#include "LunaSchema.h"
#include "BitmapEngine.h"
#include "ResourceUpdater.h"


struct CachedThemeSource
{
    std::wstring sourcePath;
    std::wstring textFileName;
    std::vector<ImageReference> refs;
    std::vector<SchemaTarget> schema;
    std::vector<ResourceEntry> entries;
    PeFileInfo pe;
    bool valid;
    CachedThemeSource() : valid(false) {}
};

static CachedThemeSource gCachedSource;
static std::map<std::wstring,std::vector<BYTE> > gBitmapCache;
static std::map<std::wstring,std::vector<BYTE> > gOriginalBitmapCache;
static std::set<std::wstring> gBitmapOverrides;

static void ClearGeneratorCaches()
{
    gCachedSource=CachedThemeSource();
    gBitmapCache.clear();
    gOriginalBitmapCache.clear();
    gBitmapOverrides.clear();
}


static std::wstring TrimWsLocal(const std::wstring& s)
{
    size_t begin=0;
    while(begin<s.size() &&
          (s[begin]==L' ' || s[begin]==L'\t' || s[begin]==L'\r' || s[begin]==L'\n'))
        ++begin;

    size_t end=s.size();
    while(end>begin &&
          (s[end-1]==L' ' || s[end-1]==L'\t' || s[end-1]==L'\r' || s[end-1]==L'\n'))
        --end;

    return s.substr(begin,end-begin);
}

static bool EqualsNoCaseLocal(const std::wstring& a,const std::wstring& b)
{
    if(a.size()!=b.size()) return false;
    for(size_t i=0;i<a.size();++i)
        if(towupper(a[i])!=towupper(b[i])) return false;
    return true;
}

static std::wstring TrimWs(const std::wstring& value)
{
    size_t a=0,b=value.size();
    while(a<b && iswspace(value[a])) ++a;
    while(b>a && iswspace(value[b-1])) --b;
    return value.substr(a,b-a);
}

static bool ReadTextFilePortable(const std::wstring& path,std::wstring& text,std::wstring& errorText)
{
    HANDLE h=CreateFileW(path.c_str(),GENERIC_READ,FILE_SHARE_READ,NULL,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,NULL);
    if(h==INVALID_HANDLE_VALUE){errorText=L"Could not open preset file.";return false;}

    DWORD size=GetFileSize(h,NULL);
    if(size==INVALID_FILE_SIZE){CloseHandle(h);errorText=L"Could not read preset size.";return false;}

    std::vector<BYTE> bytes(size);
    DWORD got=0;
    bool ok=size==0 || ReadFile(h,&bytes[0],size,&got,NULL)!=FALSE;
    CloseHandle(h);
    if(!ok || got!=size){errorText=L"Could not read preset file.";return false;}

    if(size>=2 && bytes[0]==0xFF && bytes[1]==0xFE)
    {
        size_t chars=(size-2)/2;
        text.assign(chars,L'\0');
        for(size_t i=0;i<chars;++i)
            text[i]=(wchar_t)(bytes[2+i*2] | ((WORD)bytes[3+i*2]<<8));
        return true;
    }

    int n=MultiByteToWideChar(CP_ACP,0,
        size?reinterpret_cast<LPCSTR>(&bytes[0]):"",
        (int)size,NULL,0);
    text.assign(n,L'\0');
    if(n)
        MultiByteToWideChar(CP_ACP,0,reinterpret_cast<LPCSTR>(&bytes[0]),(int)size,&text[0],n);
    return true;
}

static bool LoadLunaMap(const wchar_t* path,const wchar_t* textFileName,
                        LunaIni& ini,std::vector<ImageReference>& refs,std::wstring& errorText)
{
    ResourceProbe probe;
    std::vector<ResourceEntry> entries;
    PeFileInfo info;

    if(!probe.Scan(path,entries,info,errorText))
        return false;

    const ResourceEntry* iniResource=NULL;
    for(size_t i=0;i<entries.size();++i)
    {
        if(EqualsNoCaseLocal(entries[i].type,L"TEXTFILE") &&
           EqualsNoCaseLocal(entries[i].name,textFileName))
        {
            iniResource=&entries[i];
            break;
        }
    }

    if(!iniResource)
    {
        errorText=L"TEXTFILE resource not found: ";
        errorText+=textFileName;
        return false;
    }

    std::vector<BYTE> bytes;
    if(!probe.GetResourceData(*iniResource,bytes,errorText))
        return false;
    if(!ini.ParseUtf16Le(bytes,errorText))
        return false;

    LunaMapper::BuildImageMap(ini,entries,refs);
    return true;
}

static bool BuildCanonicalSchema(const wchar_t* path,const wchar_t* textFileName,
                                 std::vector<ImageReference>& refs,
                                 std::vector<SchemaTarget>& schema,
                                 std::wstring& errorText)
{
    LunaIni ini;
    if(!LoadLunaMap(path,textFileName,ini,refs,errorText))
        return false;

    std::vector<StateReference> states;
    LunaMapper::BuildStateMap(refs,states);
    LunaSchema::Build(refs,states,schema);
    return true;
}

static const ResourceEntry* FindResourceByName(const std::vector<ResourceEntry>& entries,
                                                const std::wstring& name)
{
    for(size_t i=0;i<entries.size();++i)
        if(EqualsNoCaseLocal(entries[i].type,L"BITMAP") &&
           EqualsNoCaseLocal(entries[i].name,name))
            return &entries[i];
    return NULL;
}



static void LsParseSizingMargins(const std::wstring& text,
                                 unsigned int& left,unsigned int& right,
                                 unsigned int& top,unsigned int& bottom)
{
    left=right=top=bottom=0;
    unsigned int l=0,r=0,t=0,b=0;
    if(swscanf(text.c_str(),L"%u , %u , %u , %u",&l,&r,&t,&b)==4 ||
       swscanf(text.c_str(),L"%u,%u,%u,%u",&l,&r,&t,&b)==4)
    {
        left=l;right=r;top=t;bottom=b;
    }
}

static void LsEffectiveGradientMargins(const SchemaTarget& t,
                                       unsigned int& left,unsigned int& right,
                                       unsigned int& top,unsigned int& bottom)
{
    LsParseSizingMargins(t.sizingMargins,left,right,top,bottom);

    // Preserve enough fixed edge artwork for Luna's rounded/bevelled caps,
    // but guarantee a useful center band for a real two-color gradient.
    if(_wcsicmp(t.sizingType.c_str(),L"stretch")==0)
    {
        unsigned int cellWidth=0;
        std::map<std::wstring,std::vector<BYTE> >::const_iterator it=
            gBitmapCache.find(t.resolvedResource);
        if(it!=gBitmapCache.end())
        {
            BitmapInfoSummary bi;
            std::wstring err;
            if(BitmapEngine::ParseDib(it->second,bi,err))
            {
                unsigned int count=t.imageCount?t.imageCount:1;
                bool horizontal=_wcsicmp(t.imageLayout.c_str(),L"horizontal")==0;
                cellWidth=horizontal ? (unsigned int)bi.width/count : (unsigned int)bi.width;
            }
        }

        if(left>6) left=6;
        if(right>6) right=6;

        if(cellWidth>20)
        {
            while(left+right+16>cellWidth && (left>2 || right>2))
            {
                if(left>=right && left>2) --left;
                else if(right>2) --right;
                else break;
            }
        }
    }
}



static bool ResourceNameContains(const std::wstring& name,const wchar_t* token)
{
    std::wstring a=name,b=token;
    for(size_t i=0;i<a.size();++i) a[i]=(wchar_t)towupper(a[i]);
    for(size_t i=0;i<b.size();++i) b[i]=(wchar_t)towupper(b[i]);
    return a.find(b)!=std::wstring::npos;
}


static bool ContainsNoCaseLocal(const std::wstring& value,const wchar_t* token)
{
    std::wstring a=value,b=token;
    for(size_t i=0;i<a.size();++i) a[i]=(wchar_t)towupper(a[i]);
    for(size_t i=0;i<b.size();++i) b[i]=(wchar_t)towupper(b[i]);
    return a.find(b)!=std::wstring::npos;
}

static bool IsSurfaceFillSafeResource(const std::wstring& name)
{
    // Background/face resources with no indispensable embedded foreground glyph.
    // Keep arrows, checkbox/radio marks, caption glyphs and other mask sheets out.
    static const wchar_t* safe[]={
        L"STARTPANELMFUBACKGROUND",
        L"STARTPANELPLACESBACKGROUND",
        L"STARTUSERPANEL",
        L"STARTPANELLOGOFFBACKGROUND",
        L"STARTPANELMOREPROGBACKGROUND",
        L"STARTGROUPBACKGROUND",
        L"NORMALGROUPBACKGROUND",
        L"NORMALGROUPHEAD",
        L"SPECIALGROUPBACKGROUND",
        L"SPECIALGROUPHEAD",
        L"EXPLORERBARHEADERBACKGROUND",
        L"EXPLORERBARTOOLBARBACKGROUND",
        L"BUTTON_BMP",
        L"GROUPBOX_BMP",
        L"COMBOBUTTON_BMP",
        L"TABBACKGROUND",
        L"TABPANEEDGE",
        L"TABITEM",
        L"LISTVIEWHEADERBACKGROUND",
        L"LISTVIEWHEADER_BMP",
        L"STATUSBACKGROUND",
        L"STATUSPANE",
        L"TOOLBARBACKGROUND",
        L"PROGRESSTRACK",
        L"PROGRESSCHUNK",
        L"SCROLLTHUMB",
        L"SCROLLSHAFT"
    };

    if(ContainsNoCaseLocal(name,L"GLYPH")) return false;
    if(ContainsNoCaseLocal(name,L"CHECKBOX")) return false;
    if(ContainsNoCaseLocal(name,L"RADIOBUTTON")) return false;
    if(ContainsNoCaseLocal(name,L"SCROLLARROW")) return false;
    if(ContainsNoCaseLocal(name,L"STARTBUTTON")) return false;
    if(ContainsNoCaseLocal(name,L"CAPTIONBUTTON")) return false;

    for(size_t i=0;i<sizeof(safe)/sizeof(safe[0]);++i)
        if(ContainsNoCaseLocal(name,safe[i]))
            return true;
    return false;
}

static BYTE ClampByteInt(int v)
{
    if(v<0) return 0;
    if(v>255) return 255;
    return (BYTE)v;
}

static void AdjustSurfaceFillTarget(const std::wstring& resource,
                                    BYTE inR,BYTE inG,BYTE inB,
                                    BYTE& outR,BYTE& outG,BYTE& outB)
{
    outR=inR;outG=inG;outB=inB;

    // A single semantic Scrollbar role references both shaft and thumb
    // resources. Giving every resource the exact same target makes the
    // thumb disappear into the track. Keep the shaft darker and make the
    // thumb/gripper progressively brighter.
    int delta=0;

    if(ContainsNoCaseLocal(resource,L"SCROLLTHUMBGRIPPER"))
        delta=68;
    else if(ContainsNoCaseLocal(resource,L"SCROLLTHUMB"))
        delta=34;
    else if(ContainsNoCaseLocal(resource,L"SCROLLSHAFT"))
        delta=-10;

    outR=ClampByteInt((int)inR+delta);
    outG=ClampByteInt((int)inG+delta);
    outB=ClampByteInt((int)inB+delta);
}


static bool IsCaptionGlyphResource(const std::wstring& resourceName)
{
    return ResourceNameContains(resourceName,L"GLYPH") &&
           (ResourceNameContains(resourceName,L"CLOSE") ||
            ResourceNameContains(resourceName,L"MAXIMIZE") ||
            ResourceNameContains(resourceName,L"MINIMIZE") ||
            ResourceNameContains(resourceName,L"RESTORE") ||
            ResourceNameContains(resourceName,L"HELP"));
}

static bool IsCaptionButtonBackgroundResource(const std::wstring& resourceName)
{
    return ResourceNameContains(resourceName,L"CAPTIONBUTTON_BMP") ||
           ResourceNameContains(resourceName,L"CLOSEBUTTON_BMP") ||
           ResourceNameContains(resourceName,L"SMALLCLOSEBUTTON_BMP");
}

static bool IsBulkUnsafeResource(const std::wstring& resourceName)
{
    // Bulk recolor is intentionally conservative for sprite/overlay artwork.
    // These resources frequently contain transparent/neutral state pixels that
    // should not be treated as broad theme-color surfaces.
    return ResourceNameContains(resourceName,L"GLYPH") ||
           ResourceNameContains(resourceName,L"CHECKBOX") ||
           ResourceNameContains(resourceName,L"RADIOBUTTON") ||
           ResourceNameContains(resourceName,L"TREEEXPANDCOLLAPSE") ||
           ResourceNameContains(resourceName,L"CHEVRON") ||
           ResourceNameContains(resourceName,L"GRIPPER") ||
           ResourceNameContains(resourceName,L"RESIZEGRIP") ||
           ResourceNameContains(resourceName,L"SEPARATOR") ||
           ResourceNameContains(resourceName,L"BALLOONCLOSE") ||
           ResourceNameContains(resourceName,L"PIN_BMP") ||

           // Explorer/IE toolbar button sheets were the remaining source of
           // white/block artifacts after the first bulk-safe pass.
           ResourceNameContains(resourceName,L"TOOLBARBUTTON") ||
           ResourceNameContains(resourceName,L"PERSONALBARMENU") ||
           ResourceNameContains(resourceName,L"PLACEBARBUTTONS");
}


static bool IsBulkColorSurfaceRole(const std::wstring& role)
{
    // Deliberately conservative. Only broad, visually color-bearing surfaces
    // are included in Color ALL. Sprite sheets, glyphs, masks, indicators,
    // toolbar/rebar art and other utility graphics stay stock unless the user
    // edits them individually.
    return role.find(L"Window.ActiveCaption")==0 ||
           role.find(L"Window.InactiveCaption")==0 ||
           role.find(L"Window.ActiveFrame")==0 ||
           role.find(L"Window.InactiveFrame")==0 ||
           role.find(L"Window.CaptionButtons")==0 ||

           role.find(L"Taskbar.Background")==0 ||
           role.find(L"Taskbar.NotificationArea")==0 ||
           role.find(L"Taskbar.GroupMenu.Background")==0 ||
           role.find(L"Taskbar.GroupMenu.Hover")==0 ||
           role.find(L"Taskbar.TaskButtons")==0 ||

           role.find(L"StartMenu")==0 ||

           role.find(L"Controls.Progress")==0 ||

           role.find(L"ExplorerBar")==0 ||
           role.find(L"List/Header")==0 ||
           role.find(L"Status Bar")==0;
}


static bool StateCellGeometryCompatible(const std::vector<BYTE>& dib,
                                        unsigned int stateIndex,
                                        unsigned int imageCount,
                                        const std::wstring& imageLayout)
{
    if(imageCount==0) imageCount=1;
    if(stateIndex>=imageCount) return false;

    BitmapInfoSummary info;
    std::wstring ignored;
    if(!BitmapEngine::ParseDib(dib,info,ignored))
        return false;

    size_t w=(size_t)info.width;
    size_t h=info.height<0?(size_t)-info.height:(size_t)info.height;
    bool horizontal=EqualsNoCaseLocal(imageLayout,L"horizontal");
    bool vertical=EqualsNoCaseLocal(imageLayout,L"vertical") || imageLayout.empty();
    if(!horizontal && !vertical) vertical=true;

    if(imageCount<=1) return true;
    if(horizontal) return w>=imageCount && (w%imageCount)==0;
    return h>=imageCount && (h%imageCount)==0;
}

static bool RoleLess(const ThemeRoleInfo& a,const ThemeRoleInfo& b)
{
    return _wcsicmp(a.role.c_str(),b.role.c_str())<0;
}


static bool EnsureCachedSource(const std::wstring& sourcePath,
                               const std::wstring& textFileName,
                               std::wstring& errorText)
{
    if(gCachedSource.valid &&
       _wcsicmp(gCachedSource.sourcePath.c_str(),sourcePath.c_str())==0 &&
       _wcsicmp(gCachedSource.textFileName.c_str(),textFileName.c_str())==0)
        return true;

    ClearGeneratorCaches();

    if(!BuildCanonicalSchema(sourcePath.c_str(),textFileName.c_str(),
                             gCachedSource.refs,gCachedSource.schema,errorText))
        return false;

    ResourceProbe probe;
    if(!probe.Scan(sourcePath.c_str(),gCachedSource.entries,gCachedSource.pe,errorText))
        return false;

    gCachedSource.sourcePath=sourcePath;
    gCachedSource.textFileName=textFileName;
    gCachedSource.valid=true;
    return true;
}

bool ThemeGenerator::BuildSourceCache(const std::wstring& sourcePath,
                                      const std::wstring& textFileName,
                                      ThemeSourceCache& cache,
                                      std::wstring& errorText)
{
    cache=ThemeSourceCache();
    if(!EnsureCachedSource(sourcePath,textFileName,errorText))
        return false;

    if(!EnumerateRoles(sourcePath,textFileName,cache.roles,errorText))
        return false;

    // Load every BITMAP now so all later previews are memory-only.
    gBitmapCache.clear();
    gOriginalBitmapCache.clear();
    gBitmapOverrides.clear();

    ResourceProbe probe;
    std::vector<ResourceEntry> entries;
    PeFileInfo pe;
    if(!probe.Scan(sourcePath.c_str(),entries,pe,errorText))
        return false;

    unsigned __int64 totalBytes=0;
    unsigned int totalBitmaps=0;

    for(size_t i=0;i<entries.size();++i)
    {
        if(!EqualsNoCaseLocal(entries[i].type,L"BITMAP"))
            continue;

        std::vector<BYTE> data;
        if(!probe.GetResourceData(entries[i],data,errorText))
        {
            std::wstringstream s;
            s << L"Could not preload BITMAP resource " << entries[i].name << L":\r\n" << errorText;
            errorText=s.str();
            gBitmapCache.clear();
            return false;
        }

        gBitmapCache[entries[i].name]=data;
        gOriginalBitmapCache[entries[i].name]=data;
        totalBytes+=(unsigned __int64)data.size();
        ++totalBitmaps;
    }

    cache.sourcePath=sourcePath;
    cache.textFileName=textFileName;
    cache.bitmapResourceCount=totalBitmaps;
    cache.bitmapBytesLoaded=totalBytes;
    cache.ready=true;
    return true;
}

bool ThemeGenerator::EnumerateRoles(const std::wstring& sourcePath,
                                    const std::wstring& textFileName,
                                    std::vector<ThemeRoleInfo>& roles,
                                    std::wstring& errorText)
{
    roles.clear();
    errorText.clear();

    if(!EnsureCachedSource(sourcePath,textFileName,errorText))
        return false;

    const std::vector<SchemaTarget>& schema=gCachedSource.schema;

    std::map<std::wstring,size_t> byRole;
    for(size_t i=0;i<schema.size();++i)
    {
        const SchemaTarget& t=schema[i];
        if(!t.resourceFound || t.editorRole.empty()) continue;

        size_t idx=(size_t)-1;
        std::map<std::wstring,size_t>::iterator it=byRole.find(t.editorRole);
        if(it==byRole.end())
        {
            ThemeRoleInfo info;
            info.role=t.editorRole;
            info.targetCount=1;
            info.firstResource=t.resolvedResource;
            info.firstStateName=t.stateName;
            info.firstStateIndex=t.stateIndex;
            info.imageCount=t.imageCount;
            info.imageLayout=t.imageLayout;
            roles.push_back(info);
            idx=roles.size()-1;
            byRole[t.editorRole]=idx;
        }
        else
        {
            idx=it->second;
            ++roles[idx].targetCount;
        }
    }

    std::sort(roles.begin(),roles.end(),RoleLess);
    return !roles.empty();
}


static void GatherRoleTargets(const std::wstring& role,
                              std::vector<const SchemaTarget*>& targets)
{
    targets.clear();
    std::set<std::wstring> seen;
    for(size_t i=0;i<gCachedSource.schema.size();++i)
    {
        const SchemaTarget& t=gCachedSource.schema[i];
        if(!t.resourceFound || !EqualsNoCaseLocal(t.editorRole,role))
            continue;

        std::wstringstream key;
        key<<t.resolvedResource<<L"#"<<t.stateIndex;
        if(seen.find(key.str())!=seen.end()) continue;
        seen.insert(key.str());
        targets.push_back(&t);
    }
}

bool ThemeGenerator::EnumerateRoleStates(const ThemeSourceCache& cache,
                                         const std::wstring& role,
                                         std::vector<ThemeRoleStateInfo>& states,
                                         std::wstring& errorText)
{
    states.clear(); errorText.clear();
    if(!cache.ready){errorText=L"Theme source cache is not ready.";return false;}

    std::vector<const SchemaTarget*> targets;
    GatherRoleTargets(role,targets);
    if(targets.empty()){errorText=L"Selected semantic role has no resolved state targets.";return false;}

    for(size_t i=0;i<targets.size();++i)
    {
        ThemeRoleStateInfo s;
        s.resource=targets[i]->resolvedResource;
        s.stateName=targets[i]->stateName;
        s.stateIndex=targets[i]->stateIndex;
        s.imageCount=targets[i]->imageCount;
        s.imageLayout=targets[i]->imageLayout;
        s.sizingType=targets[i]->sizingType;
        s.sizingMargins=targets[i]->sizingMargins;
        states.push_back(s);
    }
    return true;
}

bool ThemeGenerator::BuildRoleStatePreviewCached(const ThemeSourceCache& cache,
                                                 const std::wstring& role,
                                                 unsigned int stateOrdinal,
                                                 const ThemeRoleColor* color,
                                                 bool originalSource,
                                                 std::vector<BYTE>& previewDib,
                                                 ThemeRoleStateInfo& stateInfo,
                                                 std::wstring& errorText)
{
    previewDib.clear(); errorText.clear();
    if(!cache.ready){errorText=L"Theme source cache is not ready.";return false;}

    std::vector<const SchemaTarget*> targets;
    GatherRoleTargets(role,targets);
    if(targets.empty()){errorText=L"Selected semantic role has no resolved state targets.";return false;}
    if(stateOrdinal>=targets.size()) stateOrdinal=0;

    const SchemaTarget* target=targets[stateOrdinal];
    stateInfo=ThemeRoleStateInfo();
    stateInfo.resource=target->resolvedResource;
    stateInfo.stateName=target->stateName;
    stateInfo.stateIndex=target->stateIndex;
    stateInfo.imageCount=target->imageCount;
    stateInfo.imageLayout=target->imageLayout;
    stateInfo.sizingType=target->sizingType;
    stateInfo.sizingMargins=target->sizingMargins;

    const std::map<std::wstring,std::vector<BYTE> >& sourceMap=
        originalSource?gOriginalBitmapCache:gBitmapCache;
    std::map<std::wstring,std::vector<BYTE> >::const_iterator bit=
        sourceMap.find(target->resolvedResource);
    if(bit==sourceMap.end()){errorText=L"Preview BITMAP is not present in memory.";return false;}

    std::vector<BYTE> full=bit->second;
    if(color && !originalSource)
    {
        RecolorResult rr;
        if(IsCaptionGlyphResource(target->resolvedResource))
        {
            // Preserve glyph overlay.
        }
        else if(IsCaptionButtonBackgroundResource(target->resolvedResource))
        {
            if(color->gradient)
            {
                unsigned int lm=0,rm=0,tm=0,bm=0;
                LsEffectiveGradientMargins(*target,lm,rm,tm,bm);
                if(_wcsicmp(target->sizingType.c_str(),L"stretch")==0)
                {
                    if(!BitmapEngine::ExpandStretchWidth(full,target->imageCount,target->imageLayout,
                                                        lm,rm,256,errorText)) return false;
                }
                if(!BitmapEngine::RecolorTintedStateCellGradient(full,target->stateIndex,target->imageCount,
                                                                target->imageLayout,lm,rm,
                                                                color->r,color->g,color->b,
                                                                color->r2,color->g2,color->b2,rr,errorText)) return false;
            }
            else if(!BitmapEngine::RecolorTintedStateCell(full,target->stateIndex,target->imageCount,
                                                         target->imageLayout,color->r,color->g,color->b,
                                                         rr,errorText)) return false;
        }
        else
        {
            if(color->gradient)
            {
                unsigned int lm=0,rm=0,tm=0,bm=0;
                LsEffectiveGradientMargins(*target,lm,rm,tm,bm);
                if(_wcsicmp(target->sizingType.c_str(),L"stretch")==0)
                {
                    if(!BitmapEngine::ExpandStretchWidth(full,target->imageCount,target->imageLayout,
                                                        lm,rm,256,errorText)) return false;
                }
                if(!BitmapEngine::RecolorWholeStateCellGradient(full,target->stateIndex,target->imageCount,
                                                               target->imageLayout,lm,rm,
                                                               color->r,color->g,color->b,
                                                               color->r2,color->g2,color->b2,rr,errorText)) return false;
            }
            else if(color->surfaceFill)
            {
                if(IsSurfaceFillSafeResource(target->resolvedResource))
                {
                    BYTE sr=color->r,sg=color->g,sb=color->b;
                    AdjustSurfaceFillTarget(target->resolvedResource,color->r,color->g,color->b,sr,sg,sb);
                    if(!BitmapEngine::RecolorSurfaceStateCell(full,target->stateIndex,target->imageCount,
                                                              target->imageLayout,sr,sg,sb,
                                                              rr,errorText)) return false;
                }
                else
                {
                    // A surface-fill role can contain companion glyph/sprite targets.
                    // Keep those byte-for-byte stock.
                    rr.changed=false;
                }
            }
            else if(!BitmapEngine::RecolorWholeStateCell(full,target->stateIndex,target->imageCount,
                                                        target->imageLayout,color->r,color->g,color->b,
                                                        rr,errorText)) return false;
        }
    }

    return BitmapEngine::ExtractStateCell(full,target->stateIndex,target->imageCount,
                                          target->imageLayout,previewDib,errorText);
}

bool ThemeGenerator::BuildRolePreviewCached(const ThemeSourceCache& cache,
                                             const std::wstring& role,
                                             const ThemeRoleColor* color,
                                             double hueWindow,
                                             std::vector<BYTE>& previewDib,
                                             ThemeRoleInfo& roleInfo,
                                             std::wstring& errorText)
{
    if(!cache.ready)
    {
        errorText=L"Theme source cache is not ready.";
        return false;
    }
    return BuildRolePreview(cache.sourcePath,cache.textFileName,role,color,hueWindow,
                            previewDib,roleInfo,errorText);
}

bool ThemeGenerator::BuildRolePreview(const std::wstring& sourcePath,
                                      const std::wstring& textFileName,
                                      const std::wstring& role,
                                      const ThemeRoleColor* color,
                                      double hueWindow,
                                      std::vector<BYTE>& previewDib,
                                      ThemeRoleInfo& roleInfo,
                                      std::wstring& errorText)
{
    (void)hueWindow;
    previewDib.clear();
    errorText.clear();

    if(!EnsureCachedSource(sourcePath,textFileName,errorText))
        return false;

    const std::vector<SchemaTarget>& schema=gCachedSource.schema;
    const SchemaTarget* target=NULL;
    unsigned int count=0;

    for(size_t i=0;i<schema.size();++i)
    {
        if(schema[i].resourceFound && EqualsNoCaseLocal(schema[i].editorRole,role))
        {
            if(!target) target=&schema[i];
            ++count;
        }
    }
    if(!target)
    {
        errorText=L"Selected semantic role was not found in the source scheme.";
        return false;
    }

    roleInfo=ThemeRoleInfo();
    roleInfo.role=target->editorRole;
    roleInfo.targetCount=count;
    roleInfo.firstResource=target->resolvedResource;
    roleInfo.firstStateName=target->stateName;
    roleInfo.firstStateIndex=target->stateIndex;
    roleInfo.imageCount=target->imageCount;
    roleInfo.imageLayout=target->imageLayout;

    std::map<std::wstring,std::vector<BYTE> >::iterator bit=gBitmapCache.find(target->resolvedResource);
    if(bit==gBitmapCache.end())
    {
        errorText=L"Preview BITMAP is not present in the preloaded memory cache. Reload semantic roles.";
        return false;
    }

    previewDib=bit->second;

    if(color)
    {
        RecolorResult rr;

        // Caption glyph sheets are overlays. Recoloring them with the button
        // background makes the white minimize/maximize/close symbols disappear.
        if(IsCaptionGlyphResource(target->resolvedResource))
        {
            // Keep stock glyph colors.
        }
        else if(IsCaptionButtonBackgroundResource(target->resolvedResource))
        {
            if(!BitmapEngine::RecolorTintedStateCell(previewDib,target->stateIndex,target->imageCount,
                                                     target->imageLayout,color->r,color->g,color->b,
                                                     rr,errorText))
                return false;
        }
        else
        {
            if(!BitmapEngine::RecolorWholeStateCell(previewDib,target->stateIndex,target->imageCount,
                                                    target->imageLayout,color->r,color->g,color->b,
                                                    rr,errorText))
                return false;
        }
    }

    if(!BitmapEngine::ConvertTo24Bpp(previewDib,errorText))
        return false;

    return true;
}

bool ThemeGenerator::LoadPreset(const std::wstring& presetPath,
                                std::vector<ThemeRoleColor>& colors,
                                std::wstring& errorText)
{
    colors.clear();
    std::wstring text;
    if(!ReadTextFilePortable(presetPath,text,errorText))
        return false;

    bool inPalette=false;
    size_t pos=0;
    while(pos<=text.size())
    {
        size_t end=text.find_first_of(L"\r\n",pos);
        if(end==std::wstring::npos) end=text.size();
        std::wstring line=TrimWs(text.substr(pos,end-pos));

        if(end<text.size() && text[end]==L'\r' && end+1<text.size() && text[end+1]==L'\n')
            pos=end+2;
        else
            pos=end+1;

        if(line.empty() || line[0]==L';' || line[0]==L'#') continue;
        if(line[0]==L'[' && line[line.size()-1]==L']')
        {
            inPalette=EqualsNoCaseLocal(TrimWs(line.substr(1,line.size()-2)),L"Palette");
            continue;
        }
        if(!inPalette) continue;

        size_t eq=line.find(L'=');
        if(eq==std::wstring::npos) continue;

        ThemeRoleColor e;
        e.role=TrimWs(line.substr(0,eq));
        std::wstring colorText=TrimWs(line.substr(eq+1));
        if(e.role.empty() || colorText.empty()) continue;

        size_t gt=colorText.find(L'>');
        std::wstring c1=gt==std::wstring::npos?colorText:TrimWs(colorText.substr(0,gt));
        if(!BitmapEngine::ParseColor(c1,e.r,e.g,e.b,errorText))
        {
            errorText=L"Preset role '"+e.role+L"': "+errorText; return false;
        }
        if(gt!=std::wstring::npos)
        {
            std::wstring c2=TrimWs(colorText.substr(gt+1));
            if(!BitmapEngine::ParseColor(c2,e.r2,e.g2,e.b2,errorText))
            { errorText=L"Preset gradient role '"+e.role+L"': "+errorText; return false; }
            e.gradient=true;
        }
        colors.push_back(e);
    }

    if(colors.empty())
    {
        errorText=L"Preset contains no [Palette] role=color entries.";
        return false;
    }
    return true;
}

bool ThemeGenerator::SavePreset(const std::wstring& presetPath,
                                const std::vector<ThemeRoleColor>& colors,
                                std::wstring& errorText)
{
    errorText.clear();
    HANDLE h=CreateFileW(presetPath.c_str(),GENERIC_WRITE,0,NULL,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,NULL);
    if(h==INVALID_HANDLE_VALUE)
    {
        errorText=L"Could not create preset file.";
        return false;
    }

    std::ostringstream out;
    out << "; LunaXP Studio palette preset\r\n[Palette]\r\n";
    for(size_t i=0;i<colors.size();++i)
    {
        std::string role;
        int n=WideCharToMultiByte(CP_ACP,0,colors[i].role.c_str(),-1,NULL,0,NULL,NULL);
        if(n>1)
        {
            std::vector<char> tmp(n);
            WideCharToMultiByte(CP_ACP,0,colors[i].role.c_str(),-1,&tmp[0],n,NULL,NULL);
            role.assign(&tmp[0]);
        }
        char hex[32];
        if(colors[i].gradient)
            wsprintfA(hex,"#%02X%02X%02X>#%02X%02X%02X",
                      colors[i].r,colors[i].g,colors[i].b,colors[i].r2,colors[i].g2,colors[i].b2);
        else
            wsprintfA(hex,"#%02X%02X%02X",colors[i].r,colors[i].g,colors[i].b);
        out << role << "=" << hex << "\r\n";
    }

    std::string s=out.str();
    DWORD written=0;
    BOOL ok=s.empty() || WriteFile(h,s.data(),(DWORD)s.size(),&written,NULL);
    CloseHandle(h);
    if(!ok || written!=s.size())
    {
        errorText=L"Could not write preset file.";
        return false;
    }
    return true;
}


bool ThemeGenerator::ExportBitmapResource(const ThemeSourceCache& cache,
                                          const std::wstring& resourceName,
                                          const std::wstring& bmpPath,
                                          std::wstring& errorText)
{
    errorText.clear();
    if(!cache.ready)
    {
        errorText=L"Theme source cache is not ready.";
        return false;
    }

    std::map<std::wstring,std::vector<BYTE> >::const_iterator it=
        gBitmapCache.find(resourceName);
    if(it==gBitmapCache.end())
    {
        errorText=L"Selected BITMAP resource is not present in memory.";
        return false;
    }

    // Export a normalized COPY, not the original cached DIB. This prevents
    // Windows XP Paint from editing an indexed/paletted Luna bitmap through
    // its limited palette (where "black" may resolve to a dark blue entry).
    std::vector<BYTE> exportDib=it->second;
    if(!BitmapEngine::ConvertTo24Bpp(exportDib,errorText))
    {
        errorText=L"Could not convert exported bitmap to 24-bit RGB:\r\n"+errorText;
        return false;
    }

    return BitmapEngine::WriteBmpFile(bmpPath,exportDib,errorText);
}

bool ThemeGenerator::ReplaceBitmapResource(const ThemeSourceCache& cache,
                                           const std::wstring& resourceName,
                                           const std::wstring& bmpPath,
                                           std::wstring& errorText)
{
    errorText.clear();
    if(!cache.ready)
    {
        errorText=L"Theme source cache is not ready.";
        return false;
    }

    std::map<std::wstring,std::vector<BYTE> >::iterator original=
        gBitmapCache.find(resourceName);
    if(original==gBitmapCache.end())
    {
        errorText=L"Selected BITMAP resource is not present in memory.";
        return false;
    }

    std::vector<BYTE> replacement;
    if(!BitmapEngine::LoadBmpFile(bmpPath,replacement,errorText))
        return false;

    BitmapInfoSummary oldInfo,newInfo;
    if(!BitmapEngine::ParseDib(original->second,oldInfo,errorText))
        return false;
    if(!BitmapEngine::ParseDib(replacement,newInfo,errorText))
        return false;

    if(oldInfo.width!=newInfo.width || oldInfo.height!=newInfo.height)
    {
        std::wstringstream s;
        s << L"Replacement bitmap dimensions must match the Luna resource.\r\n\r\n"
          << L"Required: " << oldInfo.width << L" x "
          << (oldInfo.height<0?-oldInfo.height:oldInfo.height) << L"\r\n"
          << L"Loaded: " << newInfo.width << L" x "
          << (newInfo.height<0?-newInfo.height:newInfo.height);
        errorText=s.str();
        return false;
    }

    // Normalize external files to an XP-friendly BI_RGB 24-bpp resource DIB.
    if(!BitmapEngine::ConvertTo24Bpp(replacement,errorText))
        return false;

    gBitmapCache[resourceName]=replacement;
    gBitmapOverrides.insert(resourceName);
    return true;
}



bool ThemeGenerator::ExportBitmapStateForRole(const ThemeSourceCache& cache,
                                              const std::wstring& role,
                                              const std::wstring& bmpPath,
                                              std::wstring& errorText)
{
    errorText.clear();
    if(!cache.ready){errorText=L"Theme source cache is not ready.";return false;}

    const SchemaTarget* target=NULL;
    for(size_t i=0;i<gCachedSource.schema.size();++i)
    {
        const SchemaTarget& t=gCachedSource.schema[i];
        if(t.resourceFound && EqualsNoCaseLocal(t.editorRole,role))
        {
            target=&t;
            break;
        }
    }
    if(!target){errorText=L"Selected semantic role was not found.";return false;}

    std::map<std::wstring,std::vector<BYTE> >::const_iterator it=
        gBitmapCache.find(target->resolvedResource);
    if(it==gBitmapCache.end()){errorText=L"Selected BITMAP is not present in memory.";return false;}

    std::vector<BYTE> cell;
    if(!BitmapEngine::ExtractStateCell(it->second,target->stateIndex,target->imageCount,
                                      target->imageLayout,cell,errorText))
        return false;
    return BitmapEngine::WriteBmpFile(bmpPath,cell,errorText);
}

bool ThemeGenerator::ReplaceBitmapForRole(const ThemeSourceCache& cache,
                                          const std::wstring& role,
                                          const std::wstring& bmpPath,
                                          unsigned int& replacedResources,
                                          unsigned int& skippedResources,
                                          std::wstring& errorText)
{
    replacedResources=0;
    skippedResources=0;
    errorText.clear();

    if(!cache.ready){errorText=L"Theme source cache is not ready.";return false;}

    std::vector<BYTE> replacementCell;
    if(!BitmapEngine::LoadBmpFile(bmpPath,replacementCell,errorText)) return false;
    if(!BitmapEngine::ConvertTo24Bpp(replacementCell,errorText)) return false;

    // Apply only the logical state(s) belonging to this semantic role. This is
    // deliberately state-aware: ActiveCaption no longer overwrites the
    // InactiveCaption cell merely because both live in FRAMECAPTION_BMP.
    std::set<std::wstring> appliedTargets;
    for(size_t i=0;i<gCachedSource.schema.size();++i)
    {
        const SchemaTarget& t=gCachedSource.schema[i];
        if(!t.resourceFound || !EqualsNoCaseLocal(t.editorRole,role)) continue;

        std::wstringstream key;
        key<<t.resolvedResource<<L"#"<<t.stateIndex;
        if(appliedTargets.find(key.str())!=appliedTargets.end()) continue;
        appliedTargets.insert(key.str());

        std::map<std::wstring,std::vector<BYTE> >::iterator it=gBitmapCache.find(t.resolvedResource);
        if(it==gBitmapCache.end()){++skippedResources;continue;}

        std::vector<BYTE> trial=it->second;
        std::wstring local;
        if(!BitmapEngine::ReplaceStateCell(trial,t.stateIndex,t.imageCount,t.imageLayout,
                                           replacementCell,local))
        {
            ++skippedResources;
            continue;
        }

        it->second.swap(trial);
        gBitmapOverrides.insert(t.resolvedResource);
        ++replacedResources;
    }

    if(replacedResources==0)
    {
        errorText=L"The imported bitmap does not match any state cell used by the selected semantic role.";
        return false;
    }
    return true;
}

bool ThemeGenerator::HasBitmapOverrides()
{
    return !gBitmapOverrides.empty();
}

bool ThemeGenerator::IsBitmapOverridden(const std::wstring& resourceName)
{
    return gBitmapOverrides.find(resourceName)!=gBitmapOverrides.end();
}


static bool LsParseRgb(const std::wstring& text,BYTE& r,BYTE& g,BYTE& b)
{
    unsigned int rr=0,gg=0,bb=0;
    if(swscanf(text.c_str(),L"%u %u %u",&rr,&gg,&bb)!=3 || rr>255 || gg>255 || bb>255) return false;
    r=(BYTE)rr;g=(BYTE)gg;b=(BYTE)bb;return true;
}

static std::wstring LsDecodeUtf16(const std::vector<BYTE>& bytes)
{
    std::wstring s;
    for(size_t i=0;i+1<bytes.size();i+=2)
    {
        wchar_t ch=(wchar_t)(bytes[i]|((WORD)bytes[i+1]<<8));
        if(i==0 && ch==0xFEFF) continue;
        s.push_back(ch);
    }
    return s;
}

static std::vector<BYTE> LsEncodeUtf16(const std::wstring& s)
{
    std::vector<BYTE> out; out.reserve(s.size()*2);
    for(size_t i=0;i<s.size();++i){WORD w=(WORD)s[i];out.push_back((BYTE)w);out.push_back((BYTE)(w>>8));}
    return out;
}

static bool LsGetTextFile(const std::wstring& source,const std::wstring& name,
                          std::vector<BYTE>& raw,WORD& lang,std::wstring& errorText)
{
    ResourceProbe probe; std::vector<ResourceEntry> entries; PeFileInfo pe;
    if(!probe.Scan(source.c_str(),entries,pe,errorText)) return false;
    for(size_t i=0;i<entries.size();++i)
        if(EqualsNoCaseLocal(entries[i].type,L"TEXTFILE") && EqualsNoCaseLocal(entries[i].name,name))
        { lang=entries[i].language; return probe.GetResourceData(entries[i],raw,errorText); }
    errorText=L"TEXTFILE definition not found."; return false;
}

bool ThemeGenerator::GetClassicColors(const ThemeSourceCache& cache,
                                      std::vector<ThemeClassicColor>& colors,
                                      std::wstring& errorText)
{
    colors.clear();
    if(!cache.ready){errorText=L"Theme source cache is not ready.";return false;}

    // Only expose properties that are actually present in the selected
    // Luna [SysMetrics] section. XP's visual-style parser is strict about
    // this schema; injecting unsupported classic color names can cause the
    // generated .msstyles file to be rejected.
    static const wchar_t* keys[]={
        L"Window",L"MenuBar",L"Menu",L"Background",L"Btnface",L"Highlight",
        L"ActiveCaption",L"CaptionText",L"InactiveCaption",L"InactiveCaptionText",
        L"GradientActiveCaption",L"GradientInactiveCaption",L"HighlightText",
        L"MenuHilight",L"BtnShadow",L"GrayText",L"BtnHighlight",L"DkShadow3d",L"Light3d"};

    static const wchar_t* labels[]={
        L"Window background",L"Menu bar",L"Menu background",L"Desktop background",L"Button face",
        L"Selection highlight",L"Active caption",L"Active caption text",L"Inactive caption",
        L"Inactive caption text",L"Active caption gradient",L"Inactive caption gradient",
        L"Highlight text",L"Menu highlight",L"Button shadow",L"Disabled/gray text",
        L"Button highlight",L"3D dark shadow",L"3D light"};

    std::vector<BYTE> raw; WORD lang=0;
    if(!LsGetTextFile(cache.sourcePath,cache.textFileName,raw,lang,errorText)) return false;

    LunaIni ini;
    if(!ini.ParseUtf16Le(raw,errorText)) return false;

    const std::vector<IniSection>& secs=ini.Sections();
    const IniSection* sys=NULL;
    for(size_t i=0;i<secs.size();++i)
        if(EqualsNoCaseLocal(secs[i].name,L"SysMetrics")){sys=&secs[i];break;}

    if(!sys){errorText=L"[SysMetrics] not found.";return false;}

    for(size_t k=0;k<sizeof(keys)/sizeof(keys[0]);++k)
    {
        ThemeClassicColor cc;
        cc.key=keys[k];
        cc.label=labels[k];

        for(size_t p=0;p<sys->properties.size();++p)
        {
            if(EqualsNoCaseLocal(sys->properties[p].key,cc.key) &&
               LsParseRgb(sys->properties[p].value,cc.r,cc.g,cc.b))
            {
                colors.push_back(cc);
                break;
            }
        }
    }

    return !colors.empty();
}

static bool LsParseFontSpec(const std::wstring& value,ThemeClassicFont& f)
{
    std::wstring s=value;
    size_t p1=s.find(L',');
    if(p1==std::wstring::npos) return false;
    size_t p2=s.find(L',',p1+1);
    f.face=TrimWs(s.substr(0,p1));
    std::wstring sizeText=TrimWs(p2==std::wstring::npos?s.substr(p1+1):s.substr(p1+1,p2-p1-1));
    unsigned int ps=0;
    if(swscanf(sizeText.c_str(),L"%u",&ps)!=1 || ps==0) return false;
    f.pointSize=ps;
    f.bold=false;f.italic=false;
    if(p2!=std::wstring::npos)
    {
        std::wstring flags=TrimWs(s.substr(p2+1));
        std::wstring up=flags;
        for(size_t i=0;i<up.size();++i) up[i]=(wchar_t)towupper(up[i]);
        if(up.find(L"BOLD")!=std::wstring::npos) f.bold=true;
        if(up.find(L"ITALIC")!=std::wstring::npos) f.italic=true;
    }
    return !f.face.empty();
}

static std::wstring LsFontSpec(const ThemeClassicFont& f)
{
    std::wstringstream s;
    s<<f.face<<L", "<<f.pointSize;
    if(f.bold||f.italic)
    {
        s<<L", ";
        if(f.bold) s<<L"bold";
        if(f.bold&&f.italic) s<<L", ";
        if(f.italic) s<<L"italic";
    }
    return s.str();
}

bool ThemeGenerator::GetClassicFonts(const ThemeSourceCache& cache,
                                     std::vector<ThemeClassicFont>& fonts,
                                     std::wstring& errorText)
{
    fonts.clear();
    if(!cache.ready){errorText=L"Theme source cache is not ready.";return false;}

    static const wchar_t* keys[]={
        L"CaptionFont",L"SmallCaptionFont",L"MenuFont",L"StatusFont",L"MsgBoxFont",L"IconTitleFont"};
    static const wchar_t* labels[]={
        L"Caption font",L"Small caption font",L"Menu font",L"Status font",L"Message box font",L"Icon title font"};

    std::vector<BYTE> raw; WORD lang=0;
    if(!LsGetTextFile(cache.sourcePath,cache.textFileName,raw,lang,errorText)) return false;
    LunaIni ini; if(!ini.ParseUtf16Le(raw,errorText)) return false;
    const std::vector<IniSection>& secs=ini.Sections(); const IniSection* sys=NULL;
    for(size_t i=0;i<secs.size();++i) if(EqualsNoCaseLocal(secs[i].name,L"SysMetrics")){sys=&secs[i];break;}
    if(!sys){errorText=L"[SysMetrics] not found.";return false;}

    for(size_t k=0;k<sizeof(keys)/sizeof(keys[0]);++k)
    {
        ThemeClassicFont f;f.key=keys[k];f.label=labels[k];
        for(size_t p=0;p<sys->properties.size();++p)
            if(EqualsNoCaseLocal(sys->properties[p].key,f.key) && LsParseFontSpec(sys->properties[p].value,f))
            {fonts.push_back(f);break;}
    }
    return !fonts.empty();
}

bool ThemeGenerator::GetClassicMetrics(const ThemeSourceCache& cache,
                                       std::vector<ThemeClassicMetric>& metrics,
                                       std::wstring& errorText)
{
    metrics.clear();
    if(!cache.ready){errorText=L"Theme source cache is not ready.";return false;}

    static const wchar_t* keys[]={
        L"CaptionBarHeight",L"SMCaptionBarHeight",L"SMCaptionBarWidth",L"ScrollbarWidth",L"ScrollbarHeight"};
    static const wchar_t* labels[]={
        L"Caption bar height",L"Small caption height",L"Small caption width",L"Scrollbar width",L"Scrollbar height"};

    std::vector<BYTE> raw; WORD lang=0;
    if(!LsGetTextFile(cache.sourcePath,cache.textFileName,raw,lang,errorText)) return false;
    LunaIni ini; if(!ini.ParseUtf16Le(raw,errorText)) return false;
    const std::vector<IniSection>& secs=ini.Sections(); const IniSection* sys=NULL;
    for(size_t i=0;i<secs.size();++i) if(EqualsNoCaseLocal(secs[i].name,L"SysMetrics")){sys=&secs[i];break;}
    if(!sys){errorText=L"[SysMetrics] not found.";return false;}

    for(size_t k=0;k<sizeof(keys)/sizeof(keys[0]);++k)
    {
        ThemeClassicMetric m;m.key=keys[k];m.label=labels[k];
        for(size_t p=0;p<sys->properties.size();++p)
            if(EqualsNoCaseLocal(sys->properties[p].key,m.key))
            {
                int v=0;
                if(swscanf(sys->properties[p].value.c_str(),L"%d",&v)==1)
                {m.value=v;metrics.push_back(m);}
                break;
            }
    }
    return !metrics.empty();
}


static bool LsReplaceExistingIniProperty(std::wstring& text,
                                         const std::wstring& sectionName,
                                         const std::wstring& propertyName,
                                         const std::wstring& newValue)
{
    bool inSection=false;
    size_t pos=0;

    while(pos<text.size())
    {
        size_t lineEnd=text.find(L'\n',pos);
        if(lineEnd==std::wstring::npos) lineEnd=text.size();

        size_t contentEnd=lineEnd;
        if(contentEnd>pos && text[contentEnd-1]==L'\r') --contentEnd;

        std::wstring line=text.substr(pos,contentEnd-pos);
        std::wstring trimmed=TrimWsLocal(line);

        if(trimmed.size()>=2 && trimmed[0]==L'[' && trimmed[trimmed.size()-1]==L']')
        {
            std::wstring sec=TrimWsLocal(trimmed.substr(1,trimmed.size()-2));
            inSection=EqualsNoCaseLocal(sec,sectionName);
        }
        else if(inSection && !trimmed.empty() && trimmed[0]!=L';')
        {
            size_t eq=line.find(L'=');
            if(eq!=std::wstring::npos)
            {
                std::wstring key=TrimWsLocal(line.substr(0,eq));
                if(EqualsNoCaseLocal(key,propertyName))
                {
                    std::wstring replacement=key+L" = "+newValue;
                    std::wstring eol;
                    if(lineEnd<text.size()) eol=L"\r\n";
                    text.replace(pos,(lineEnd<text.size()?lineEnd+1:lineEnd)-pos,replacement+eol);
                    return true;
                }
            }
        }

        if(lineEnd>=text.size()) break;
        pos=lineEnd+1;
    }

    return false;
}

static bool LsDarkPresetRequested(const std::vector<ThemeRoleColor>& edits)
{
    for(size_t i=0;i<edits.size();++i)
        if(edits[i].surfaceFill &&
           (EqualsNoCaseLocal(edits[i].role,L"StartMenu.ProgramList") ||
            EqualsNoCaseLocal(edits[i].role,L"ExplorerBar.NormalGroup")))
            return true;
    return false;
}

static void LsApplyDarkExistingProperties(std::wstring& text)
{
    struct Edit { const wchar_t* section; const wchar_t* property; const wchar_t* value; };
    static const Edit e[]={
        // Start menu foregrounds. These properties already exist in stock Luna.
        {L"StartPanel.ProgList",          L"TextColor",    L"235 235 235"},
        {L"StartPanel.ProgList",          L"HotTracking",  L"120 185 255"},
        {L"StartPanel.ProgList",          L"CaptionText",  L"176 176 176"},
        {L"StartPanel.MorePrograms",      L"TextColor",    L"235 235 235"},
        {L"StartPanel.MorePrograms",      L"HotTracking",  L"120 185 255"},
        {L"StartPanel.PlacesList",        L"TextColor",    L"225 232 240"},
        {L"StartPanel.PlacesList",        L"HotTracking",  L"135 195 255"},
        {L"StartMenu::MenuBand",          L"TextColor",    L"235 235 235"},
        {L"StartMenu::MenuBand(Hot)",     L"TextColor",    L"255 255 255"},
        {L"StartMenu::Toolbar",           L"TextColor",    L"235 235 235"},
        {L"StartMenu::Toolbar(Hot)",      L"TextColor",    L"255 255 255"},

        // Explorer task pane: dark panel surfaces + readable text.
        {L"ExplorerBar.NormalGroupBackground", L"FillColor",   L"52 52 52"},
        {L"ExplorerBar.NormalGroupBackground", L"BorderColor", L"82 82 82"},
        {L"ExplorerBar.NormalGroupBackground", L"TextColor",   L"225 225 225"},
        {L"ExplorerBar.NormalGroupHead",       L"TextColor",   L"225 225 225"},
        {L"ExplorerBar.SpecialGroupBackground",L"TextColor",   L"225 225 225"},
        {L"ExplorerBar.SpecialGroupHead",      L"TextColor",   L"255 255 255"},

        // Property-sheet/dialog client surfaces. Replace all common stock
        // Luna fill hints used by property pages. Existing-key-only means a
        // section/property absent in a given Luna scheme is simply ignored.
        {L"Window.dialog",                L"FillColor",     L"48 48 48"},
        {L"Window.dialog",                L"FillColorHint", L"48 48 48"},
        {L"Window.dialog",                L"BorderColor",   L"70 70 70"},
        {L"Tab",                          L"FillColor",     L"48 48 48"},
        {L"Tab",                          L"FillColorHint", L"48 48 48"},
        {L"Tab.Pane",                     L"FillColor",     L"48 48 48"},
        {L"Tab.Pane",                     L"FillColorHint", L"48 48 48"},
        {L"Tab.Body",                     L"FillColor",     L"48 48 48"},
        {L"Tab.Body",                     L"FillColorHint", L"48 48 48"},

        // Shell/list/tree client foregrounds. Explorer's folder captions can
        // use themed class colors rather than COLOR_WINDOWTEXT.
        {L"ListView",                     L"TextColor",     L"238 238 238"},
        {L"ListView",                     L"FillColor",     L"58 58 58"},
        {L"ListView",                     L"FillColorHint", L"58 58 58"},
        {L"ListView.ListItem",            L"TextColor",     L"238 238 238"},
        {L"TreeView",                     L"TextColor",     L"238 238 238"},
        {L"TreeView",                     L"FillColor",     L"58 58 58"},
        {L"TreeView",                     L"FillColorHint", L"58 58 58"},
        {L"Edit",                         L"TextColor",     L"238 238 238"},
        {L"Edit",                         L"FillColor",     L"64 64 64"},
        {L"ListBox",                      L"TextColor",     L"238 238 238"},
        {L"ListBox",                      L"FillColor",     L"58 58 58"},

        // Status bar foreground/background. Explorer's status text is not
        // guaranteed to follow COLOR_WINDOWTEXT, so handle the themed status
        // classes directly when stock Luna exposes these keys.
        {L"Status",                       L"TextColor",     L"20 20 20"},
        {L"Status",                       L"FillColor",     L"152 152 152"},
        {L"Status.Pane",                  L"TextColor",     L"20 20 20"},
        {L"Status.Pane",                  L"FillColor",     L"152 152 152"},
        {L"Status.Gripper",               L"TextColor",     L"40 40 40"},

        // Explorer menu/rebar/toolbar labels.
        {L"Rebar",                        L"TextColor",     L"232 232 232"},
        {L"Toolbar",                      L"TextColor",     L"232 232 232"},
        {L"Toolbar",                      L"FillColor",     L"46 46 46"},

        // Group-box caption on dark dialog surfaces.
        {L"button.groupbox",              L"TextColor",     L"220 220 220"},

        // Generic/menu foregrounds where stock Luna exposes these properties.
        // Replacement is existing-key-only, so unsupported sections remain untouched.
        {L"Menu",                         L"TextColor",    L"235 235 235"},
        {L"Menu.BarItem",                 L"TextColor",    L"238 238 238"},
        {L"Menu.BarItem(Hot)",            L"TextColor",    L"255 255 255"},
        {L"Menu.BarItem(Disabled)",       L"TextColor",    L"228 228 228"},
        {L"Menu.BarItem(Inactive)",       L"TextColor",    L"242 242 242"},
        {L"Menu.PopupItem",               L"TextColor",    L"238 238 238"},
        {L"Menu.PopupItem(Hot)",          L"TextColor",    L"255 255 255"},
        {L"Menu.PopupItem(Disabled)",     L"TextColor",    L"228 228 228"},
        {L"Menu.BarItem(InactiveNormal)", L"TextColor",    L"242 242 242"},
        {L"Menu.BarItem(InactiveHot)",    L"TextColor",    L"255 255 255"},
        {L"Menu.BarItem(DisabledHot)",    L"TextColor",    L"235 235 235"},
        {L"Toolbar.Button",               L"TextColor",    L"235 235 235"},
        {L"Header.HeaderItem",            L"TextColor",    L"235 235 235"},
        {L"button.groupbox",              L"TextColor",    L"225 225 225"},

        // Combo edit field is BorderFill rather than a bitmap.
        {L"Combobox",                    L"FillColor",     L"68 68 68"},
        {L"Combobox",                    L"BorderColor",   L"92 92 92"},
        {L"Combobox(Disabled)",          L"FillColor",     L"82 82 82"},
        {L"Combobox(Disabled)",          L"BorderColor",   L"82 82 82"},

        // Push-button foregrounds after the button face bitmap is darkened.
        {L"button.pushbutton",           L"TextColor",     L"238 238 238"},
        {L"button.pushbutton(disabled)", L"TextColor",     L"145 145 145"}
    };

    for(size_t i=0;i<sizeof(e)/sizeof(e[0]);++i)
        LsReplaceExistingIniProperty(text,e[i].section,e[i].property,e[i].value);
}


static bool LsRewriteSysMetrics(const std::wstring& source,const std::wstring& name,
                                const std::vector<ThemeRoleColor>& roleEdits,
                                const std::vector<ThemeClassicColor>& colors,
                                const std::vector<ThemeClassicFont>& fonts,
                                const std::vector<ThemeClassicMetric>& metrics,
                                ResourceReplacement& rr,
                                unsigned int& colorChanged,
                                unsigned int& fontChanged,
                                unsigned int& metricChanged,
                                std::wstring& errorText)
{
    colorChanged=fontChanged=metricChanged=0;
    std::map<std::wstring,std::wstring> values;

    for(size_t i=0;i<colors.size();++i)
        if(colors[i].edited)
        {
            std::wstringstream s;
            s<<(unsigned int)colors[i].r<<L" "<<(unsigned int)colors[i].g<<L" "<<(unsigned int)colors[i].b;
            values[colors[i].key]=s.str();
        }

    for(size_t i=0;i<fonts.size();++i)
        if(fonts[i].edited)
            values[fonts[i].key]=LsFontSpec(fonts[i]);

    for(size_t i=0;i<metrics.size();++i)
        if(metrics[i].edited)
        {
            std::wstringstream s;s<<metrics[i].value;
            values[metrics[i].key]=s.str();
        }

    // Map sections belonging to gradient-edited roles to adjusted
    // SizingMargins. XP will then stretch the same center band into which
    // LunaXPStudio paints the gradient.
    std::map<std::wstring,std::wstring> gradientMargins;
    for(size_t ei=0;ei<roleEdits.size();++ei)
    {
        if(!roleEdits[ei].gradient) continue;
        for(size_t si=0;si<gCachedSource.schema.size();++si)
        {
            const SchemaTarget& t=gCachedSource.schema[si];
            if(!t.resourceFound || !EqualsNoCaseLocal(t.editorRole,roleEdits[ei].role))
                continue;
            if(_wcsicmp(t.sizingType.c_str(),L"stretch")!=0)
                continue;

            unsigned int l=0,r=0,tp=0,b=0;
            LsEffectiveGradientMargins(t,l,r,tp,b);
            std::wstringstream sm;
            sm<<l<<L", "<<r<<L", "<<tp<<L", "<<b;
            gradientMargins[t.section]=sm.str();

            // XP also consults the caption sizing-template geometry.
            // Keep it synchronized with the gradient-edited caption section;
            // otherwise the stock 28/35 margins still leave only a 3px center.
            std::wstring secUpper=t.section;
            for(size_t su=0;su<secUpper.size();++su)
                secUpper[su]=(wchar_t)towupper(secUpper[su]);

            if(secUpper.find(L"WINDOW.CAPTION")!=std::wstring::npos &&
               secUpper.find(L"SIZINGTEMPLATE")==std::wstring::npos)
                gradientMargins[L"Window.CaptionSizingTemplate"]=sm.str();

            if(secUpper.find(L"WINDOW.SMALLCAPTION")!=std::wstring::npos &&
               secUpper.find(L"SIZINGTEMPLATE")==std::wstring::npos)
                gradientMargins[L"Window.SmallCaptionSizingTemplate"]=sm.str();
        }
    }

    std::vector<BYTE> raw; WORD lang=0;
    if(!LsGetTextFile(source,name,raw,lang,errorText)) return false;
    std::wstring text=LsDecodeUtf16(raw);

    bool inSys=false;
    std::wstring currentSection;
    size_t pos=0;
    while(pos<text.size())
    {
        size_t lineEnd=text.find(L'\n',pos); if(lineEnd==std::wstring::npos) lineEnd=text.size();
        size_t contentEnd=lineEnd; if(contentEnd>pos && text[contentEnd-1]==L'\r') --contentEnd;
        std::wstring line=text.substr(pos,contentEnd-pos), trimmed=TrimWs(line);

        if(trimmed.size()>=2 && trimmed[0]==L'[' && trimmed[trimmed.size()-1]==L']')
        {
            currentSection=TrimWs(trimmed.substr(1,trimmed.size()-2));
            inSys=EqualsNoCaseLocal(currentSection,L"SysMetrics");
        }
        else
        {
            // Gradient bitmap sections: keep resource dimensions stock, but
            // open a wider stretch center by updating SizingMargins.
            if(!currentSection.empty())
            {
                std::map<std::wstring,std::wstring>::const_iterator gm=gradientMargins.end();
                for(std::map<std::wstring,std::wstring>::const_iterator it=gradientMargins.begin();
                    it!=gradientMargins.end();++it)
                    if(EqualsNoCaseLocal(it->first,currentSection)){gm=it;break;}

                if(gm!=gradientMargins.end())
                {
                    size_t geq=trimmed.find(L'=');
                    if(geq!=std::wstring::npos)
                    {
                        std::wstring gkey=TrimWs(trimmed.substr(0,geq));
                        if(EqualsNoCaseLocal(gkey,L"SizingMargins"))
                        {
                            std::wstring nl=L"SizingMargins = "+gm->second;
                            long delta=(long)nl.size()-(long)(contentEnd-pos);
                            text.replace(pos,contentEnd-pos,nl);
                            lineEnd=(size_t)((long)lineEnd+delta);
                            contentEnd=(size_t)((long)contentEnd+delta);
                            trimmed=nl;
                        }
                    }
                }
            }

            if(inSys)
            {
            size_t eq=trimmed.find(L'=');
            if(eq!=std::wstring::npos)
            {
                std::wstring key=TrimWs(trimmed.substr(0,eq));
                std::map<std::wstring,std::wstring>::const_iterator match=values.end();
                for(std::map<std::wstring,std::wstring>::const_iterator it=values.begin();it!=values.end();++it)
                    if(EqualsNoCaseLocal(it->first,key)){match=it;break;}

                if(match!=values.end())
                {
                    std::wstring nl=match->first+L" = "+match->second;
                    long delta=(long)nl.size()-(long)(contentEnd-pos);
                    text.replace(pos,contentEnd-pos,nl);
                    lineEnd=(size_t)((long)lineEnd+delta);
                    contentEnd=(size_t)((long)contentEnd+delta);

                    bool counted=false;
                    for(size_t i=0;i<colors.size();++i)
                        if(colors[i].edited && EqualsNoCaseLocal(colors[i].key,key)){++colorChanged;counted=true;break;}
                    if(!counted)
                        for(size_t i=0;i<fonts.size();++i)
                            if(fonts[i].edited && EqualsNoCaseLocal(fonts[i].key,key)){++fontChanged;counted=true;break;}
                    if(!counted)
                        for(size_t i=0;i<metrics.size();++i)
                            if(metrics[i].edited && EqualsNoCaseLocal(metrics[i].key,key)){++metricChanged;break;}
                }
            }
            }
        }

        if(lineEnd>=text.size()) break;
        pos=lineEnd+1;
    }

    if(LsDarkPresetRequested(roleEdits))
        LsApplyDarkExistingProperties(text);

    rr.type=L"TEXTFILE";rr.name=name;rr.language=lang;rr.data=LsEncodeUtf16(text);
    return true;
}


static bool LsExpandCompanionSizingTemplate(const SchemaTarget& sourceTarget,
                                            unsigned int leftMargin,
                                            unsigned int rightMargin,
                                            std::map<std::wstring,std::vector<BYTE> >& work,
                                            std::map<std::wstring,WORD>& langs,
                                            std::set<std::wstring>& expanded,
                                            std::wstring& errorText)
{
    std::wstring sectionUpper=sourceTarget.section;
    for(size_t i=0;i<sectionUpper.size();++i)
        sectionUpper[i]=(wchar_t)towupper(sectionUpper[i]);

    std::wstring companion;
    if(sectionUpper.find(L"WINDOW.CAPTION")!=std::wstring::npos &&
       sectionUpper.find(L"SIZINGTEMPLATE")==std::wstring::npos)
        companion=L"Window.CaptionSizingTemplate";
    else if(sectionUpper.find(L"WINDOW.SMALLCAPTION")!=std::wstring::npos &&
            sectionUpper.find(L"SIZINGTEMPLATE")==std::wstring::npos)
        companion=L"Window.SmallCaptionSizingTemplate";
    else
        return true;

    for(size_t i=0;i<gCachedSource.schema.size();++i)
    {
        const SchemaTarget& t=gCachedSource.schema[i];
        if(!EqualsNoCaseLocal(t.section,companion) || !t.resourceFound)
            continue;

        if(expanded.find(t.resolvedResource)!=expanded.end())
            return true;

        if(work.find(t.resolvedResource)==work.end())
        {
            std::map<std::wstring,std::vector<BYTE> >::const_iterator cached=
                gBitmapCache.find(t.resolvedResource);
            if(cached==gBitmapCache.end())
            {
                errorText=L"Caption sizing-template bitmap is missing from the preloaded cache.";
                return false;
            }
            work[t.resolvedResource]=cached->second;
            std::map<std::wstring,WORD>::const_iterator existingLang=langs.find(sourceTarget.resolvedResource);
            langs[t.resolvedResource]=(existingLang!=langs.end())?existingLang->second:(WORD)1033;
        }

        if(!BitmapEngine::ExpandStretchWidth(work[t.resolvedResource],
                                             t.imageCount,
                                             t.imageLayout,
                                             leftMargin,
                                             rightMargin,
                                             256,
                                             errorText))
            return false;

        expanded.insert(t.resolvedResource);
        return true;
    }

    return true;
}


static bool LsForceDarkStubbornSurfaces(std::map<std::wstring,std::vector<BYTE> >& work,
                                        std::map<std::wstring,WORD>& langs,
                                        const std::vector<ResourceEntry>& entries,
                                        std::wstring& errorText)
{
    struct ForcedSurface
    {
        const wchar_t* token;
        BYTE r,g,b;
    };

    // These are stock Luna bitmap surfaces which can remain visually light
    // even when their INI FillColor hints and semantic role are changed.
    // RecolorSurfaceStateCell with ImageCount=1 intentionally treats the
    // complete bitmap as one surface, including neutral/white pixels.
    static const ForcedSurface forced[]={
        {L"TABBACKGROUND", 38,38,38},
        {L"TABPANEEDGE",   42,42,42},

        // ScrollArrows.bmp contains the arrow-button face on stock Luna.
        // A somewhat lighter face preserves strong contrast with the arrow
        // artwork instead of letting both collapse into the same dark tone.
        {L"SCROLLARROWS_BMP", 92,92,92}
    };

    for(size_t fi=0;fi<sizeof(forced)/sizeof(forced[0]);++fi)
    {
        for(size_t ei=0;ei<entries.size();++ei)
        {
            const ResourceEntry& re=entries[ei];
            if(re.type!=L"#2" || !ContainsNoCaseLocal(re.name,forced[fi].token))
                continue;

            if(work.find(re.name)==work.end())
            {
                std::map<std::wstring,std::vector<BYTE> >::const_iterator bi=gBitmapCache.find(re.name);
                if(bi==gBitmapCache.end())
                    continue;
                work[re.name]=bi->second;
                langs[re.name]=re.language;
            }

            RecolorResult rr;
            std::wstring local;
            if(!BitmapEngine::RecolorSurfaceStateCell(work[re.name],0,1,L"vertical",
                                                       forced[fi].r,forced[fi].g,forced[fi].b,
                                                       rr,local))
            {
                errorText=L"Could not darken stubborn Luna surface "+re.name+L": "+local;
                return false;
            }
        }
    }
    return true;
}

static bool GenerateInternal(const std::wstring& sourcePath,
                             const std::wstring& textFileName,
                             const std::vector<ThemeRoleColor>& edits,
                             const std::vector<ThemeClassicColor>& classicColors,
                             const std::vector<ThemeClassicFont>& classicFonts,
                             const std::vector<ThemeClassicMetric>& classicMetrics,
                             const std::wstring& outputPath,
                             double hueWindow,
                             ThemeGenerateResult& result,
                             std::wstring& errorText)
{
    (void)hueWindow;
    result=ThemeGenerateResult();
    errorText.clear();
    result.presetRoles=(unsigned int)edits.size();

    if(sourcePath.empty() || outputPath.empty())
    {
        errorText=L"Source and output paths are required.";
        return false;
    }
    if(_wcsicmp(sourcePath.c_str(),outputPath.c_str())==0)
    {
        errorText=L"Output must be a different file from the source. LunaXP Studio never edits the source in place.";
        return false;
    }
    bool anyClassic=false;
    for(size_t ci=0;ci<classicColors.size();++ci) if(classicColors[ci].edited){anyClassic=true;break;}
    if(!anyClassic) for(size_t fi=0;fi<classicFonts.size();++fi) if(classicFonts[fi].edited){anyClassic=true;break;}
    if(!anyClassic) for(size_t mi=0;mi<classicMetrics.size();++mi) if(classicMetrics[mi].edited){anyClassic=true;break;}

    bool anyGradient=false;
    for(size_t gi=0;gi<edits.size();++gi) if(edits[gi].gradient){anyGradient=true;break;}

    if(edits.empty() && gBitmapOverrides.empty() && !anyClassic)
    {
        errorText=L"No semantic colors, classic colors, or custom bitmap resources have been edited.";
        return false;
    }

    if(!EnsureCachedSource(sourcePath,textFileName,errorText))
    {
        errorText=L"Could not prepare Luna source cache:\r\n"+errorText;
        return false;
    }

    const std::vector<SchemaTarget>& schema=gCachedSource.schema;
    const std::vector<ResourceEntry>& entries=gCachedSource.entries;

    std::map<std::wstring,std::vector<BYTE> > work;
    std::map<std::wstring,WORD> langs;
    std::set<std::wstring> appliedKeys;
    std::set<std::wstring> bulkProcessedResources;
    std::set<std::wstring> gradientExpandedResources;

    // Custom full-resource bitmap replacements are first-class edits. Seed the
    // output work set with them even if no color roles were changed.
    for(std::set<std::wstring>::const_iterator oi=gBitmapOverrides.begin();
        oi!=gBitmapOverrides.end();++oi)
    {
        std::map<std::wstring,std::vector<BYTE> >::const_iterator bi=gBitmapCache.find(*oi);
        if(bi==gBitmapCache.end())
            continue;

        const ResourceEntry* re=FindResourceByName(entries,*oi);
        if(!re)
            continue;

        work[*oi]=bi->second;
        langs[*oi]=re->language;
        ++result.customBitmapResources;
    }

    for(size_t ei=0;ei<edits.size();++ei)
    {
        bool matched=false;
        for(size_t si=0;si<schema.size();++si)
        {
            const SchemaTarget& t=schema[si];
            if(!EqualsNoCaseLocal(t.editorRole,edits[ei].role))
                continue;

            matched=true;
            std::wstringstream key;
            key<<t.resolvedResource<<L"#"<<t.stateIndex<<L"#"<<edits[ei].role;
            if(appliedKeys.find(key.str())!=appliedKeys.end())
                continue;
            appliedKeys.insert(key.str());

            const ResourceEntry* re=FindResourceByName(entries,t.resolvedResource);
            if(!re) continue;

            if(work.find(re->name)==work.end())
            {
                std::map<std::wstring,std::vector<BYTE> >::const_iterator cached=
                    gBitmapCache.find(re->name);
                if(cached==gBitmapCache.end())
                {
                    errorText=L"Required BITMAP is missing from the preloaded memory cache. Reload semantic roles.";
                    return false;
                }
                work[re->name]=cached->second;
                langs[re->name]=re->language;
            }

            // Color ALL gives every semantic role the same target color. That
            // lets us recolor the entire physical resource once while keeping
            // its original indexed/24/32-bpp format intact. This is much safer
            // for XP sprite/mask resources than promoting each state to 24-bpp.
            if(edits[ei].bulkSafeOnly)
            {
                if(!IsBulkColorSurfaceRole(edits[ei].role) || IsBulkUnsafeResource(re->name))
                {
                    ++result.skippedIncompatibleTargets;
                    continue;
                }

                if(bulkProcessedResources.find(re->name)==bulkProcessedResources.end())
                {
                    RecolorResult bulkResult;
                    std::wstring bulkError;
                    if(!BitmapEngine::RecolorWholeBitmapPreserveFormat(work[re->name],
                                                                       edits[ei].r,
                                                                       edits[ei].g,
                                                                       edits[ei].b,
                                                                       bulkResult,
                                                                       bulkError))
                    {
                        std::wstringstream e;
                        e<<L"Bulk recolor failed for "<<re->name<<L": "<<bulkError;
                        errorText=e.str();
                        return false;
                    }

                    bulkProcessedResources.insert(re->name);
                    if(bulkResult.changed) ++result.changedStateTargets;
                    else ++result.unchangedStateTargets;
                }
                continue;
            }

            if(edits[ei].gradient &&
               _wcsicmp(t.sizingType.c_str(),L"stretch")==0 &&
               gradientExpandedResources.find(re->name)==gradientExpandedResources.end())
            {
                unsigned int glm=0,grm=0,gtm=0,gbm=0;
                LsEffectiveGradientMargins(t,glm,grm,gtm,gbm);

                if(!BitmapEngine::ExpandStretchWidth(work[re->name],
                                                     t.imageCount,
                                                     t.imageLayout,
                                                     glm,
                                                     grm,
                                                     256,
                                                     errorText))
                    return false;

                gradientExpandedResources.insert(re->name);

                if(!LsExpandCompanionSizingTemplate(t,glm,grm,work,langs,
                                                    gradientExpandedResources,errorText))
                    return false;
            }

            // Some stock schema entries advertise ImageCount values even though
            // their underlying bitmap is not physically divisible into that strip.
            // This is common for generic/size-box resources such as ResizeGrip2.
            // Such targets are valid theme resources but are not safe state-cell
            // recolor targets, so skip them instead of aborting a bulk operation.
            if(!StateCellGeometryCompatible(work[re->name],t.stateIndex,t.imageCount,t.imageLayout))
            {
                ++result.skippedIncompatibleTargets;
                continue;
            }

            // Caption glyph sheets are kept stock so the white symbols remain
            // visible over recolored button faces.
            if(IsCaptionGlyphResource(re->name))
            {
                ++result.unchangedStateTargets;
                continue;
            }

            RecolorResult rr;
            std::wstring local;
            bool recolorOk=false;

            if(IsCaptionButtonBackgroundResource(re->name))
            {
                if(edits[ei].gradient)
                    {
                        unsigned int lm=0,rm=0,tm=0,bm=0;
                        LsEffectiveGradientMargins(t,lm,rm,tm,bm);
                        recolorOk=BitmapEngine::RecolorTintedStateCellGradient(work[re->name],t.stateIndex,t.imageCount,
                                                                             t.imageLayout,lm,rm,
                                                                             edits[ei].r,edits[ei].g,edits[ei].b,
                                                                             edits[ei].r2,edits[ei].g2,edits[ei].b2,rr,local);
                    }
                else
                    recolorOk=BitmapEngine::RecolorTintedStateCell(work[re->name],t.stateIndex,t.imageCount,
                                                                  t.imageLayout,edits[ei].r,edits[ei].g,edits[ei].b,rr,local);
            }
            else
            {
                if(edits[ei].gradient)
                    {
                        unsigned int lm=0,rm=0,tm=0,bm=0;
                        LsEffectiveGradientMargins(t,lm,rm,tm,bm);
                        recolorOk=BitmapEngine::RecolorWholeStateCellGradient(work[re->name],t.stateIndex,t.imageCount,
                                                                            t.imageLayout,lm,rm,
                                                                            edits[ei].r,edits[ei].g,edits[ei].b,
                                                                            edits[ei].r2,edits[ei].g2,edits[ei].b2,rr,local);
                    }
                else if(edits[ei].surfaceFill)
                {
                    if(IsSurfaceFillSafeResource(re->name))
                    {
                        BYTE sr=edits[ei].r,sg=edits[ei].g,sb=edits[ei].b;
                        AdjustSurfaceFillTarget(re->name,edits[ei].r,edits[ei].g,edits[ei].b,sr,sg,sb);
                        recolorOk=BitmapEngine::RecolorSurfaceStateCell(work[re->name],t.stateIndex,t.imageCount,
                                                                       t.imageLayout,sr,sg,sb,rr,local);
                    }
                    else
                    {
                        // Preserve unsafe companion glyph/mask/sprite resource.
                        rr.changed=false;
                        recolorOk=true;
                    }
                }
                else
                    recolorOk=BitmapEngine::RecolorWholeStateCell(work[re->name],t.stateIndex,t.imageCount,
                                                                 t.imageLayout,edits[ei].r,edits[ei].g,edits[ei].b,rr,local);
            }

            if(!recolorOk)
            {
                std::wstringstream e;
                e<<L"Failed "<<edits[ei].role<<L" / "<<re->name
                 <<L" cell "<<t.stateIndex<<L": "<<local;
                errorText=e.str();
                return false;
            }

            if(rr.changed) ++result.changedStateTargets;
            else ++result.unchangedStateTargets;
        }

        if(matched) ++result.matchedRoles;
    }

    // Final stock-Luna dark pass. Do this after semantic recoloring so the
    // stubborn tab/page bitmaps and scrollbar arrow face cannot be overwritten
    // by an earlier role operation.
    if(LsDarkPresetRequested(edits))
    {
        if(!LsForceDarkStubbornSurfaces(work,langs,entries,errorText))
            return false;
    }

    std::vector<ResourceReplacement> replacements;
    if(anyClassic || anyGradient)
    {
        ResourceReplacement tr;
        unsigned int nc=0,nf=0,nm=0;
        if(!LsRewriteSysMetrics(sourcePath,textFileName,edits,classicColors,classicFonts,classicMetrics,
                                tr,nc,nf,nm,errorText)) return false;
        if(nc||nf||nm||anyGradient)
        {
            replacements.push_back(tr);
            result.classicColorsChanged=nc;
            result.classicFontsChanged=nf;
            result.classicMetricsChanged=nm;
        }
    }
    for(std::map<std::wstring,std::vector<BYTE> >::iterator it=work.begin();it!=work.end();++it)
    {
        ResourceReplacement r;
        r.type=L"BITMAP";
        r.name=it->first;
        r.language=langs[it->first];
        r.data.swap(it->second);
        replacements.push_back(r);
    }
    result.modifiedResources=(unsigned int)replacements.size();

    if(!ResourceUpdater::CopyAndReplace(sourcePath.c_str(),outputPath.c_str(),replacements,errorText))
    {
        errorText=L"Could not create semantic recolor copy:\r\n"+errorText;
        return false;
    }

    ResourceProbe verify;
    std::vector<ResourceEntry> ve;
    PeFileInfo vi;
    if(!verify.Scan(outputPath.c_str(),ve,vi,errorText))
    {
        errorText=L"Output was written, but verification scan failed:\r\n"+errorText;
        return false;
    }

    result.verificationResourceCount=(unsigned int)ve.size();
    return true;
}

bool ThemeGenerator::GenerateFromColors(const std::wstring& sourcePath,
                                        const std::wstring& textFileName,
                                        const std::vector<ThemeRoleColor>& colors,
                                        const std::wstring& outputPath,
                                        double hueWindow,
                                        ThemeGenerateResult& result,
                                        std::wstring& errorText)
{
    std::vector<ThemeClassicColor> noneColors;
    std::vector<ThemeClassicFont> noneFonts;
    std::vector<ThemeClassicMetric> noneMetrics;
    return GenerateInternal(sourcePath,textFileName,colors,noneColors,noneFonts,noneMetrics,
                            outputPath,hueWindow,result,errorText);
}

bool ThemeGenerator::GenerateFromColorsAndClassic(const std::wstring& sourcePath,
                                                  const std::wstring& textFileName,
                                                  const std::vector<ThemeRoleColor>& colors,
                                                  const std::vector<ThemeClassicColor>& classicColors,
                                                  const std::vector<ThemeClassicFont>& classicFonts,
                                                  const std::vector<ThemeClassicMetric>& classicMetrics,
                                                  const std::wstring& outputPath,
                                                  double hueWindow,
                                                  ThemeGenerateResult& result,
                                                  std::wstring& errorText)
{
    return GenerateInternal(sourcePath,textFileName,colors,classicColors,classicFonts,classicMetrics,
                            outputPath,hueWindow,result,errorText);
}

bool ThemeGenerator::GenerateFromPreset(const std::wstring& sourcePath,
                                        const std::wstring& textFileName,
                                        const std::wstring& presetPath,
                                        const std::wstring& outputPath,
                                        double hueWindow,
                                        ThemeGenerateResult& result,
                                        std::wstring& errorText)
{
    std::vector<ThemeRoleColor> edits;
    if(!LoadPreset(presetPath,edits,errorText))
        return false;
    std::vector<ThemeClassicColor> noneColors;
    std::vector<ThemeClassicFont> noneFonts;
    std::vector<ThemeClassicMetric> noneMetrics;
    return GenerateInternal(sourcePath,textFileName,edits,noneColors,noneFonts,noneMetrics,
                            outputPath,hueWindow,result,errorText);
}
