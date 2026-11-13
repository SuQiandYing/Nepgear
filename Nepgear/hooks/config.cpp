#include "../pch.h"
#include "config.h"
#include "utils.h"
#include <shlwapi.h>
#include <string>

#pragma comment(lib, "Shlwapi.lib")

namespace Config {

    bool    IsSystemEnabled = true;
    bool    ShowStartupPopup = true;
    bool    EnableFontHook = false;
    wchar_t FontFileName[MAX_PATH] = { 0 };
    wchar_t ForcedFontNameW[64] = { 0 };
    wchar_t SourceFontNameW[64] = { 0 };
    char    ForcedFontNameA[64] = { 0 };
    DWORD   ForcedCharset = DEFAULT_CHARSET;
    bool    EnableFaceNameReplace = true;
    bool    EnableCharsetReplace = false;
    bool    EnableFontHeightScale = false;
    float   FontHeightScale = 1.0f;
    bool    EnableFontWidthScale = false;
    float   FontWidthScale = 1.0f;
    bool    EnableFontWeight = false;
    int     FontWeight = 0;
    bool    EnableFontCharSpacing = false;
    int     FontCharSpacing = 0;
    bool    EnableFontVerticalMetrics = false;
    int     FontAscentPermille = 880;
    int     FontDescentPermille = -120;
    bool    EnableFontLineSpacing = false;
    int     FontLineSpacing = 0;
    bool    EnableCodepageRuntimeReplace = false;
    bool    EnableCodepageRedirect = false;
    UINT    CodepageRedirectFrom = 932;
    UINT    CodepageRedirectTo = CP_UTF8;
    bool    EnableTextSubstitution = false;
    int     TextSubstitutionMode = TextSubstitutionModeJapaneseTraditional;
    UINT    TextSubstitutionCodepage = 932;
    UINT    YurisAtlasCodepage = 0;

    bool    CompatSkipDrawTextA = true;
    bool    CompatSkipFontDataQueries = true;
    bool    CompatSelectObjectTrackedOnly = false;
    bool    CompatHookCreateFontW = true;
    bool    CompatHookCreateFontIndirectW = true;
    bool    CompatHookGetTextFace = false;

    bool    EnableBgiHook = false;
    bool    BgiPatchGdiImports = true;
    bool    BgiClearGlyphCacheOnSwitch = true;
    bool    EnableArtemisHook = false;
    bool    ArtemisPatchTables = true;
    bool    ArtemisRedirectFontFiles = true;
    bool    ArtemisClearFontCacheOnSwitch = true;
    wchar_t ArtemisFontPath[MAX_PATH] = { 0 };
    int     ArtemisFontSize = 0;
    int     ArtemisRubySize = -1;
    bool    EnableKrkrHook = false;
    bool    KrkrDisablePrerenderedFonts = true;
    int     KrkrNativeDispatchMode = KrkrNativeDispatchAuto;
    bool    EnableEntisHook = false;
    bool    EntisDisableBitmapFonts = true;
    bool    EntisRefreshFontOnSwitch = true;
    bool    EnableSoftpalHook = false;
    bool    SoftpalDisableDefaultFontDat = true;
    bool    SoftpalForceDefaultOptionToSystemFont = true;
    bool    EnableEscudeHook = false;
    bool    EscudeVirtualFontConfig = true;
    bool    EnableMiraiHook = false;
    bool    MiraiReplaceFontDataQueries = true;
    bool    MiraiRedirectFontFiles = true;
    bool    MiraiPinFontDataSource = true;
    bool    EnableMajiroHook = false;
    bool    MajiroDisableFontCache = true;
    bool    EnableDxLibHook = false;
    bool    DxLibDisableFontCache = false;
    bool    DxLibReplaceFontDataQueries = true;
    bool    DxLibClearRuntimeFontCacheOnSwitch = false;
    wchar_t DxLibCachedFontNameW[LF_FACESIZE] = { 0 };
    bool    EnableTinkerBellHook = false;
    bool    EnableCatSystemUnityHook = false;
    bool    EnableTyranoHook = false;
    bool    TyranoRedirectWebFonts = true;
    bool    EnableRenPyHook = false;
    bool    RenPyRedirectFonts = true;
    bool    RenPyRefreshFontOnSwitch = true;
    bool    EnableTopCatHook = false;

