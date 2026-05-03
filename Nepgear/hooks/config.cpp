#include "../pch.h"
#include "config.h"
#include "utils.h"
#include <shlwapi.h>
#include <string>

#pragma comment(lib, "Shlwapi.lib")

namespace Config {

    bool    IsSystemEnabled = true;
    bool    EnableFontHook = false;
    wchar_t FontFileName[MAX_PATH] = { 0 };
    wchar_t ForcedFontNameW[64] = { 0 };
    char    ForcedFontNameA[64] = { 0 };
    DWORD   ForcedCharset = DEFAULT_CHARSET;
    bool    EnableFaceNameReplace = true;
    bool    EnableCharsetReplace = false;
    bool    EnableFontHeightScale = false;
    double  FontHeightScale = 1.0;
    bool    EnableFontWidthScale = false;
    double  FontWidthScale = 1.0;
    bool    EnableFontWeight = false;
    int     FontWeight = 0;

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
    wchar_t LE_Timezone[128] = L"Tokyo Standard Time";

    bool    EnableCodepageSpoof = false;
    DWORD   SpoofFromCharset = 128;
    DWORD   SpoofToCharset = 1;

    bool    EnableCodePageHook = false;
    UINT    FromCodePage = 0;
    UINT    ToCodePage = 0;

    DWORD   DetectedCharset = 1;
    bool    NeedFontReload = false;
    LONG    ConfigVersion = 1;

    bool    EnableDebug = false;
    bool    EnableLogToFile = false;

    bool    EnableMedFix = false;
    bool    EnableMajiroFix = false;

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


        EnableFontHook = GetPrivateProfileIntW(L"Font", L"Enable", 0, ini) != 0;
        GetPrivateProfileStringW(L"Font", L"FileName", L"", FontFileName, MAX_PATH, ini);
        GetPrivateProfileStringW(L"Font", L"FaceName", L"", ForcedFontNameW, 64, ini);
        WCharToChar(ForcedFontNameW, ForcedFontNameA, 64);
        ForcedCharset = GetPrivateProfileIntW(L"Font", L"Charset", DEFAULT_CHARSET, ini);
        EnableFaceNameReplace = GetPrivateProfileIntW(L"Font", L"EnableFaceNameReplace", 1, ini) != 0;
        EnableCharsetReplace  = GetPrivateProfileIntW(L"Font", L"EnableCharsetReplace",  0, ini) != 0;

        EnableFontHeightScale = GetPrivateProfileIntW(L"Font", L"EnableHeightScale", 0, ini) != 0;
        wchar_t scaleBuffer[32];
        GetPrivateProfileStringW(L"Font", L"HeightScale", L"1.0", scaleBuffer, 32, ini);
        FontHeightScale = _wtof(scaleBuffer);
        if (FontHeightScale <= 0.0) FontHeightScale = 1.0;

        EnableFontWidthScale = GetPrivateProfileIntW(L"Font", L"EnableWidthScale", 0, ini) != 0;
        GetPrivateProfileStringW(L"Font", L"WidthScale", L"1.0", scaleBuffer, 32, ini);
        FontWidthScale = _wtof(scaleBuffer);
        if (FontWidthScale <= 0.0) FontWidthScale = 1.0;

        EnableFontWeight = GetPrivateProfileIntW(L"Font", L"EnableWeight", 0, ini) != 0;
        FontWeight = GetPrivateProfileIntW(L"Font", L"Weight", 0, ini);

        EnableCodepageSpoof = GetPrivateProfileIntW(L"Font", L"EnableCodepageSpoof", 0, ini) != 0;
        SpoofFromCharset = GetPrivateProfileIntW(L"Font", L"SpoofFromCharset", 128, ini);
        SpoofToCharset   = GetPrivateProfileIntW(L"Font", L"SpoofToCharset",   1,   ini);


        EnableWindowTitleHook = GetPrivateProfileIntW(L"Window", L"Enable", 0, ini) != 0;
        WindowTitleMode = GetPrivateProfileIntW(L"Window", L"TitleMode", 2, ini);
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
        GetPrivateProfileStringW(L"LocaleEmulator", L"Timezone", L"Tokyo Standard Time", LE_Timezone, 128, ini);


        EnableCodePageHook = GetPrivateProfileIntW(L"CodePage", L"Enable", 0, ini) != 0;
        FromCodePage = GetPrivateProfileIntW(L"CodePage", L"FromCodePage", 0, ini);
        ToCodePage = GetPrivateProfileIntW(L"CodePage", L"ToCodePage", 0, ini);


        EnableDebug    = GetPrivateProfileIntW(L"Debug", L"Enable",    0, ini) != 0;
        EnableLogToFile = GetPrivateProfileIntW(L"Debug", L"LogToFile", 0, ini) != 0;
        

        if (EnableDebug) {
            Utils::Log("[Config] Configuration loaded from: %ls", ini);
            Utils::Log("[Config] Window Title Mode: %d, Custom Title: %ls", WindowTitleMode, CustomTitleW);
        }

        EnableMedFix = GetPrivateProfileIntW(L"GLOBAL", L"MED", 0, ini) != 0;
        EnableMajiroFix = GetPrivateProfileIntW(L"GLOBAL", L"MAJIRO", 0, ini) != 0;
    }
}
