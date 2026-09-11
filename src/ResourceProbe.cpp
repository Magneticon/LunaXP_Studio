#include "ResourceProbe.h"

#include <algorithm>
#include <sstream>
#include <iomanip>

namespace
{
    static DWORD ReadU32(const std::vector<BYTE>& data, size_t offset)
    {
        return static_cast<DWORD>(data[offset]) |
               (static_cast<DWORD>(data[offset + 1]) << 8) |
               (static_cast<DWORD>(data[offset + 2]) << 16) |
               (static_cast<DWORD>(data[offset + 3]) << 24);
    }

    static WORD ReadU16(const std::vector<BYTE>& data, size_t offset)
    {
        return static_cast<WORD>(data[offset] |
               (static_cast<WORD>(data[offset + 1]) << 8));
    }

    static DWORD Ror(DWORD x, int n)
    {
        return (x >> n) | (x << (32 - n));
    }
}

ResourceProbe::ResourceProbe()
    : m_resourceRva(0), m_resourceRaw(0)
{
}

bool ResourceProbe::RangeValid(size_t offset, size_t length) const
{
    return offset <= m_file.size() && length <= (m_file.size() - offset);
}

std::wstring ResourceProbe::FormatWin32Error(DWORD error)
{
    LPWSTR buffer = NULL;
    DWORD flags = FORMAT_MESSAGE_ALLOCATE_BUFFER |
                  FORMAT_MESSAGE_FROM_SYSTEM |
                  FORMAT_MESSAGE_IGNORE_INSERTS;
    DWORD length = FormatMessageW(flags, NULL, error, 0,
        reinterpret_cast<LPWSTR>(&buffer), 0, NULL);

    std::wstring result;
    if (length && buffer)
    {
        result.assign(buffer, length);
        LocalFree(buffer);
    }
    else
    {
        std::wstringstream ss;
        ss << L"Win32 error " << error;
        result = ss.str();
    }
    return result;
}

bool ResourceProbe::ReadFileBytes(const std::wstring& filePath, std::wstring& errorText)
{
    HANDLE h = CreateFileW(filePath.c_str(), GENERIC_READ,
                           FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                           NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE)
    {
        errorText = FormatWin32Error(GetLastError());
        return false;
    }

    LARGE_INTEGER size;
    if (!GetFileSizeEx(h, &size) || size.QuadPart < 0 || size.QuadPart > 0x7fffffff)
    {
        errorText = L"File is too large or its size could not be read.";
        CloseHandle(h);
        return false;
    }

    m_file.resize(static_cast<size_t>(size.QuadPart));
    DWORD total = 0;
    while (total < m_file.size())
    {
        DWORD remaining = static_cast<DWORD>(m_file.size() - total);
        DWORD got = 0;
        if (!ReadFile(h, &m_file[total], remaining, &got, NULL))
        {
            errorText = FormatWin32Error(GetLastError());
            CloseHandle(h);
            return false;
        }
        if (got == 0)
            break;
        total += got;
    }
    CloseHandle(h);

    if (total != m_file.size())
    {
        errorText = L"Unexpected end of file.";
        return false;
    }
    return true;
}

std::wstring ResourceProbe::MachineToString(WORD machine)
{
    switch (machine)
    {
    case IMAGE_FILE_MACHINE_I386:  return L"x86 (I386)";
    case IMAGE_FILE_MACHINE_AMD64: return L"x64 (AMD64)";
    case IMAGE_FILE_MACHINE_IA64:  return L"IA64";
    default:
        {
            std::wstringstream ss;
            ss << L"unknown (0x" << std::hex << std::uppercase << machine << L")";
            return ss.str();
        }
    }
}