    bool    EnableWindowTitleHook = false;
    int     WindowTitleMode = 2;
    wchar_t CustomTitleW[256] = { 0 };
    char    CustomTitleA[256] = { 0 };

    bool    EnableFileHook = false;
    bool    EnableKrkrzHook = false;
    bool    EnableRioShiinaHook = false;
    bool    EnableRioShiinaProcessReg = true;
    bool    EnableRioShiinaProcessDvd = false;
    UINT64  RioShiinaSpecDvdFileSize = 0;
    wchar_t RedirectFolderW[MAX_PATH] = L"Nepgear";
    wchar_t KrkrzPatchFolder[MAX_PATH] = L"patch";
    wchar_t KrkrzPatchFile[MAX_PATH] = L"patch.xp3";
    char    RedirectFolderA[MAX_PATH] = "Nepgear";
    wchar_t ArchiveFileName[MAX_PATH] = L"Nepgear.chs";
    int     VFSMode = 0;
    int     RioShiinaMode = 1;
    wchar_t RioShiinaArchivesToExtract[1024] = { 0 };
    bool    RioShiinaSkipInvalidFileName = true;

    bool    EnableLE = false;
    UINT    LE_Codepage = 932;
    UINT    LE_Charset = 128;
    UINT    LE_LocaleID = 1041;
    UINT    LE_RegistryRedirectionMode = 2;
    UINT    LE_HookUILanguageMode = 1;
    wchar_t LE_Timezone[128] = L"Tokyo Standard Time";

    bool    EnableCodepageSpoof = false;
    DWORD   SpoofFromCharset = 128;
    DWORD   SpoofToCharset = 1;

    bool    EnableCodePageHook = false;
    UINT    FromCodePage = 0;
    UINT    ToCodePage = 0;

    DWORD   DetectedCharset = 1;
    volatile LONG NeedFontReload = 0;
    volatile LONG ConfigVersion = 1;

    bool    EnableDebug = false;
    bool    EnableLogToFile = false;
    bool    EnableDebugLog = false;
    int     DebugSlowMs = 50;
    int     DebugTraceSampleLimit = 0;
    int     DebugPickerThreadLogLimit = 0;

    wchar_t IniFileName[MAX_PATH] = L"Nepgear.ini";


    static void WCharToChar(const wchar_t* src, char* dest, int destSize) {
        WideCharToMultiByte(LE_Codepage, 0, src, -1, dest, destSize, NULL, NULL);
    }

