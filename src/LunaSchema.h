#pragma once

#include <string>
#include <vector>
#include "LunaMapper.h"

struct SchemaTarget
{
    std::wstring section;
    std::wstring property;
    std::wstring canonicalResource;
    std::wstring resolvedResource;
    std::wstring editorRole;
    unsigned int stateIndex;
    std::wstring stateName;
    std::wstring confidence;
    unsigned int imageCount;
    std::wstring imageLayout;
    std::wstring sizingType;
    std::wstring sizingMargins;
    bool resourceFound;
    unsigned int sourceLine;
};

struct SchemaValidationItem
{
    std::wstring name;
    bool ok;
    unsigned int matches;
};

struct SchemaValidationReport
{
    unsigned int imageReferences;
    unsigned int resolvedReferences;
    unsigned int stateCells;
    unsigned int confirmedStates;
    unsigned int inferredStates;
    unsigned int genericStates;
    std::vector<SchemaValidationItem> critical;
    bool valid;
};

class LunaSchema
{
public:
    static std::wstring CanonicalResourceKey(const std::wstring& resourceName);
    static std::wstring RefineEditorRole(const StateReference& state);

    static void Build(const std::vector<ImageReference>& images,
                      const std::vector<StateReference>& states,
                      std::vector<SchemaTarget>& output);

    static void Validate(const std::vector<ImageReference>& images,
                         const std::vector<SchemaTarget>& schema,
                         SchemaValidationReport& report);

private:
    static bool StartsWithNoCase(const std::wstring& value, const wchar_t* prefix);
    static bool ContainsNoCase(const std::wstring& value, const wchar_t* needle);
    static bool EqualsNoCase(const std::wstring& a, const std::wstring& b);
};
