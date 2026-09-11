#pragma once

#include <string>
#include <vector>
#include "LunaIni.h"
#include "ResourceProbe.h"

struct ImageReference
{
    std::wstring section;
    std::wstring property;
    std::wstring imagePath;
    std::wstring resourceName;
    std::wstring role;
    std::wstring imageCount;
    std::wstring imageLayout;
    std::wstring sizingType;
    std::wstring sizingMargins;
    bool resourceFound;
    unsigned int sourceLine;
};

struct StateReference
{
    std::wstring section;
    std::wstring property;
    std::wstring resourceName;
    std::wstring broadRole;
    std::wstring editorRole;
    unsigned int stateIndex;       // zero-based cell index in the bitmap strip
    std::wstring stateName;
    std::wstring confidence;       // Confirmed, Inferred, Generic
    std::wstring imageLayout;
    unsigned int imageCount;
    std::wstring sizingType;
    std::wstring sizingMargins;
    bool resourceFound;
    unsigned int sourceLine;
};

class LunaMapper
{
public:
    static void BuildImageMap(const LunaIni& ini,
                              const std::vector<ResourceEntry>& resources,
                              std::vector<ImageReference>& output);

    static void BuildStateMap(const std::vector<ImageReference>& images,
                              std::vector<StateReference>& output);

    static std::wstring NormalizeBitmapResourceName(const std::wstring& imagePath);
    static std::wstring InferRole(const std::wstring& section, const std::wstring& imagePath);

private:
    static bool EqualsNoCase(const std::wstring& a, const std::wstring& b);
    static bool StartsWithNoCase(const std::wstring& value, const wchar_t* prefix);
    static std::wstring ToUpper(const std::wstring& value);
    static std::wstring FindProperty(const IniSection& section, const wchar_t* key);
    static bool IsImageProperty(const std::wstring& key);
    static unsigned int ParseUnsigned(const std::wstring& value);
    static void DescribeState(const ImageReference& image,
                              unsigned int index,
                              std::wstring& stateName,
                              std::wstring& editorRole,
                              std::wstring& confidence);
};
