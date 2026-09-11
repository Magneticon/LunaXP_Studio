#include "ResourceUpdater.h"

#include <sstream>

namespace
{
    static std::wstring FormatError(DWORD e)
    {
        LPWSTR buffer = NULL;
        DWORD n = FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                                 NULL, e, 0, reinterpret_cast<LPWSTR>(&buffer), 0, NULL);
        if (n && buffer)
        {
            std::wstring s(buffer, n);
            LocalFree(buffer);
            return s;
        }
        std::wstringstream ss;
        ss << L"Win32 error " << e;
        return ss.str();
    }

    static bool ParseNumericId(const std::wstring& s, WORD& id)
    {
        if (s.size() < 2 || s[0] != L'#')
            return false;
        wchar_t* endp = NULL;
        unsigned long v = wcstoul(s.c_str() + 1, &endp, 10);
        if (endp == NULL || *endp != 0 || v > 65535)
            return false;
        id = static_cast<WORD>(v);
        return true;
    }

    static bool KnownResourceTypeId(const std::wstring& s, WORD& id)
    {
        struct KnownType { const wchar_t* name; WORD id; };
        static const KnownType kTypes[] =
        {
            { L"CURSOR", 1 }, { L"BITMAP", 2 }, { L"ICON", 3 }, { L"MENU", 4 },
            { L"DIALOG", 5 }, { L"STRING", 6 }, { L"FONTDIR", 7 }, { L"FONT", 8 },
            { L"ACCELERATOR", 9 }, { L"RCDATA", 10 }, { L"MESSAGETABLE", 11 },
            { L"GROUP_CURSOR", 12 }, { L"GROUP_ICON", 14 }, { L"VERSION", 16 },
            { L"DLGINCLUDE", 17 }, { L"PLUGPLAY", 19 }, { L"VXD", 20 },
            { L"ANICURSOR", 21 }, { L"ANIICON", 22 }, { L"HTML", 23 }, { L"MANIFEST", 24 }
        };
        for (size_t i = 0; i < sizeof(kTypes) / sizeof(kTypes[0]); ++i)
        {
            if (_wcsicmp(s.c_str(), kTypes[i].name) == 0)
            {
                id = kTypes[i].id;
                return true;
            }
        }
        return false;
    }

    static LPCWSTR ToResourceTypePtr(const std::wstring& s, WORD& numeric, bool& isNumeric)
    {
        isNumeric = ParseNumericId(s, numeric) || KnownResourceTypeId(s, numeric);
        return isNumeric ? MAKEINTRESOURCEW(numeric) : s.c_str();
    }

    static LPCWSTR ToResourceNamePtr(const std::wstring& s, WORD& numeric, bool& isNumeric)
    {
        isNumeric = ParseNumericId(s, numeric);
        return isNumeric ? MAKEINTRESOURCEW(numeric) : s.c_str();
    }
}

bool ResourceUpdater::CopyAndReplace(const std::wstring& sourcePath,
                                     const std::wstring& outputPath,
                                     const std::vector<ResourceReplacement>& replacements,
                                     std::wstring& errorText)
{
    if (_wcsicmp(sourcePath.c_str(), outputPath.c_str()) == 0)
    {
        errorText = L"Refusing to overwrite the source file. Choose a different output path.";
        return false;
    }
    if (replacements.empty())
    {
        errorText = L"No resource replacements were supplied.";
        return false;
    }

    if (!CopyFileW(sourcePath.c_str(), outputPath.c_str(), FALSE))
    {
        errorText = L"Could not create output copy: " + FormatError(GetLastError());
        return false;
    }

    HANDLE h = BeginUpdateResourceW(outputPath.c_str(), FALSE);
    if (h == NULL)
    {
        errorText = L"BeginUpdateResource failed: " + FormatError(GetLastError());
        DeleteFileW(outputPath.c_str());
        return false;
    }

    for (size_t i = 0; i < replacements.size(); ++i)
    {
        const ResourceReplacement& r = replacements[i];
        WORD typeId = 0, nameId = 0;
        bool typeNumeric = false, nameNumeric = false;
        LPCWSTR typePtr = ToResourceTypePtr(r.type, typeId, typeNumeric);
        LPCWSTR namePtr = ToResourceNamePtr(r.name, nameId, nameNumeric);
        LPVOID ptr = r.data.empty() ? NULL : const_cast<BYTE*>(&r.data[0]);
        if (!UpdateResourceW(h, typePtr, namePtr, r.language, ptr, static_cast<DWORD>(r.data.size())))
        {
            DWORD e = GetLastError();
            EndUpdateResourceW(h, TRUE);
            DeleteFileW(outputPath.c_str());
            errorText = L"UpdateResource failed for " + r.name + L": " + FormatError(e);
            return false;
        }
    }

    if (!EndUpdateResourceW(h, FALSE))
    {
        errorText = L"EndUpdateResource failed: " + FormatError(GetLastError());
        DeleteFileW(outputPath.c_str());
        return false;
    }
    return true;
}
