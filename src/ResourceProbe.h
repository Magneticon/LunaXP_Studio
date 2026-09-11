#pragma once

#include <windows.h>
#include <string>
#include <vector>

struct ResourceEntry
{
    std::wstring type;
    std::wstring name;
    WORD language;
    DWORD size;
    DWORD dataRva;
    DWORD rawOffset;
    std::wstring sha256;
};

struct PeFileInfo
{
    std::wstring architecture;
    WORD machine;
    bool pe32Plus;
    DWORD resourceRva;
    DWORD resourceSize;
};

class ResourceProbe
{
public:
    ResourceProbe();

    bool Scan(const std::wstring& filePath,
              std::vector<ResourceEntry>& entries,
              PeFileInfo& fileInfo,
              std::wstring& errorText);

    bool GetResourceData(const ResourceEntry& entry,
                         std::vector<BYTE>& data,
                         std::wstring& errorText) const;

private:
    struct SectionInfo
    {
        DWORD virtualAddress;
        DWORD virtualSize;
        DWORD rawOffset;
        DWORD rawSize;
    };

    std::vector<BYTE> m_file;
    std::vector<SectionInfo> m_sections;
    DWORD m_resourceRva;
    DWORD m_resourceRaw;

    bool ReadFileBytes(const std::wstring& filePath, std::wstring& errorText);
    bool ParseHeaders(PeFileInfo& fileInfo, std::wstring& errorText);
    bool WalkResources(std::vector<ResourceEntry>& entries, std::wstring& errorText);

    bool WalkDirectory(DWORD directoryRelativeOffset,
                       int level,
                       const std::wstring& inheritedType,
                       const std::wstring& inheritedName,
                       std::vector<ResourceEntry>& entries,
                       std::wstring& errorText);

    bool ReadResourceIdentifier(DWORD nameField, std::wstring& value) const;
    bool RvaToRaw(DWORD rva, DWORD& raw) const;
    bool RangeValid(size_t offset, size_t length) const;

    static std::wstring NumericIdentifierToString(WORD id);
    static std::wstring ResourceTypeToString(WORD id);
    static std::wstring MachineToString(WORD machine);
    static std::wstring FormatWin32Error(DWORD error);
    static std::wstring Sha256Hex(const BYTE* data, size_t size);
};
