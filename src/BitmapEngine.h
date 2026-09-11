#pragma once

#include <windows.h>
#include <string>
#include <vector>

struct BitmapInfoSummary
{
    LONG width;
    LONG height;
    WORD bitCount;
    DWORD compression;
    DWORD headerSize;
    DWORD colorsUsed;
    size_t paletteEntries;
    size_t pixelOffset;
    size_t rowStride;
    size_t pixelBytes;
};

struct BitmapAnalysis
{
    unsigned int paletteEntries;
    unsigned int usedPaletteEntries;
    unsigned int saturatedUsedEntries;
    unsigned long totalPixels;
    unsigned long coloredPixels;
    unsigned int uniqueUsedColors;
};

struct RecolorResult
{
    bool changed;
    unsigned int paletteEntries;
    unsigned int usedPaletteEntries;
    unsigned int changedColors;
    unsigned long changedPixels;
    double sourceHueDegrees;
    double targetHueDegrees;
    std::wstring note;
};

struct BitmapCompareResult
{
    bool compatible;
    unsigned long comparedPixels;
    unsigned long differentPixels;
    unsigned int firstUniqueColors;
    unsigned int secondUniqueColors;
    double averageRgbDistance;
};

class BitmapEngine
{
public:
    static bool ParseDib(const std::vector<BYTE>& dib,
                         BitmapInfoSummary& info,
                         std::wstring& errorText);

    static bool AnalyzeDib(const std::vector<BYTE>& dib,
                           BitmapAnalysis& analysis,
                           std::wstring& errorText);

    static bool WriteBmpFile(const std::wstring& path,
                             const std::vector<BYTE>& dib,
                             std::wstring& errorText);

    static bool LoadBmpFile(const std::wstring& path,
                            std::vector<BYTE>& dib,
                            std::wstring& errorText);

    static bool CompareDibs(const std::vector<BYTE>& first,
                            const std::vector<BYTE>& second,
                            BitmapCompareResult& result,
                            std::wstring& errorText);

    static bool ParseColor(const std::wstring& text,
                           BYTE& r, BYTE& g, BYTE& b,
                           std::wstring& errorText);

    // Source-relative hue rotation. It preserves each affected color's hue
    // offset from the detected Luna base hue, plus its original lightness.
    // Low-saturation neutrals and the magenta key are protected.
    static bool RecolorDominantHue(std::vector<BYTE>& dib,
                                   BYTE targetR, BYTE targetG, BYTE targetB,
                                   double hueWindowDegrees,
                                   RecolorResult& result,
                                   std::wstring& errorText);

    // Bulk-safe recolor for an entire bitmap resource. Unlike state-cell
    // recoloring, this preserves the original bitmap format:
    // - indexed images keep their palette/pixel indices;
    // - 24-bpp images remain 24-bpp;
    // - 32-bpp images retain the original alpha byte.
    // Intended for Color ALL when every state in the resource gets one color.
    static bool RecolorWholeBitmapPreserveFormat(std::vector<BYTE>& dib,
                                                 BYTE targetR, BYTE targetG, BYTE targetB,
                                                 RecolorResult& result,
                                                 std::wstring& errorText);


    // Recolors one logical cell in an image strip. Indexed/RLE resources are
    // promoted to a 24-bpp BI_RGB DIB first so one state can be changed without
    // changing other states that share the same palette entries.
    static bool RecolorStateCell(std::vector<BYTE>& dib,
                                 unsigned int stateIndex,
                                 unsigned int imageCount,
                                 const std::wstring& imageLayout,
                                 BYTE targetR, BYTE targetG, BYTE targetB,
                                 double hueWindowDegrees,
                                 RecolorResult& result,
                                 std::wstring& errorText);

    // Whole-element recolor: all chromatic pixels in the selected state cell
    // are mapped to the target hue while retaining their original shading.
    static bool RecolorWholeStateCell(std::vector<BYTE>& dib,
                                      unsigned int stateIndex,
                                      unsigned int imageCount,
                                      const std::wstring& imageLayout,
                                      BYTE targetR, BYTE targetG, BYTE targetB,
                                      RecolorResult& result,
                                      std::wstring& errorText);

    static bool RecolorWholeStateCellGradient(std::vector<BYTE>& dib,
                                              unsigned int stateIndex,
                                              unsigned int imageCount,
                                              const std::wstring& imageLayout,
                                              unsigned int leftMargin,
                                              unsigned int rightMargin,
                                              BYTE r1, BYTE g1, BYTE b1,
                                              BYTE r2, BYTE g2, BYTE b2,
                                              RecolorResult& result,
                                              std::wstring& errorText);


    // Strong tint mode for button/background artwork. Unlike WholeStateCell,
    // this also colorizes low-saturation pixels (except transparency and true
    // black/white extremes), preventing pale/white button faces after recolor.
    static bool RecolorTintedStateCell(std::vector<BYTE>& dib,
                                       unsigned int stateIndex,
                                       unsigned int imageCount,
                                       const std::wstring& imageLayout,
                                       BYTE targetR, BYTE targetG, BYTE targetB,
                                       RecolorResult& result,
                                       std::wstring& errorText);

    // Strong surface recolor for known-safe background/face resources.
    // Unlike normal recolor, neutral and white pixels are also mapped so
    // originally white Luna surfaces can become genuinely dark.
    static bool RecolorSurfaceStateCell(std::vector<BYTE>& dib,
                                        unsigned int stateIndex,
                                        unsigned int imageCount,
                                        const std::wstring& imageLayout,
                                        BYTE targetR, BYTE targetG, BYTE targetB,
                                        RecolorResult& result,
                                        std::wstring& errorText);


    static bool RecolorTintedStateCellGradient(std::vector<BYTE>& dib,
                                               unsigned int stateIndex,
                                               unsigned int imageCount,
                                               const std::wstring& imageLayout,
                                               unsigned int leftMargin,
                                               unsigned int rightMargin,
                                               BYTE r1, BYTE g1, BYTE b1,
                                               BYTE r2, BYTE g2, BYTE b2,
                                               RecolorResult& result,
                                               std::wstring& errorText);

    // Expands the logical width of every state in a stretch bitmap while
    // preserving left/right fixed caps and resampling the center.
    // Used by gradient captions to provide XP with more color samples.
    static bool ExpandStretchWidth(std::vector<BYTE>& dib,
                                   unsigned int imageCount,
                                   const std::wstring& imageLayout,
                                   unsigned int leftMargin,
                                   unsigned int rightMargin,
                                   unsigned int targetCellWidth,
                                   std::wstring& errorText);





    // Extracts one logical state from a multi-state strip as a standalone
    // 24-bpp BI_RGB DIB suitable for editing in Paint.
    static bool ExtractStateCell(const std::vector<BYTE>& sourceDib,
                                 unsigned int stateIndex,
                                 unsigned int imageCount,
                                 const std::wstring& imageLayout,
                                 std::vector<BYTE>& cellDib,
                                 std::wstring& errorText);

    // Copies a standalone bitmap into one logical state cell only. Other cells
    // in the strip are preserved.
    static bool ReplaceStateCell(std::vector<BYTE>& destinationDib,
                                 unsigned int stateIndex,
                                 unsigned int imageCount,
                                 const std::wstring& imageLayout,
                                 const std::vector<BYTE>& cellDib,
                                 std::wstring& errorText);

    static bool ConvertTo24Bpp(std::vector<BYTE>& dib,
                               std::wstring& errorText);

private:
    static bool IsMagentaKey(BYTE r, BYTE g, BYTE b);
};
