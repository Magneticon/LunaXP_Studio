#include "LunaSchema.h"
#include <cwctype>

bool LunaSchema::EqualsNoCase(const std::wstring& a, const std::wstring& b)
{
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i)
        if (towupper(a[i]) != towupper(b[i])) return false;
    return true;
}

bool LunaSchema::StartsWithNoCase(const std::wstring& value, const wchar_t* prefix)
{
    std::wstring p(prefix);
    if (value.size() < p.size()) return false;
    for (size_t i = 0; i < p.size(); ++i)
        if (towupper(value[i]) != towupper(p[i])) return false;
    return true;
}

bool LunaSchema::ContainsNoCase(const std::wstring& value, const wchar_t* needle)
{
    std::wstring v(value), n(needle);
    for (size_t i = 0; i < v.size(); ++i) v[i] = static_cast<wchar_t>(towupper(v[i]));
    for (size_t i = 0; i < n.size(); ++i) n[i] = static_cast<wchar_t>(towupper(n[i]));
    return v.find(n) != std::wstring::npos;
}

std::wstring LunaSchema::CanonicalResourceKey(const std::wstring& resourceName)
{
    if (StartsWithNoCase(resourceName, L"BLUE_")) return resourceName.substr(5);
    if (StartsWithNoCase(resourceName, L"HOMESTEAD_")) return resourceName.substr(10);
    if (StartsWithNoCase(resourceName, L"METALLIC_")) return resourceName.substr(9);
    return resourceName;
}

std::wstring LunaSchema::RefineEditorRole(const StateReference& state)
{
    const std::wstring& s = state.section;
    const std::wstring& r = state.resourceName;

    // Preserve the most important state-aware roles from v1.3.
    if (StartsWithNoCase(state.editorRole, L"Window.Active") ||
        StartsWithNoCase(state.editorRole, L"Window.Inactive") ||
        StartsWithNoCase(state.editorRole, L"Window.CaptionButtons."))
        return state.editorRole;

    if (StartsWithNoCase(s, L"WINDOW."))
    {
        if (ContainsNoCase(s, L"CLOSEBUTTON")) return L"Window.CaptionButtons.Close";
        if (ContainsNoCase(s, L"MAXBUTTON")) return L"Window.CaptionButtons.Maximize";
        if (ContainsNoCase(s, L"MINBUTTON")) return L"Window.CaptionButtons.Minimize";
        if (ContainsNoCase(s, L"RESTOREBUTTON")) return L"Window.CaptionButtons.Restore";
        if (ContainsNoCase(s, L"HELPBUTTON")) return L"Window.CaptionButtons.Help";
        return L"Window.Other";
    }

    if (StartsWithNoCase(s, L"TASKBAR.BACKGROUND")) return L"Taskbar.Background";
    if (StartsWithNoCase(s, L"TASKBAR.SIZINGBAR")) return L"Taskbar.SizingBars";
    if (StartsWithNoCase(s, L"TASKBAR::REBAR.GRIPPER")) return L"Taskbar.Grippers";
    if (StartsWithNoCase(s, L"TASKBAR::REBAR.CHEVRON")) return L"Taskbar.Chevrons";
    if (StartsWithNoCase(s, L"TASKBAR::TOOLBAR") || StartsWithNoCase(s, L"TASKBARVERT::TOOLBAR")) return L"Taskbar.ToolbarButtons";
    if (StartsWithNoCase(s, L"TASKBAND.FLASH")) return L"Taskbar.Flash";
    if (StartsWithNoCase(s, L"TASKBAND::TOOLBAR") || StartsWithNoCase(s, L"TASKBANDVERT::TOOLBAR")) return L"Taskbar.TaskButtons";
    if (StartsWithNoCase(s, L"TASKBAND::SCROLLBAR")) return L"Taskbar.Scrollbar";
    if (StartsWithNoCase(s, L"TASKBANDGROUPMENU::TOOLBAR.BUTTON")) return L"Taskbar.GroupMenu.Hover";
    if (StartsWithNoCase(s, L"TASKBANDGROUPMENU::TOOLBAR")) return L"Taskbar.GroupMenu.Background";
    if (ContainsNoCase(r, L"TASKBARTRAY")) return L"Taskbar.NotificationArea";

    if (EqualsNoCase(s, L"START::BUTTON")) return L"StartMenu.StartButton";
    if (StartsWithNoCase(s, L"STARTPANEL.USERPANE")) return L"StartMenu.UserPane";
    if (StartsWithNoCase(s, L"STARTPANEL.USERPICTURE")) return L"StartMenu.UserPicture";
    if (StartsWithNoCase(s, L"STARTPANEL.PROGLISTSEPARATOR")) return L"StartMenu.ProgramList.Separator";
    if (StartsWithNoCase(s, L"STARTPANEL.PROGLIST")) return L"StartMenu.ProgramList";
    if (StartsWithNoCase(s, L"STARTPANEL.MOREPROGRAMSARROW(HOT)")) return L"StartMenu.MorePrograms.ArrowHot";
    if (StartsWithNoCase(s, L"STARTPANEL.MOREPROGRAMSARROW")) return L"StartMenu.MorePrograms.Arrow";
    if (StartsWithNoCase(s, L"STARTPANEL.MOREPROGRAMS")) return L"StartMenu.MorePrograms.Background";
    if (StartsWithNoCase(s, L"STARTPANEL.PLACESLISTSEPARATOR")) return L"StartMenu.PlacesList.Separator";
    if (StartsWithNoCase(s, L"STARTPANEL.PLACESLIST")) return L"StartMenu.PlacesList";
    if (StartsWithNoCase(s, L"STARTPANEL.LOGOFFBUTTONS(HOT)")) return L"StartMenu.Logoff.ButtonsHot";
    if (StartsWithNoCase(s, L"STARTPANEL.LOGOFFBUTTONS")) return L"StartMenu.Logoff.Buttons";
    if (StartsWithNoCase(s, L"STARTPANEL.LOGOFF")) return L"StartMenu.Logoff.Background";
    if (StartsWithNoCase(s, L"STARTMENU::MENUBAND.NEWAPPBUTTON")) return L"StartMenu.MenuBand.NewApp";
    if (StartsWithNoCase(s, L"STARTMENU::MENUBAND.SEPERATOR")) return L"StartMenu.MenuBand.Separator";
    if (StartsWithNoCase(s, L"STARTMENU::TOOLBAR.BUTTON")) return L"StartMenu.Toolbar.Buttons";
    if (StartsWithNoCase(s, L"STARTMENU::TOOLBAR")) return L"StartMenu.Toolbar.Background";

    if (StartsWithNoCase(s, L"EXPLORERBAR.HEADER")) return L"ExplorerBar.Header";
    if (StartsWithNoCase(s, L"EXPLORERBAR.NORMALGROUP")) return L"ExplorerBar.NormalGroup";
    if (StartsWithNoCase(s, L"EXPLORERBAR.SPECIALGROUP")) return L"ExplorerBar.SpecialGroup";
    if (StartsWithNoCase(s, L"EXPLORERBAR::")) return L"ExplorerBar.Toolbar";

    // Preserve already-useful control roles from the state mapper.
    if (!state.editorRole.empty() && state.editorRole != L"Taskbar" && state.editorRole != L"StartMenu" && state.editorRole != L"ExplorerBar")
        return state.editorRole;

    return state.editorRole.empty() ? state.broadRole : state.editorRole;
}

