#pragma once
#include <windows.h>

namespace Config {

    enum TextSubstitutionModeValue : int {
        TextSubstitutionModeJapaneseTraditional = 0,
        TextSubstitutionModeTraditionalToSimplified = 1,
        TextSubstitutionModeSimplifiedToTraditional = 2,
    };

    enum KrkrNativeDispatchModeValue : int {
        KrkrNativeDispatchAuto = 0,
        KrkrNativeDispatchForce = 1,
        KrkrNativeDispatchDisabled = 2,
    };

    extern bool    IsSystemEnabled;
    extern bool    ShowStartupPopup;


    extern bool    EnableFontHook;
    extern wchar_t FontFileName[MAX_PATH];
    extern wchar_t ForcedFontNameW[64];
    extern wchar_t SourceFontNameW[64];
    extern char    ForcedFontNameA[64];
    extern DWORD   ForcedCharset;
    extern bool    EnableFaceNameReplace;
    extern bool    EnableCharsetReplace;
    extern bool    EnableFontHeightScale;
    extern float   FontHeightScale;
    extern bool    EnableFontWidthScale;
    extern float   FontWidthScale;
    extern bool    EnableFontWeight;
    extern int     FontWeight;
    extern bool    EnableFontCharSpacing;
    extern int     FontCharSpacing;
    extern bool    EnableFontVerticalMetrics;
    extern int     FontAscentPermille;
    extern int     FontDescentPermille;
    extern bool    EnableFontLineSpacing;
    extern int     FontLineSpacing;
    extern bool    EnableCodepageRuntimeReplace;
    extern bool    EnableCodepageRedirect;
    extern UINT    CodepageRedirectFrom;
    extern UINT    CodepageRedirectTo;
    extern bool    EnableTextSubstitution;
    extern int     TextSubstitutionMode;
    extern UINT    TextSubstitutionCodepage;
    extern UINT    YurisAtlasCodepage;

    extern bool    CompatSkipDrawTextA;
    extern bool    CompatSkipFontDataQueries;
    extern bool    CompatSelectObjectTrackedOnly;
    extern bool    CompatHookCreateFontW;
    extern bool    CompatHookCreateFontIndirectW;
    extern bool    CompatHookGetTextFace;

    extern bool    EnableBgiHook;
    extern bool    BgiPatchGdiImports;
    extern bool    BgiClearGlyphCacheOnSwitch;
    extern bool    EnableArtemisHook;
    extern bool    ArtemisPatchTables;
    extern bool    ArtemisRedirectFontFiles;
    extern bool    ArtemisClearFontCacheOnSwitch;
    extern wchar_t ArtemisFontPath[MAX_PATH];
    extern int     ArtemisFontSize;
    extern int     ArtemisRubySize;
    extern bool    EnableKrkrHook;
    extern bool    KrkrDisablePrerenderedFonts;
    extern int     KrkrNativeDispatchMode;
    extern bool    EnableEntisHook;
    extern bool    EntisDisableBitmapFonts;
    extern bool    EntisRefreshFontOnSwitch;
    extern bool    EnableSoftpalHook;
    extern bool    SoftpalDisableDefaultFontDat;
    extern bool    SoftpalForceDefaultOptionToSystemFont;
    extern bool    EnableEscudeHook;
    extern bool    EscudeVirtualFontConfig;
    extern bool    EnableMiraiHook;
    extern bool    MiraiReplaceFontDataQueries;
    extern bool    MiraiRedirectFontFiles;
    extern bool    MiraiPinFontDataSource;
    extern bool    EnableMajiroHook;
    extern bool    MajiroDisableFontCache;
    extern bool    EnableDxLibHook;
    extern bool    DxLibDisableFontCache;
    extern bool    DxLibReplaceFontDataQueries;
    extern bool    DxLibClearRuntimeFontCacheOnSwitch;
    extern wchar_t DxLibCachedFontNameW[LF_FACESIZE];
    extern bool    EnableTinkerBellHook;
    extern bool    EnableCatSystemUnityHook;
    extern bool    EnableTyranoHook;
    extern bool    TyranoRedirectWebFonts;
    extern bool    EnableRenPyHook;
    extern bool    RenPyRedirectFonts;
    extern bool    RenPyRefreshFontOnSwitch;
    extern bool    EnableTopCatHook;


    extern bool    EnableWindowTitleHook;
    extern int     WindowTitleMode;
    extern wchar_t CustomTitleW[256];
    extern char    CustomTitleA[256];


    extern bool    EnableFileHook;
    extern bool    EnableKrkrzHook;
    extern bool    EnableRioShiinaHook;
    extern bool    EnableRioShiinaProcessReg;
    extern bool    EnableRioShiinaProcessDvd;
    extern UINT64  RioShiinaSpecDvdFileSize;
    extern wchar_t RedirectFolderW[MAX_PATH];
    extern wchar_t KrkrzPatchFolder[MAX_PATH];
    extern wchar_t KrkrzPatchFile[MAX_PATH];
    extern char    RedirectFolderA[MAX_PATH];
    extern wchar_t ArchiveFileName[MAX_PATH];
    extern int     VFSMode;

    extern int     RioShiinaMode;
    extern wchar_t RioShiinaArchivesToExtract[1024];
    extern bool    RioShiinaSkipInvalidFileName;
    extern bool    EnableLE;
    extern UINT    LE_Codepage;
    extern UINT    LE_Charset;
    extern UINT    LE_LocaleID;
    extern UINT    LE_RegistryRedirectionMode;
    extern UINT    LE_HookUILanguageMode;
    extern wchar_t LE_Timezone[128];


    extern bool    EnableCodepageSpoof;
    extern DWORD   SpoofFromCharset;
    extern DWORD   SpoofToCharset;

    extern bool    EnableCodePageHook;
    extern UINT    FromCodePage;
    extern UINT    ToCodePage;

    extern DWORD   DetectedCharset;
    extern volatile LONG NeedFontReload;
    extern volatile LONG ConfigVersion;


    extern bool    EnableDebug;
    extern bool    EnableLogToFile;
    extern bool    EnableDebugLog;
    extern int     DebugSlowMs;
    extern int     DebugTraceSampleLimit;
    extern int     DebugPickerThreadLogLimit;

    extern wchar_t IniFileName[MAX_PATH];

    void LoadConfiguration(HMODULE hModule);
}
