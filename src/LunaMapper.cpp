#include "LunaMapper.h"

#include <cwctype>

bool LunaMapper::EqualsNoCase(const std::wstring& a, const std::wstring& b)
{
    if (a.size() != b.size())
        return false;
    for (size_t i = 0; i < a.size(); ++i)
    {
        if (towupper(a[i]) != towupper(b[i]))
            return false;
    }
    return true;
}

bool LunaMapper::StartsWithNoCase(const std::wstring& value, const wchar_t* prefix)
{
    std::wstring p(prefix);
    if (value.size() < p.size())
        return false;
    for (size_t i = 0; i < p.size(); ++i)
    {
        if (towupper(value[i]) != towupper(p[i]))
            return false;
    }
    return true;
}

std::wstring LunaMapper::ToUpper(const std::wstring& value)
{
    std::wstring out(value);
    for (size_t i = 0; i < out.size(); ++i)
        out[i] = static_cast<wchar_t>(towupper(out[i]));
    return out;
}

std::wstring LunaMapper::FindProperty(const IniSection& section, const wchar_t* key)
{
    for (size_t i = 0; i < section.properties.size(); ++i)
    {
        if (EqualsNoCase(section.properties[i].key, key))
            return section.properties[i].value;
    }
    return L"";
}

bool LunaMapper::IsImageProperty(const std::wstring& key)
{
    std::wstring u = ToUpper(key);
    return StartsWithNoCase(u, L"IMAGEFILE") ||
           StartsWithNoCase(u, L"GLYPHIMAGEFILE") ||
           StartsWithNoCase(u, L"STOCKIMAGEFILE");
}

std::wstring LunaMapper::NormalizeBitmapResourceName(const std::wstring& imagePath)
{
    std::wstring out = ToUpper(imagePath);
    for (size_t i = 0; i < out.size(); ++i)
    {
        wchar_t& c = out[i];
        if (c == L'\\' || c == L'/' || c == L'.' || c == L' ' || c == L'-')
            c = L'_';
    }

    // Collapse duplicate underscores in case a path contains punctuation combinations.
    std::wstring compact;
    compact.reserve(out.size());
    for (size_t i = 0; i < out.size(); ++i)
    {
        if (out[i] == L'_' && !compact.empty() && compact[compact.size() - 1] == L'_')
            continue;
        compact.push_back(out[i]);
    }
    return compact;
}

std::wstring LunaMapper::InferRole(const std::wstring& section, const std::wstring& imagePath)
{
    std::wstring s = ToUpper(section);
    std::wstring p = ToUpper(imagePath);

    if (StartsWithNoCase(s, L"WINDOW") || p.find(L"FRAMECAPTION") != std::wstring::npos ||
        p.find(L"FRAMELEFT") != std::wstring::npos || p.find(L"FRAMERIGHT") != std::wstring::npos ||
        p.find(L"FRAMEBOTTOM") != std::wstring::npos)
        return L"Window";

    if (StartsWithNoCase(s, L"TASKBAR") || StartsWithNoCase(s, L"TASKBAND") ||
        p.find(L"TASKBAR") != std::wstring::npos || p.find(L"TASKBAND") != std::wstring::npos ||
        p.find(L"TRAY") != std::wstring::npos)
        return L"Taskbar";

    if (StartsWithNoCase(s, L"START") || p.find(L"STARTBUTTON") != std::wstring::npos ||
        p.find(L"STARTPANEL") != std::wstring::npos || p.find(L"STARTGROUP") != std::wstring::npos ||
        p.find(L"STARTUSER") != std::wstring::npos || p.find(L"STARTPLACES") != std::wstring::npos ||
        p.find(L"STARTPROGRAMS") != std::wstring::npos)
        return L"Start Menu";

    if (StartsWithNoCase(s, L"SCROLLBAR")) return L"Scrollbar";
    if (StartsWithNoCase(s, L"BUTTON")) return L"Button";
    if (StartsWithNoCase(s, L"COMBOBOX")) return L"ComboBox";
    if (StartsWithNoCase(s, L"SPIN")) return L"Spin Control";
    if (StartsWithNoCase(s, L"REBAR") || StartsWithNoCase(s, L"TOOLBAR")) return L"Toolbar/Rebar";
    if (StartsWithNoCase(s, L"STATUS")) return L"Status Bar";
    if (StartsWithNoCase(s, L"TAB")) return L"Tabs";
    if (StartsWithNoCase(s, L"TRACKBAR")) return L"Trackbar";
    if (StartsWithNoCase(s, L"PROGRESS")) return L"Progress";
    if (StartsWithNoCase(s, L"TREEVIEW")) return L"Tree View";
    if (StartsWithNoCase(s, L"LISTVIEW") || StartsWithNoCase(s, L"HEADER")) return L"List/Header";
    if (StartsWithNoCase(s, L"EXPLORERBAR")) return L"Explorer Bar";
    if (StartsWithNoCase(s, L"TOOLTIP")) return L"Tooltip";
    if (StartsWithNoCase(s, L"MENU")) return L"Menu";
    if (StartsWithNoCase(s, L"EDIT")) return L"Edit Control";
    if (StartsWithNoCase(s, L"PAGE")) return L"Page";

    return L"Other";
}