void LunaSchema::Build(const std::vector<ImageReference>& images,
                       const std::vector<StateReference>& states,
                       std::vector<SchemaTarget>& output)
{
    output.clear();
    for (size_t i = 0; i < states.size(); ++i)
    {
        SchemaTarget t;
        t.section = states[i].section;
        t.property = states[i].property;
        t.canonicalResource = CanonicalResourceKey(states[i].resourceName);
        t.resolvedResource = states[i].resourceName;
        t.editorRole = RefineEditorRole(states[i]);
        t.stateIndex = states[i].stateIndex;
        t.stateName = states[i].stateName;
        t.confidence = states[i].confidence;
        t.imageCount = states[i].imageCount;
        t.imageLayout = states[i].imageLayout;
        t.sizingType = states[i].sizingType;
        t.sizingMargins = states[i].sizingMargins;
        t.resourceFound = states[i].resourceFound;
        t.sourceLine = states[i].sourceLine;
        output.push_back(t);
    }
    (void)images;
}

static unsigned int CountRolePrefix(const std::vector<SchemaTarget>& schema, const wchar_t* rolePrefix)
{
    unsigned int count = 0;
    std::wstring p(rolePrefix);
    for (size_t i = 0; i < schema.size(); ++i)
    {
        if (schema[i].editorRole.size() >= p.size())
        {
            bool match = true;
            for (size_t j = 0; j < p.size(); ++j)
                if (towupper(schema[i].editorRole[j]) != towupper(p[j])) { match = false; break; }
            if (match) ++count;
        }
    }
    return count;
}

void LunaSchema::Validate(const std::vector<ImageReference>& images,
                          const std::vector<SchemaTarget>& schema,
                          SchemaValidationReport& report)
{
    report.imageReferences = static_cast<unsigned int>(images.size());
    report.resolvedReferences = 0;
    for (size_t i = 0; i < images.size(); ++i) if (images[i].resourceFound) ++report.resolvedReferences;
    report.stateCells = static_cast<unsigned int>(schema.size());
    report.confirmedStates = report.inferredStates = report.genericStates = 0;
    for (size_t i = 0; i < schema.size(); ++i)
    {
        if (EqualsNoCase(schema[i].confidence, L"Confirmed")) ++report.confirmedStates;
        else if (EqualsNoCase(schema[i].confidence, L"Inferred")) ++report.inferredStates;
        else ++report.genericStates;
    }

    report.critical.clear();
    const wchar_t* criticalNames[] = {
        L"Window.ActiveCaption", L"Window.InactiveCaption", L"Window.ActiveFrame", L"Window.InactiveFrame",
        L"Window.CaptionButtons", L"StartMenu.StartButton", L"StartMenu.UserPane", L"StartMenu.ProgramList",
        L"StartMenu.PlacesList", L"StartMenu.Logoff", L"Taskbar.Background", L"Taskbar.TaskButtons",
        L"Taskbar.Flash", L"Controls.PushButton", L"Controls.Scrollbar"
    };
    const size_t criticalCount = sizeof(criticalNames) / sizeof(criticalNames[0]);
    report.valid = (report.imageReferences > 0 && report.resolvedReferences == report.imageReferences);
    for (size_t i = 0; i < criticalCount; ++i)
    {
        SchemaValidationItem item;
        item.name = criticalNames[i];
        item.matches = CountRolePrefix(schema, criticalNames[i]);
        item.ok = item.matches > 0;
        if (!item.ok) report.valid = false;
        report.critical.push_back(item);
    }
}
