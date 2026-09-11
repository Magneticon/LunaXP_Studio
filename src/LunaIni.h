#pragma once

#include <string>
#include <vector>

struct IniProperty
{
    std::wstring key;
    std::wstring value;
    unsigned int lineNumber;
};

struct IniSection
{
    std::wstring name;
    unsigned int lineNumber;
    std::vector<IniProperty> properties;
};

class LunaIni
{
public:
    bool ParseUtf16Le(const std::vector<unsigned char>& bytes, std::wstring& errorText);
    const std::vector<IniSection>& Sections() const { return m_sections; }

    static std::wstring Trim(const std::wstring& s);
    static std::wstring StripComment(const std::wstring& s);

private:
    std::vector<IniSection> m_sections;
};