bool ResourceProbe::ParseHeaders(PeFileInfo& info, std::wstring& errorText)
{
    if (!RangeValid(0, 0x40) || ReadU16(m_file, 0) != IMAGE_DOS_SIGNATURE)
    {
        errorText = L"Not a valid DOS/PE file (missing MZ header).";
        return false;
    }

    DWORD peOffset = ReadU32(m_file, 0x3c);
    if (!RangeValid(peOffset, 24) || ReadU32(m_file, peOffset) != IMAGE_NT_SIGNATURE)
    {
        errorText = L"Not a valid PE file (missing PE signature).";
        return false;
    }

    size_t fileHeader = peOffset + 4;
    WORD machine = ReadU16(m_file, fileHeader);
    WORD numberOfSections = ReadU16(m_file, fileHeader + 2);
    WORD optionalSize = ReadU16(m_file, fileHeader + 16);
    size_t optional = fileHeader + 20;
    if (!RangeValid(optional, optionalSize))
    {
        errorText = L"Truncated PE optional header.";
        return false;
    }

    WORD magic = ReadU16(m_file, optional);
    bool plus = (magic == IMAGE_NT_OPTIONAL_HDR64_MAGIC);
    if (!plus && magic != IMAGE_NT_OPTIONAL_HDR32_MAGIC)
    {
        errorText = L"Unsupported PE optional-header format.";
        return false;
    }

    // DataDirectory begins at +96 in PE32 and +112 in PE32+.
    size_t dataDirectory = optional + (plus ? 112 : 96);
    size_t resourceDirectory = dataDirectory + IMAGE_DIRECTORY_ENTRY_RESOURCE * 8;
    if (!RangeValid(resourceDirectory, 8))
    {
        errorText = L"PE resource data directory is missing.";
        return false;
    }

    m_resourceRva = ReadU32(m_file, resourceDirectory);
    DWORD resourceSize = ReadU32(m_file, resourceDirectory + 4);
    if (m_resourceRva == 0 || resourceSize == 0)
    {
        errorText = L"PE file contains no resource directory.";
        return false;
    }

    size_t sectionTable = optional + optionalSize;
    m_sections.clear();
    for (WORD i = 0; i < numberOfSections; ++i)
    {
        size_t s = sectionTable + static_cast<size_t>(i) * 40;
        if (!RangeValid(s, 40))
        {
            errorText = L"Truncated PE section table.";
            return false;
        }

        SectionInfo sec;
        sec.virtualSize = ReadU32(m_file, s + 8);
        sec.virtualAddress = ReadU32(m_file, s + 12);
        sec.rawSize = ReadU32(m_file, s + 16);
        sec.rawOffset = ReadU32(m_file, s + 20);
        m_sections.push_back(sec);
    }

    if (!RvaToRaw(m_resourceRva, m_resourceRaw))
    {
        errorText = L"Resource directory RVA does not map to a file section.";
        return false;
    }

    info.machine = machine;
    info.architecture = MachineToString(machine);
    info.pe32Plus = plus;
    info.resourceRva = m_resourceRva;
    info.resourceSize = resourceSize;
    return true;
}

bool ResourceProbe::RvaToRaw(DWORD rva, DWORD& raw) const
{
    for (size_t i = 0; i < m_sections.size(); ++i)
    {
        const SectionInfo& s = m_sections[i];
        DWORD span = (std::max)(s.virtualSize, s.rawSize);
        if (rva >= s.virtualAddress && rva - s.virtualAddress < span)
        {
            DWORD delta = rva - s.virtualAddress;
            if (delta >= s.rawSize)
                return false;
            raw = s.rawOffset + delta;
            return raw < m_file.size();
        }
    }
    return false;
}

std::wstring ResourceProbe::NumericIdentifierToString(WORD id)
{
    std::wstringstream ss;
    ss << L"#" << static_cast<unsigned int>(id);
    return ss.str();
}

std::wstring ResourceProbe::ResourceTypeToString(WORD id)
{
    switch (id)
    {
    case 1:  return L"CURSOR";
    case 2:  return L"BITMAP";
    case 3:  return L"ICON";
    case 4:  return L"MENU";
    case 5:  return L"DIALOG";
    case 6:  return L"STRING";
    case 7:  return L"FONTDIR";
    case 8:  return L"FONT";
    case 9:  return L"ACCELERATOR";
    case 10: return L"RCDATA";
    case 11: return L"MESSAGETABLE";
    case 12: return L"GROUP_CURSOR";
    case 14: return L"GROUP_ICON";
    case 16: return L"VERSION";
    case 17: return L"DLGINCLUDE";
    case 19: return L"PLUGPLAY";
    case 20: return L"VXD";
    case 21: return L"ANICURSOR";
    case 22: return L"ANIICON";
    case 23: return L"HTML";
    case 24: return L"MANIFEST";
    default: return NumericIdentifierToString(id);
    }
}

