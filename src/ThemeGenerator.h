#pragma once
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <string>
#include <vector>

struct ThemeGenerateResult
{
    unsigned int presetRoles;
    unsigned int matchedRoles;
    unsigned int modifiedResources;
    unsigned int changedStateTargets;
    unsigned int unchangedStateTargets;
    unsigned int skippedIncompatibleTargets;
    unsigned int customBitmapResources;
    unsigned int classicColorsChanged;
    unsigned int classicFontsChanged;
    unsigned int classicMetricsChanged;
    unsigned int verificationResourceCount;

    ThemeGenerateResult() : presetRoles(0), matchedRoles(0), modifiedResources(0),
        changedStateTargets(0), unchangedStateTargets(0), skippedIncompatibleTargets(0),
        customBitmapResources(0), classicColorsChanged(0), classicFontsChanged(0),
        classicMetricsChanged(0), verificationResourceCount(0) {}
};

struct ThemeRoleColor
{
    std::wstring role;
    BYTE r, g, b;
    BYTE r2, g2, b2;
    bool gradient;
    bool bulkSafeOnly;
    bool surfaceFill;
    ThemeRoleColor() : r(0), g(0), b(0), r2(0), g2(0), b2(0),
        gradient(false), bulkSafeOnly(false), surfaceFill(false) {}
};

struct ThemeClassicColor
{
    std::wstring key;
    std::wstring label;
    BYTE r, g, b;
    bool edited;
    ThemeClassicColor() : r(0), g(0), b(0), edited(false) {}
};

struct ThemeClassicFont
{
    std::wstring key;
    std::wstring label;
    std::wstring face;
    unsigned int pointSize;
    bool bold;
    bool italic;
    bool edited;
    ThemeClassicFont() : pointSize(8), bold(false), italic(false), edited(false) {}
};

struct ThemeClassicMetric
{
    std::wstring key;
    std::wstring label;
    int value;
    bool edited;
    ThemeClassicMetric() : value(0), edited(false) {}
};

struct ThemeRoleInfo
{
    std::wstring role;
    unsigned int targetCount;
    std::wstring firstResource;
    std::wstring firstStateName;
    unsigned int firstStateIndex;
    unsigned int imageCount;
    std::wstring imageLayout;

    ThemeRoleInfo() : targetCount(0), firstStateIndex(0), imageCount(0) {}
};



struct ThemeRoleStateInfo
{
    std::wstring resource;
    std::wstring stateName;
    unsigned int stateIndex;
    unsigned int imageCount;
    std::wstring imageLayout;
    std::wstring sizingType;
    std::wstring sizingMargins;
    ThemeRoleStateInfo() : stateIndex(0), imageCount(0) {}
};

struct ThemeSourceCache
{
    std::wstring sourcePath;
    std::wstring textFileName;
    std::vector<ThemeRoleInfo> roles;
    unsigned int bitmapResourceCount;
    unsigned __int64 bitmapBytesLoaded;
    bool ready;
    ThemeSourceCache() : bitmapResourceCount(0), bitmapBytesLoaded(0), ready(false) {}
};

class ThemeGenerator
{
public:
    // Parses/scans the selected source once and keeps semantic role metadata
    // ready for repeated preview operations.
    static bool BuildSourceCache(const std::wstring& sourcePath,
                                 const std::wstring& textFileName,
                                 ThemeSourceCache& cache,
                                 std::wstring& errorText);

    // Enumerates unique semantic editor roles from a Luna color-scheme definition.
    static bool EnumerateRoles(const std::wstring& sourcePath,
                               const std::wstring& textFileName,
                               std::vector<ThemeRoleInfo>& roles,
                               std::wstring& errorText);

    // Creates a 24-bpp preview DIB for the first state/resource represented by role.
    // If color is NULL, the source state is shown without recoloring.
    static bool BuildRolePreviewCached(const ThemeSourceCache& cache,
                                       const std::wstring& role,
                                       const ThemeRoleColor* color,
                                       double hueWindow,
                                       std::vector<BYTE>& previewDib,
                                       ThemeRoleInfo& roleInfo,
                                       std::wstring& errorText);

    static bool EnumerateRoleStates(const ThemeSourceCache& cache,
                                    const std::wstring& role,
                                    std::vector<ThemeRoleStateInfo>& states,
                                    std::wstring& errorText);