void LunaMapper::BuildImageMap(const LunaIni& ini,
                               const std::vector<ResourceEntry>& resources,
                               std::vector<ImageReference>& output)
{
    output.clear();
    const std::vector<IniSection>& sections = ini.Sections();

    for (size_t i = 0; i < sections.size(); ++i)
    {
        const IniSection& section = sections[i];
        const std::wstring imageCount = FindProperty(section, L"ImageCount");
        const std::wstring imageLayout = FindProperty(section, L"ImageLayout");
        const std::wstring sizingType = FindProperty(section, L"SizingType");
        const std::wstring sizingMargins = FindProperty(section, L"SizingMargins");

        for (size_t j = 0; j < section.properties.size(); ++j)
        {
            const IniProperty& prop = section.properties[j];
            if (!IsImageProperty(prop.key))
                continue;

            ImageReference ref;
            ref.section = section.name;
            ref.property = prop.key;
            ref.imagePath = prop.value;
            ref.resourceName = NormalizeBitmapResourceName(prop.value);
            ref.role = InferRole(section.name, prop.value);
            ref.imageCount = imageCount;
            ref.imageLayout = imageLayout;
            ref.sizingType = sizingType;
            ref.sizingMargins = sizingMargins;
            ref.resourceFound = false;
            ref.sourceLine = prop.lineNumber;

            for (size_t r = 0; r < resources.size(); ++r)
            {
                if (EqualsNoCase(resources[r].type, L"BITMAP") &&
                    EqualsNoCase(resources[r].name, ref.resourceName))
                {
                    ref.resourceFound = true;
                    break;
                }
            }
            output.push_back(ref);
        }
    }
}


unsigned int LunaMapper::ParseUnsigned(const std::wstring& value)
{
    unsigned int result = 0;
    if (value.empty())
        return 0;
    for (size_t i = 0; i < value.size(); ++i)
    {
        if (value[i] < L'0' || value[i] > L'9')
            return 0;
        result = result * 10 + static_cast<unsigned int>(value[i] - L'0');
    }
    return result;
}

static std::wstring GenericStateName(unsigned int index)
{
    wchar_t buffer[32];
    wsprintfW(buffer, L"STATE_%u", index + 1);
    return buffer;
}