bool ResourceProbe::ReadResourceIdentifier(DWORD field, std::wstring& value) const
{
    if ((field & 0x80000000UL) == 0)
    {
        value = NumericIdentifierToString(static_cast<WORD>(field & 0xffff));
        return true;
    }

    DWORD rel = field & 0x7fffffffUL;
    size_t pos = static_cast<size_t>(m_resourceRaw) + rel;
    if (!RangeValid(pos, 2))
        return false;
    WORD length = ReadU16(m_file, pos);
    pos += 2;
    if (!RangeValid(pos, static_cast<size_t>(length) * 2))
        return false;

    value.clear();
    value.reserve(length);
    for (WORD i = 0; i < length; ++i)
        value.push_back(static_cast<wchar_t>(ReadU16(m_file, pos + i * 2)));
    return true;
}

bool ResourceProbe::WalkDirectory(DWORD directoryRelativeOffset,
                                  int level,
                                  const std::wstring& inheritedType,
                                  const std::wstring& inheritedName,
                                  std::vector<ResourceEntry>& entries,
                                  std::wstring& errorText)
{
    size_t dir = static_cast<size_t>(m_resourceRaw) + directoryRelativeOffset;
    if (!RangeValid(dir, 16))
    {
        errorText = L"Resource directory points outside the file.";
        return false;
    }

    WORD named = ReadU16(m_file, dir + 12);
    WORD ids = ReadU16(m_file, dir + 14);
    DWORD count = static_cast<DWORD>(named) + ids;
    size_t table = dir + 16;
    if (!RangeValid(table, static_cast<size_t>(count) * 8))
    {
        errorText = L"Resource directory entry table is truncated.";
        return false;
    }

    for (DWORD i = 0; i < count; ++i)
    {
        size_t e = table + static_cast<size_t>(i) * 8;
        DWORD nameField = ReadU32(m_file, e);
        DWORD dataField = ReadU32(m_file, e + 4);

        std::wstring identifier;
        if (!ReadResourceIdentifier(nameField, identifier))
        {
            errorText = L"Invalid resource identifier string.";
            return false;
        }

        std::wstring type = inheritedType;
        std::wstring name = inheritedName;
        if (level == 0)
        {
            if ((nameField & 0x80000000UL) == 0)
                type = ResourceTypeToString(static_cast<WORD>(nameField & 0xffff));
            else
                type = identifier;
        }
        else if (level == 1)
            name = identifier;

        if ((dataField & 0x80000000UL) != 0)
        {
            DWORD child = dataField & 0x7fffffffUL;
            if (!WalkDirectory(child, level + 1, type, name, entries, errorText))
                return false;
        }
        else
        {
            // Standard resource trees have Type -> Name -> Language -> Data.
            // The final directory entry identifier is therefore the LANGID.
            size_t dataEntry = static_cast<size_t>(m_resourceRaw) + dataField;
            if (!RangeValid(dataEntry, 16))
            {
                errorText = L"Resource data entry points outside the file.";
                return false;
            }

            DWORD dataRva = ReadU32(m_file, dataEntry);
            DWORD dataSize = ReadU32(m_file, dataEntry + 4);
            DWORD raw = 0;
            if (!RvaToRaw(dataRva, raw) || !RangeValid(raw, dataSize))
            {
                errorText = L"Resource payload RVA/size does not map to valid file bytes.";
                return false;
            }

            ResourceEntry out;
            out.type = type.empty() ? L"<unknown>" : type;
            out.name = name.empty() ? L"<unknown>" : name;
            out.language = static_cast<WORD>(nameField & 0xffff);
            out.size = dataSize;
            out.dataRva = dataRva;
            out.rawOffset = raw;
            out.sha256 = Sha256Hex(&m_file[raw], dataSize);
            entries.push_back(out);
        }
    }
    return true;
}

bool ResourceProbe::WalkResources(std::vector<ResourceEntry>& entries, std::wstring& errorText)
{
    entries.clear();
    return WalkDirectory(0, 0, L"", L"", entries, errorText);
}

