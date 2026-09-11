#include "LunaIni.h"

#include <sstream>

std::wstring LunaIni::Trim(const std::wstring& s)
{
    size_t a = 0;
    while (a < s.size() && (s[a] == L' ' || s[a] == L'\t' || s[a] == L'\r' || s[a] == L'\n'))
        ++a;

    size_t b = s.size();
    while (b > a && (s[b - 1] == L' ' || s[b - 1] == L'\t' || s[b - 1] == L'\r' || s[b - 1] == L'\n'))
        --b;

    return s.substr(a, b - a);
}

std::wstring LunaIni::StripComment(const std::wstring& s)
{
    // Luna INI resources use ';' for comments. Image/property values do not
    // require semicolons, so stripping from the first semicolon is sufficient.
    size_t p = s.find(L';');
    return (p == std::wstring::npos) ? s : s.substr(0, p);
}

bool LunaIni::ParseUtf16Le(const std::vector<unsigned char>& bytes, std::wstring& errorText)
{
    m_sections.clear();
    errorText.clear();

    if ((bytes.size() & 1) != 0)
    {
        errorText = L"TEXTFILE resource has an odd byte count; expected UTF-16LE.";
        return false;
    }

    std::wstring text;
    text.reserve(bytes.size() / 2);
    for (size_t i = 0; i + 1 < bytes.size(); i += 2)
    {
        wchar_t ch = static_cast<wchar_t>(bytes[i] | (static_cast<unsigned int>(bytes[i + 1]) << 8));
        if (i == 0 && ch == 0xFEFF)
            continue;
        text.push_back(ch);
    }

    std::wistringstream input(text);
    std::wstring line;
    IniSection* current = NULL;
    unsigned int lineNumber = 0;

    while (std::getline(input, line))
    {
        ++lineNumber;
        if (!line.empty() && line[line.size() - 1] == L'\r')
            line.erase(line.size() - 1);

        std::wstring useful = Trim(StripComment(line));
        if (useful.empty())
            continue;

        if (useful.size() >= 2 && useful[0] == L'[' && useful[useful.size() - 1] == L']')
        {
            IniSection section;
            section.name = Trim(useful.substr(1, useful.size() - 2));
            section.lineNumber = lineNumber;
            m_sections.push_back(section);
            current = &m_sections[m_sections.size() - 1];
            continue;
        }

        size_t eq = useful.find(L'=');
        if (eq == std::wstring::npos || current == NULL)
            continue;

        IniProperty property;
        property.key = Trim(useful.substr(0, eq));
        property.value = Trim(useful.substr(eq + 1));
        property.lineNumber = lineNumber;
        current->properties.push_back(property);
    }

    if (m_sections.empty())
    {
        errorText = L"No INI sections were found in the TEXTFILE resource.";
        return false;
    }

    return true;
}