void LunaMapper::DescribeState(const ImageReference& image,
                               unsigned int index,
                               std::wstring& stateName,
                               std::wstring& editorRole,
                               std::wstring& confidence)
{
    const std::wstring s = ToUpper(image.section);
    stateName = GenericStateName(index);
    editorRole = image.role;
    confidence = L"Generic";

    // WINDOW frame/caption strips: Luna definitions explicitly use two cells.
    if ((EqualsNoCase(s, L"WINDOW.CAPTION") || EqualsNoCase(s, L"WINDOW.MINCAPTION") ||
         EqualsNoCase(s, L"WINDOW.MAXCAPTION") || EqualsNoCase(s, L"WINDOW.SMALLCAPTION") ||
         EqualsNoCase(s, L"WINDOW.FRAMELEFT") || EqualsNoCase(s, L"WINDOW.FRAMERIGHT") ||
         EqualsNoCase(s, L"WINDOW.FRAMEBOTTOM") || EqualsNoCase(s, L"WINDOW.SMALLFRAMELEFT") ||
         EqualsNoCase(s, L"WINDOW.SMALLFRAMERIGHT") || EqualsNoCase(s, L"WINDOW.SMALLFRAMEBOTTOM") ||
         EqualsNoCase(s, L"WINDOW.CAPTIONSIZINGTEMPLATE") ||
         EqualsNoCase(s, L"WINDOW.SMALLCAPTIONSIZINGTEMPLATE")) && ParseUnsigned(image.imageCount) == 2)
    {
        stateName = (index == 0) ? L"ACTIVE" : L"INACTIVE";
        if (s.find(L"CAPTION") != std::wstring::npos)
            editorRole = (index == 0) ? L"Window.ActiveCaption" : L"Window.InactiveCaption";
        else
            editorRole = (index == 0) ? L"Window.ActiveFrame" : L"Window.InactiveFrame";
        confidence = L"Confirmed";
        return;
    }

    // Standard 4-state controls.
    if (ParseUnsigned(image.imageCount) == 4 &&
        (StartsWithNoCase(s, L"COMBOBOX.DROPDOWNBUTTON") ||
         StartsWithNoCase(s, L"SCROLLBAR.THUMBBTN") || StartsWithNoCase(s, L"SCROLLBAR.GRIPPER") ||
         StartsWithNoCase(s, L"SCROLLBAR.LOWERTRACK") || StartsWithNoCase(s, L"SCROLLBAR.UPPERTRACK") ||
         StartsWithNoCase(s, L"SPIN.")))
    {
        static const wchar_t* names[] = { L"NORMAL", L"HOT", L"PRESSED", L"DISABLED" };
        if (index < 4) stateName = names[index];
        editorRole = image.role;
        confidence = L"Confirmed";
        return;
    }

    // Push button states documented by the classic XP theme layout.
    if (EqualsNoCase(s, L"BUTTON.PUSHBUTTON") && ParseUnsigned(image.imageCount) == 5)
    {
        static const wchar_t* names[] = { L"NORMAL", L"HOT", L"PRESSED", L"DISABLED", L"DEFAULTED" };
        if (index < 5) stateName = names[index];
        editorRole = L"Controls.PushButton";
        confidence = L"Confirmed";
        return;
    }

    if (EqualsNoCase(s, L"BUTTON.CHECKBOX") && ParseUnsigned(image.imageCount) == 12)
    {
        static const wchar_t* names[] = {
            L"UNCHECKED_NORMAL", L"UNCHECKED_HOT", L"UNCHECKED_PRESSED", L"UNCHECKED_DISABLED",
            L"CHECKED_NORMAL", L"CHECKED_HOT", L"CHECKED_PRESSED", L"CHECKED_DISABLED",
            L"MIXED_NORMAL", L"MIXED_HOT", L"MIXED_PRESSED", L"MIXED_DISABLED"
        };
        if (index < 12) stateName = names[index];
        editorRole = L"Controls.CheckBox";
        confidence = L"Confirmed";
        return;
    }

    if (EqualsNoCase(s, L"BUTTON.RADIOBUTTON") && ParseUnsigned(image.imageCount) == 8)
    {
        static const wchar_t* names[] = {
            L"UNCHECKED_NORMAL", L"UNCHECKED_HOT", L"UNCHECKED_PRESSED", L"UNCHECKED_DISABLED",
            L"CHECKED_NORMAL", L"CHECKED_HOT", L"CHECKED_PRESSED", L"CHECKED_DISABLED"
        };
        if (index < 8) stateName = names[index];
        editorRole = L"Controls.RadioButton";
        confidence = L"Confirmed";
        return;
    }

    if (EqualsNoCase(s, L"SCROLLBAR.ARROWBTN") && ParseUnsigned(image.imageCount) == 16)
    {
        static const wchar_t* dirs[] = { L"UP", L"DOWN", L"LEFT", L"RIGHT" };
        static const wchar_t* states[] = { L"NORMAL", L"HOT", L"PRESSED", L"DISABLED" };
        if (index < 16)
        {
            stateName = std::wstring(dirs[index / 4]) + L"_" + states[index % 4];
        }
        editorRole = L"Controls.Scrollbar.Arrow";
        confidence = L"Confirmed";
        return;
    }

    if ((StartsWithNoCase(s, L"TAB.TABITEM") || StartsWithNoCase(s, L"TAB.TOPTABITEM")) &&
        ParseUnsigned(image.imageCount) == 5)
    {
        static const wchar_t* names[] = { L"NORMAL", L"HOT", L"SELECTED", L"DISABLED", L"FOCUSED" };
        if (index < 5) stateName = names[index];
        editorRole = L"Controls.Tabs";
        confidence = L"Confirmed";
        return;
    }

    if (StartsWithNoCase(s, L"TRACKBAR.THUMB") && ParseUnsigned(image.imageCount) == 5)
    {
        static const wchar_t* names[] = { L"NORMAL", L"HOT", L"PRESSED", L"FOCUSED", L"DISABLED" };
        if (index < 5) stateName = names[index];
        editorRole = L"Controls.TrackbarThumb";
        confidence = L"Confirmed";
        return;
    }

    if ((EqualsNoCase(s, L"TOOLBAR.BUTTON") || EqualsNoCase(s, L"TOOLBAR.DROPDOWNBUTTON") ||
         EqualsNoCase(s, L"TOOLBAR.SPLITBUTTON") || EqualsNoCase(s, L"TOOLBAR.SPLITBUTTONDROPDOWN") ||
         EqualsNoCase(s, L"REBAR.CHEVRON") || EqualsNoCase(s, L"REBAR.CHEVRONVERT")) &&
        ParseUnsigned(image.imageCount) == 6)
    {
        static const wchar_t* names[] = { L"NORMAL", L"HOT", L"PRESSED", L"DISABLED", L"CHECKED", L"HOTCHECKED" };
        if (index < 6) stateName = names[index];
        editorRole = L"Controls.ToolbarButton";
        confidence = L"Confirmed";
        return;
    }

    // Luna caption buttons contain eight cells. The first/second four-cell grouping is
    // strongly implied by the stock assets but not spelled out by ImageCount itself,
    // therefore keep this explicitly marked Inferred for research safety.
    if ((EqualsNoCase(s, L"WINDOW.CLOSEBUTTON") || EqualsNoCase(s, L"WINDOW.MAXBUTTON") ||
         EqualsNoCase(s, L"WINDOW.RESTOREBUTTON") || EqualsNoCase(s, L"WINDOW.MINBUTTON") ||
         EqualsNoCase(s, L"WINDOW.HELPBUTTON")) && ParseUnsigned(image.imageCount) == 8)
    {
        static const wchar_t* states[] = { L"NORMAL", L"HOT", L"PRESSED", L"DISABLED" };
        if (index < 8)
        {
            const bool inactive = (index >= 4);
            stateName = std::wstring(inactive ? L"INACTIVE_" : L"ACTIVE_") + states[index % 4];
            editorRole = inactive ? L"Window.CaptionButtons.Inactive" : L"Window.CaptionButtons.Active";
        }
        confidence = L"Inferred";
        return;
    }

    if (image.resourceName.find(L"STARTBUTTON_BMP") != std::wstring::npos && ParseUnsigned(image.imageCount) == 3)
    {
        static const wchar_t* names[] = { L"NORMAL", L"HOT", L"PRESSED" };
        if (index < 3) stateName = names[index];
        editorRole = L"StartMenu.StartButton";
        confidence = L"Confirmed";
        return;
    }

    // A useful editor-facing role even when individual cell names remain generic.
    if (image.role == L"Taskbar") editorRole = L"Taskbar";
    else if (image.role == L"Start Menu") editorRole = L"StartMenu";
    else if (image.role == L"Explorer Bar") editorRole = L"ExplorerBar";
    else if (image.role == L"Progress") editorRole = L"Controls.Progress";
    else if (image.role == L"Scrollbar") editorRole = L"Controls.Scrollbar";
    else if (image.role == L"Button") editorRole = L"Controls.Button";
}

void LunaMapper::BuildStateMap(const std::vector<ImageReference>& images,
                               std::vector<StateReference>& output)
{
    output.clear();
    for (size_t i = 0; i < images.size(); ++i)
    {
        const ImageReference& image = images[i];
        unsigned int count = ParseUnsigned(image.imageCount);
        if (count == 0)
            count = 1;

        for (unsigned int state = 0; state < count; ++state)
        {
            StateReference item;
            item.section = image.section;
            item.property = image.property;
            item.resourceName = image.resourceName;
            item.broadRole = image.role;
            item.stateIndex = state;
            item.imageLayout = image.imageLayout;
            item.imageCount = count;
            item.sizingType = image.sizingType;
            item.sizingMargins = image.sizingMargins;
            item.resourceFound = image.resourceFound;
            item.sourceLine = image.sourceLine;
            DescribeState(image, state, item.stateName, item.editorRole, item.confidence);
            output.push_back(item);
        }
    }
}