bool ResourceProbe::GetResourceData(const ResourceEntry& entry,
                                    std::vector<BYTE>& data,
                                    std::wstring& errorText) const
{
    data.clear();
    errorText.clear();
    if (!RangeValid(entry.rawOffset, entry.size))
    {
        errorText = L"Resource payload range is no longer valid.";
        return false;
    }
    data.assign(m_file.begin() + entry.rawOffset,
                m_file.begin() + entry.rawOffset + entry.size);
    return true;
}

bool ResourceProbe::Scan(const std::wstring& filePath,
                         std::vector<ResourceEntry>& entries,
                         PeFileInfo& fileInfo,
                         std::wstring& errorText)
{
    entries.clear();
    errorText.clear();
    m_file.clear();
    m_sections.clear();
    m_resourceRva = 0;
    m_resourceRaw = 0;

    if (!ReadFileBytes(filePath, errorText))
        return false;
    if (!ParseHeaders(fileInfo, errorText))
        return false;
    return WalkResources(entries, errorText);
}

// Small self-contained SHA-256 implementation. It deliberately avoids CryptoAPI so
// LunaProbe has no dependency on a particular XP cryptographic-provider revision.
std::wstring ResourceProbe::Sha256Hex(const BYTE* data, size_t size)
{
    static const DWORD k[64] = {
        0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
        0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
        0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
        0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
        0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
        0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
        0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
        0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2
    };

    DWORD h[8] = {0x6a09e667,0xbb67ae85,0x3c6ef372,0xa54ff53a,0x510e527f,0x9b05688c,0x1f83d9ab,0x5be0cd19};
    unsigned __int64 bitLen = static_cast<unsigned __int64>(size) * 8ULL;
    size_t padded = size + 1;
    while ((padded % 64) != 56) ++padded;
    std::vector<BYTE> msg(padded + 8, 0);
    if (size) CopyMemory(&msg[0], data, size);
    msg[size] = 0x80;
    for (int i = 0; i < 8; ++i)
        msg[padded + i] = static_cast<BYTE>((bitLen >> (56 - i * 8)) & 0xff);

    for (size_t off = 0; off < msg.size(); off += 64)
    {
        DWORD w[64];
        for (int i = 0; i < 16; ++i)
        {
            size_t p = off + i * 4;
            w[i] = (static_cast<DWORD>(msg[p]) << 24) |
                   (static_cast<DWORD>(msg[p+1]) << 16) |
                   (static_cast<DWORD>(msg[p+2]) << 8) |
                   static_cast<DWORD>(msg[p+3]);
        }
        for (int i = 16; i < 64; ++i)
        {
            DWORD s0 = Ror(w[i-15],7) ^ Ror(w[i-15],18) ^ (w[i-15] >> 3);
            DWORD s1 = Ror(w[i-2],17) ^ Ror(w[i-2],19) ^ (w[i-2] >> 10);
            w[i] = w[i-16] + s0 + w[i-7] + s1;
        }

        DWORD a=h[0],b=h[1],c=h[2],d=h[3],e=h[4],f=h[5],g=h[6],hh=h[7];
        for (int i = 0; i < 64; ++i)
        {
            DWORD S1 = Ror(e,6) ^ Ror(e,11) ^ Ror(e,25);
            DWORD ch = (e & f) ^ ((~e) & g);
            DWORD t1 = hh + S1 + ch + k[i] + w[i];
            DWORD S0 = Ror(a,2) ^ Ror(a,13) ^ Ror(a,22);
            DWORD maj = (a & b) ^ (a & c) ^ (b & c);
            DWORD t2 = S0 + maj;
            hh=g; g=f; f=e; e=d+t1; d=c; c=b; b=a; a=t1+t2;
        }
        h[0]+=a; h[1]+=b; h[2]+=c; h[3]+=d;
        h[4]+=e; h[5]+=f; h[6]+=g; h[7]+=hh;
    }

    std::wstringstream ss;
    ss << std::hex << std::setfill(L'0');
    for (int i = 0; i < 8; ++i)
        ss << std::setw(8) << h[i];
    return ss.str();
}