    void LoadConfiguration(HMODULE hModule) {
        wchar_t iniPath[MAX_PATH];

        if (!GetModuleFileNameW(hModule, iniPath, MAX_PATH)) {
            MessageBoxW(NULL, L"无法获取模块路径", L"系统错误", MB_OK | MB_ICONERROR);
            ExitProcess(1);
        }

        PathRemoveFileSpecW(iniPath);
        PathAppendW(iniPath, IniFileName);

        if (!PathFileExistsW(iniPath)) {
            wchar_t errorMsg[1024];
            swprintf_s(errorMsg, 1024,
                L"启动失败：配置文件丢失！\n\n请确保 '%s' 位于以下路径：\n%s",
                IniFileName, iniPath);
            MessageBoxW(NULL, errorMsg, L"错误", MB_OK | MB_ICONERROR | MB_TOPMOST);
            ExitProcess(1);
        }

        const wchar_t* ini = iniPath;


        IsSystemEnabled = GetPrivateProfileIntW(L"System", L"Enable", 1, ini) != 0;
        if (!IsSystemEnabled) return;
        ShowStartupPopup = GetPrivateProfileIntW(L"System", L"ShowStartupPopup", 1, ini) != 0;


        EnableFontHook = GetPrivateProfileIntW(L"Font", L"Enable", 0, ini) != 0;
        GetPrivateProfileStringW(L"Font", L"FileName", L"", FontFileName, MAX_PATH, ini);
        GetPrivateProfileStringW(L"Font", L"FaceName", L"", ForcedFontNameW, 64, ini);
        wcsncpy_s(SourceFontNameW, ForcedFontNameW, _TRUNCATE);
        WCharToChar(ForcedFontNameW, ForcedFontNameA, 64);
        ForcedCharset = GetPrivateProfileIntW(L"Font", L"Charset", DEFAULT_CHARSET, ini);
        EnableFaceNameReplace = GetPrivateProfileIntW(L"Font", L"EnableFaceNameReplace", 1, ini) != 0;
        EnableCharsetReplace  = GetPrivateProfileIntW(L"Font", L"EnableCharsetReplace",  0, ini) != 0;

        EnableFontHeightScale = GetPrivateProfileIntW(L"Font", L"EnableHeightScale", 0, ini) != 0;
        wchar_t scaleBuffer[32];
        GetPrivateProfileStringW(L"Font", L"HeightScale", L"1.0", scaleBuffer, 32, ini);
        FontHeightScale = (float)_wtof(scaleBuffer);
        if (FontHeightScale <= 0.0f) FontHeightScale = 1.0f;

        EnableFontWidthScale = GetPrivateProfileIntW(L"Font", L"EnableWidthScale", 0, ini) != 0;
        GetPrivateProfileStringW(L"Font", L"WidthScale", L"1.0", scaleBuffer, 32, ini);
        FontWidthScale = (float)_wtof(scaleBuffer);
        if (FontWidthScale <= 0.0f) FontWidthScale = 1.0f;

        EnableFontWeight = GetPrivateProfileIntW(L"Font", L"EnableWeight", 0, ini) != 0;
        FontWeight = GetPrivateProfileIntW(L"Font", L"Weight", 0, ini);

        EnableFontCharSpacing = GetPrivateProfileIntW(L"Font", L"EnableCharSpacing", 0, ini) != 0;
        FontCharSpacing = GetPrivateProfileIntW(L"Font", L"CharSpacing", 0, ini);
        EnableFontVerticalMetrics = GetPrivateProfileIntW(L"Font", L"EnableVerticalMetrics", 0, ini) != 0;
        FontAscentPermille = GetPrivateProfileIntW(L"Font", L"AscentPermille", 880, ini);
        FontDescentPermille = GetPrivateProfileIntW(L"Font", L"DescentPermille", -120, ini);
        EnableFontLineSpacing = GetPrivateProfileIntW(L"Font", L"EnableLineSpacing", 0, ini) != 0;
        FontLineSpacing = GetPrivateProfileIntW(L"Font", L"LineSpacing", 0, ini);
        EnableCodepageRuntimeReplace = GetPrivateProfileIntW(L"Font", L"EnableCodepageRuntimeReplace", 0, ini) != 0;
        EnableTextSubstitution = GetPrivateProfileIntW(L"Font", L"EnableTextSubstitution", 0, ini) != 0;
        TextSubstitutionMode = GetPrivateProfileIntW(L"Font", L"TextSubstitutionMode",
            TextSubstitutionModeJapaneseTraditional, ini);
        if (TextSubstitutionMode < TextSubstitutionModeJapaneseTraditional ||
            TextSubstitutionMode > TextSubstitutionModeSimplifiedToTraditional) {
            TextSubstitutionMode = TextSubstitutionModeJapaneseTraditional;
        }
        TextSubstitutionCodepage = GetPrivateProfileIntW(L"Font", L"TextSubstitutionCodepage", 932, ini);

        EnableCodepageSpoof = GetPrivateProfileIntW(L"Font", L"EnableCodepageSpoof", 0, ini) != 0;
        SpoofFromCharset = GetPrivateProfileIntW(L"Font", L"SpoofFromCharset", 128, ini);
        SpoofToCharset   = GetPrivateProfileIntW(L"Font", L"SpoofToCharset",   1,   ini);


        EnableWindowTitleHook = GetPrivateProfileIntW(L"Window", L"Enable", 0, ini) != 0;
        WindowTitleMode = GetPrivateProfileIntW(L"Window", L"TitleMode", 2, ini);
        if (WindowTitleMode < 0 || WindowTitleMode > 2) WindowTitleMode = 2;
        GetPrivateProfileStringW(L"Window", L"Title", L"", CustomTitleW, 256, ini);
        WCharToChar(CustomTitleW, CustomTitleA, 256);


        EnableFileHook = GetPrivateProfileIntW(L"FileRedirect", L"Enable", 0, ini) != 0;
        GetPrivateProfileStringW(L"FileRedirect", L"Folder", L"Nepgear", RedirectFolderW, MAX_PATH, ini);
        WCharToChar(RedirectFolderW, RedirectFolderA, MAX_PATH);
        GetPrivateProfileStringW(L"FileRedirect", L"ArchiveFile", L"Nepgear.chs", ArchiveFileName, MAX_PATH, ini);

        VFSMode = GetPrivateProfileIntW(L"FileHook", L"VFSMode", 0, ini);

        EnableKrkrzHook = GetPrivateProfileIntW(L"GLOBAL", L"EnableKrkrz", 0, ini) != 0;
        GetPrivateProfileStringW(L"GLOBAL", L"KrkrzPatchFile", L"patch.xp3", KrkrzPatchFile, MAX_PATH, ini);
        
        EnableRioShiinaHook = GetPrivateProfileIntW(L"GLOBAL", L"EnableRioShiina", 0, ini) != 0;
        RioShiinaMode = GetPrivateProfileIntW(L"GLOBAL", L"RioShiinaMode", 1, ini);
        EnableRioShiinaProcessReg = GetPrivateProfileIntW(L"GLOBAL", L"RioShiinaProcessReg", 1, ini) != 0;
        EnableRioShiinaProcessDvd = GetPrivateProfileIntW(L"GLOBAL", L"RioShiinaProcessDvd", 0, ini) != 0;
        {
            wchar_t dvdSizeBuf[64] = {0};
            GetPrivateProfileStringW(L"GLOBAL", L"RioShiinaSpecDvdFileSize", L"0", dvdSizeBuf, 64, ini);
            RioShiinaSpecDvdFileSize = _wcstoui64(dvdSizeBuf, nullptr, 10);
        }
        GetPrivateProfileStringW(L"GLOBAL", L"RioShiinaArchivesToExtract", L"", RioShiinaArchivesToExtract, 1024, ini);
        RioShiinaSkipInvalidFileName = GetPrivateProfileIntW(L"GLOBAL", L"RioShiinaSkipInvalidFileName", 1, ini) != 0;


        EnableLE    = GetPrivateProfileIntW(L"LocaleEmulator", L"Enable",   0,    ini) != 0;
        LE_Codepage = GetPrivateProfileIntW(L"LocaleEmulator", L"CodePage", 932,  ini);
        LE_LocaleID = GetPrivateProfileIntW(L"LocaleEmulator", L"LocaleID", 1041, ini);
        LE_Charset  = GetPrivateProfileIntW(L"LocaleEmulator", L"Charset",  128,  ini);
        LE_RegistryRedirectionMode = GetPrivateProfileIntW(
            L"LocaleEmulator", L"RegistryRedirectionMode", 2, ini);
        LE_HookUILanguageMode = GetPrivateProfileIntW(
            L"LocaleEmulator", L"HookUILanguageMode", 1, ini);
        if (LE_RegistryRedirectionMode > 2) LE_RegistryRedirectionMode = 2;
        if (LE_HookUILanguageMode > 2) LE_HookUILanguageMode = 1;
        GetPrivateProfileStringW(L"LocaleEmulator", L"Timezone", L"Tokyo Standard Time", LE_Timezone, 128, ini);


        EnableCodePageHook = GetPrivateProfileIntW(L"CodePage", L"Enable", 0, ini) != 0;
        FromCodePage = GetPrivateProfileIntW(L"CodePage", L"FromCodePage", 0, ini);
        ToCodePage = GetPrivateProfileIntW(L"CodePage", L"ToCodePage", 0, ini);
        EnableCodepageRedirect = EnableCodePageHook;
        CodepageRedirectFrom = FromCodePage;
        CodepageRedirectTo = ToCodePage;

        CompatSkipDrawTextA = GetPrivateProfileIntW(L"FontCompatibility", L"SkipDrawTextA", 1, ini) != 0;
        CompatSkipFontDataQueries = GetPrivateProfileIntW(L"FontCompatibility", L"SkipFontDataQueries", 1, ini) != 0;
        CompatSelectObjectTrackedOnly = GetPrivateProfileIntW(L"FontCompatibility", L"SelectObjectTrackedOnly", 0, ini) != 0;
        CompatHookCreateFontW = GetPrivateProfileIntW(L"FontCompatibility", L"HookCreateFontW", 1, ini) != 0;
        CompatHookCreateFontIndirectW = GetPrivateProfileIntW(L"FontCompatibility", L"HookCreateFontIndirectW", 1, ini) != 0;
        CompatHookGetTextFace = GetPrivateProfileIntW(L"FontCompatibility", L"HookGetTextFace", 0, ini) != 0;

        EnableBgiHook = GetPrivateProfileIntW(L"FontEngines", L"EnableBgiHook", 0, ini) != 0;
        BgiPatchGdiImports = GetPrivateProfileIntW(L"FontEngines", L"BgiPatchGdiImports", 1, ini) != 0;
        BgiClearGlyphCacheOnSwitch = GetPrivateProfileIntW(L"FontEngines", L"BgiClearGlyphCacheOnSwitch", 1, ini) != 0;
        EnableArtemisHook = GetPrivateProfileIntW(L"FontEngines", L"EnableArtemis", 0, ini) != 0;
        ArtemisPatchTables = GetPrivateProfileIntW(L"FontEngines", L"ArtemisPatchTables", 1, ini) != 0;
        ArtemisRedirectFontFiles = GetPrivateProfileIntW(L"FontEngines", L"ArtemisRedirectFontFiles", 1, ini) != 0;
        ArtemisClearFontCacheOnSwitch = GetPrivateProfileIntW(L"FontEngines", L"ArtemisClearFontCacheOnSwitch", 1, ini) != 0;
        GetPrivateProfileStringW(L"FontEngines", L"ArtemisFontPath", L"", ArtemisFontPath, MAX_PATH, ini);
        ArtemisFontSize = GetPrivateProfileIntW(L"FontEngines", L"ArtemisFontSize", 0, ini);
        ArtemisRubySize = GetPrivateProfileIntW(L"FontEngines", L"ArtemisRubySize", -1, ini);
        EnableKrkrHook = GetPrivateProfileIntW(L"FontEngines", L"EnableKrkr", 0, ini) != 0;
        KrkrDisablePrerenderedFonts = GetPrivateProfileIntW(L"FontEngines", L"KrkrDisablePrerenderedFonts", 1, ini) != 0;
        KrkrNativeDispatchMode = GetPrivateProfileIntW(L"FontEngines", L"KrkrNativeDispatchMode",
            KrkrNativeDispatchAuto, ini);
        if (KrkrNativeDispatchMode < KrkrNativeDispatchAuto ||
            KrkrNativeDispatchMode > KrkrNativeDispatchDisabled) {
            KrkrNativeDispatchMode = KrkrNativeDispatchAuto;
        }
        EnableEntisHook = GetPrivateProfileIntW(L"FontEngines", L"EnableEntisHook", 0, ini) != 0;
        EntisDisableBitmapFonts = GetPrivateProfileIntW(L"FontEngines", L"EntisDisableBitmapFonts", 1, ini) != 0;
        EntisRefreshFontOnSwitch = GetPrivateProfileIntW(L"FontEngines", L"EntisRefreshFontOnSwitch", 1, ini) != 0;
        EnableSoftpalHook = GetPrivateProfileIntW(L"FontEngines", L"EnableSoftpal", 0, ini) != 0;
        SoftpalDisableDefaultFontDat = GetPrivateProfileIntW(L"FontEngines", L"SoftpalDisableDefaultFontDat", 1, ini) != 0;
        SoftpalForceDefaultOptionToSystemFont = GetPrivateProfileIntW(L"FontEngines", L"SoftpalForceDefaultOptionToSystemFont", 1, ini) != 0;
        EnableEscudeHook = GetPrivateProfileIntW(L"FontEngines", L"EnableEscude", 0, ini) != 0;
        EscudeVirtualFontConfig = GetPrivateProfileIntW(L"FontEngines", L"EscudeVirtualFontConfig", 1, ini) != 0;
        EnableMiraiHook = GetPrivateProfileIntW(L"FontEngines", L"EnableMirai", 0, ini) != 0;
        MiraiReplaceFontDataQueries = GetPrivateProfileIntW(L"FontEngines", L"MiraiReplaceFontDataQueries", 1, ini) != 0;
        MiraiRedirectFontFiles = GetPrivateProfileIntW(L"FontEngines", L"MiraiRedirectFontFiles", 1, ini) != 0;
        MiraiPinFontDataSource = GetPrivateProfileIntW(L"FontEngines", L"MiraiPinFontDataSource", 1, ini) != 0;
        EnableMajiroHook = GetPrivateProfileIntW(L"FontEngines", L"EnableMajiro", 0, ini) != 0;
        MajiroDisableFontCache = GetPrivateProfileIntW(L"FontEngines", L"MajiroDisableFontCache", 1, ini) != 0;
        EnableDxLibHook = GetPrivateProfileIntW(L"FontEngines", L"EnableDxLib", 0, ini) != 0;
        DxLibDisableFontCache = GetPrivateProfileIntW(L"FontEngines", L"DxLibDisableFontCache", 0, ini) != 0;
        DxLibReplaceFontDataQueries = GetPrivateProfileIntW(L"FontEngines", L"DxLibReplaceFontDataQueries", 1, ini) != 0;
        DxLibClearRuntimeFontCacheOnSwitch = GetPrivateProfileIntW(L"FontEngines", L"DxLibClearRuntimeFontCacheOnSwitch", 0, ini) != 0;
        GetPrivateProfileStringW(L"FontEngines", L"DxLibCachedFontName", L"", DxLibCachedFontNameW, LF_FACESIZE, ini);
        EnableTinkerBellHook = GetPrivateProfileIntW(L"FontEngines", L"EnableTinkerBell", 0, ini) != 0;
        EnableCatSystemUnityHook = GetPrivateProfileIntW(L"FontEngines", L"EnableCatSystemUnityHook", 0, ini) != 0;
        EnableTyranoHook = GetPrivateProfileIntW(L"FontEngines", L"EnableTyranoHook", 0, ini) != 0;
        TyranoRedirectWebFonts = GetPrivateProfileIntW(L"FontEngines", L"TyranoRedirectWebFonts", 1, ini) != 0;
        EnableRenPyHook = GetPrivateProfileIntW(L"FontEngines", L"EnableRenPyHook", 0, ini) != 0;
        RenPyRedirectFonts = GetPrivateProfileIntW(L"FontEngines", L"RenPyRedirectFonts", 1, ini) != 0;
        RenPyRefreshFontOnSwitch = GetPrivateProfileIntW(L"FontEngines", L"RenPyRefreshFontOnSwitch", 1, ini) != 0;
        EnableTopCatHook = GetPrivateProfileIntW(L"FontEngines", L"EnableTopCatHook", 0, ini) != 0;
        {
            int codepage = GetPrivateProfileIntW(L"FontEngines", L"YurisAtlasCodepage", 0, ini);
            YurisAtlasCodepage = codepage > 0 && IsValidCodePage((UINT)codepage)
                ? (UINT)codepage : 0;
        }


        EnableDebug    = GetPrivateProfileIntW(L"Debug", L"Enable",    0, ini) != 0;
        EnableLogToFile = GetPrivateProfileIntW(L"Debug", L"LogToFile", 0, ini) != 0;
        EnableDebugLog = GetPrivateProfileIntW(L"Debug", L"EnableFontTrace", EnableDebug ? 1 : 0, ini) != 0;
        DebugSlowMs = GetPrivateProfileIntW(L"Debug", L"FontSlowMs", 50, ini);
        DebugTraceSampleLimit = GetPrivateProfileIntW(L"Debug", L"FontTraceSampleLimit", 0, ini);
        DebugPickerThreadLogLimit = 0;
        

        if (EnableDebug) {
            Utils::Log("[Config] Configuration loaded from: %ls", ini);
            Utils::Log("[Config] Window Title Mode: %d, Custom Title: %ls", WindowTitleMode, CustomTitleW);
        }

    }
}
