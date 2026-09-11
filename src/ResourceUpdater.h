#pragma once

#include <windows.h>
#include <string>
#include <vector>

struct ResourceReplacement
{
    std::wstring type;
    std::wstring name;
    WORD language;
    std::vector<BYTE> data;
};

class ResourceUpdater
{
public:
    static bool CopyAndReplace(const std::wstring& sourcePath,
                               const std::wstring& outputPath,
                               const std::vector<ResourceReplacement>& replacements,
                               std::wstring& errorText);
};