    // Builds a standalone state-cell preview. originalSource=true reads from
    // the untouched source cache; false includes custom bitmap replacements.
    static bool BuildRoleStatePreviewCached(const ThemeSourceCache& cache,
                                            const std::wstring& role,
                                            unsigned int stateOrdinal,
                                            const ThemeRoleColor* color,
                                            bool originalSource,
                                            std::vector<BYTE>& previewDib,
                                            ThemeRoleStateInfo& stateInfo,
                                            std::wstring& errorText);

    static bool BuildRolePreview(const std::wstring& sourcePath,
                                 const std::wstring& textFileName,
                                 const std::wstring& role,
                                 const ThemeRoleColor* color,
                                 double hueWindow,
                                 std::vector<BYTE>& previewDib,
                                 ThemeRoleInfo& roleInfo,
                                 std::wstring& errorText);

    // Generates a modified COPY using only the explicitly edited semantic roles.
    static bool GenerateFromColors(const std::wstring& sourcePath,
                                   const std::wstring& textFileName,
                                   const std::vector<ThemeRoleColor>& colors,
                                   const std::wstring& outputPath,
                                   double hueWindow,
                                   ThemeGenerateResult& result,
                                   std::wstring& errorText);

    static bool GenerateFromColorsAndClassic(const std::wstring& sourcePath,
                                             const std::wstring& textFileName,
                                             const std::vector<ThemeRoleColor>& colors,
                                             const std::vector<ThemeClassicColor>& classicColors,
                                             const std::vector<ThemeClassicFont>& classicFonts,
                                             const std::vector<ThemeClassicMetric>& classicMetrics,
                                             const std::wstring& outputPath,
                                             double hueWindow,
                                             ThemeGenerateResult& result,
                                             std::wstring& errorText);

    static bool GetClassicColors(const ThemeSourceCache& cache,
                                 std::vector<ThemeClassicColor>& colors,
                                 std::wstring& errorText);

    static bool GetClassicFonts(const ThemeSourceCache& cache,
                                std::vector<ThemeClassicFont>& fonts,
                                std::wstring& errorText);

    static bool GetClassicMetrics(const ThemeSourceCache& cache,
                                  std::vector<ThemeClassicMetric>& metrics,
                                  std::wstring& errorText);

    // Compatibility path for existing palette preset files.
    static bool GenerateFromPreset(const std::wstring& sourcePath,
                                   const std::wstring& textFileName,
                                   const std::wstring& presetPath,
                                   const std::wstring& outputPath,
                                   double hueWindow,
                                   ThemeGenerateResult& result,
                                   std::wstring& errorText);

    static bool LoadPreset(const std::wstring& presetPath,
                           std::vector<ThemeRoleColor>& colors,
                           std::wstring& errorText);

    static bool SavePreset(const std::wstring& presetPath,
                           const std::vector<ThemeRoleColor>& colors,
                           std::wstring& errorText);


    // Export the currently cached full BITMAP resource as a normal .bmp file.
    static bool ExportBitmapResource(const ThemeSourceCache& cache,
                                     const std::wstring& resourceName,
                                     const std::wstring& bmpPath,
                                     std::wstring& errorText);

    // Exports only the first logical state represented by the selected role.
    static bool ExportBitmapStateForRole(const ThemeSourceCache& cache,
                                         const std::wstring& role,
                                         const std::wstring& bmpPath,
                                         std::wstring& errorText);

    // Replace one full BITMAP resource in the in-memory working set from a
    // normal .bmp file. Width/height must match the original Luna resource.
    static bool ReplaceBitmapResource(const ThemeSourceCache& cache,
                                      const std::wstring& resourceName,
                                      const std::wstring& bmpPath,
                                      std::wstring& errorText);

    // Replaces every compatible BITMAP resource used by a semantic role.
    // This is important for Window.ActiveCaption, which has separate normal,
    // minimized/maximized caption resources.
    static bool ReplaceBitmapForRole(const ThemeSourceCache& cache,
                                     const std::wstring& role,
                                     const std::wstring& bmpPath,
                                     unsigned int& replacedResources,
                                     unsigned int& skippedResources,
                                     std::wstring& errorText);

    static bool HasBitmapOverrides();
    static bool IsBitmapOverridden(const std::wstring& resourceName);
};
