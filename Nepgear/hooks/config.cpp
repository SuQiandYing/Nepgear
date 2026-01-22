#include "../pch.h"
#include "config.h"
#include <shlwapi.h>
#include <stdlib.h>
#include <stdio.h>

#pragma comment(lib, "Shlwapi.lib")

namespace Config {
    bool    IsSystemEnabled = true;

    bool    EnableFontHook = true;
    wchar_t FontFileName[MAX_PATH] = L"galgame_cnjp.ttf";
    wchar_t ForcedFontNameW[64] = L"galgame";
    char    ForcedFontNameA[64] = "galgame";
    DWORD   ForcedCharset = 1;
    bool    EnableFaceNameReplace = true;
    bool    EnableCharsetReplace = true;
    bool    EnableFontHeightScale = false;
    double  FontHeightScale = 1.0;
    bool    EnableFontWidthScale = false;
    double  FontWidthScale = 1.0;
    bool    EnableFontWeight = false;
    int     FontWeight = 0;

    bool    EnableWindowTitleHook = false;
    wchar_t CustomTitleW[256] = L"Default Title";
    char    CustomTitleA[256] = "Default Title";

    bool    EnableFileHook = false;
    wchar_t RedirectFolderW[MAX_PATH] = L"Nepgear";
    char    RedirectFolderA[MAX_PATH] = "Nepgear";
    wchar_t ArchiveFileName[MAX_PATH] = L"Nepgear.chs";
    int     VFSMode = 0;

    bool    EnableLE = true;
    UINT    LE_Codepage = 932;
    UINT    LE_LocaleID = 1041;
    UINT    LE_Charset = 128;
    wchar_t LE_Timezone[128] = L"Tokyo Standard Time";

    bool    EnableDebug = false;
    bool    EnableLogToFile = false;

    static void WCharToChar(const wchar_t* src, char* dest, int destSize) {
        WideCharToMultiByte(LE_Codepage, 0, src, -1, dest, destSize, NULL, NULL);
    }

    void LoadConfiguration(HMODULE hModule) {
        wchar_t iniPath[MAX_PATH];
        if (GetModuleFileNameW(hModule, iniPath, MAX_PATH)) {
            PathRemoveFileSpecW(iniPath);
            PathAppendW(iniPath, L"Nepgear.ini");
        }
        else {
            MessageBoxW(NULL, L"无法获取模块路径", L"系统错误", MB_OK | MB_ICONERROR);
            ExitProcess(1);
        }

        if (!PathFileExistsW(iniPath)) {
            wchar_t errorMsg[1024];
            swprintf_s(errorMsg, 1024, L"启动失败：配置文件丢失！\n\n请确保 'Nepgear.ini' 位于以下路径：\n%s", iniPath);
            MessageBoxW(NULL, errorMsg, L"错误", MB_OK | MB_ICONERROR | MB_TOPMOST);
            ExitProcess(1);
        }

        IsSystemEnabled = GetPrivateProfileIntW(L"System", L"Enable", 1, iniPath) != 0;
        if (!IsSystemEnabled) return;

        EnableFontHook = GetPrivateProfileIntW(L"Font", L"Enable", 1, iniPath) != 0;
        GetPrivateProfileStringW(L"Font", L"FileName", FontFileName, FontFileName, MAX_PATH, iniPath);
        GetPrivateProfileStringW(L"Font", L"FaceName", ForcedFontNameW, ForcedFontNameW, 64, iniPath);
        WCharToChar(ForcedFontNameW, ForcedFontNameA, 64);
        ForcedCharset = GetPrivateProfileIntW(L"Font", L"Charset", ForcedCharset, iniPath);
        EnableFaceNameReplace = GetPrivateProfileIntW(L"Font", L"EnableFaceNameReplace", 1, iniPath) != 0;
        EnableCharsetReplace = GetPrivateProfileIntW(L"Font", L"EnableCharsetReplace", 1, iniPath) != 0;

        EnableFontHeightScale = GetPrivateProfileIntW(L"Font", L"EnableHeightScale", 0, iniPath) != 0;
        wchar_t tempScale[32];
        GetPrivateProfileStringW(L"Font", L"HeightScale", L"1.0", tempScale, 32, iniPath);
        FontHeightScale = _wtof(tempScale);
        if (FontHeightScale <= 0.0) FontHeightScale = 1.0;

        EnableFontWidthScale = GetPrivateProfileIntW(L"Font", L"EnableWidthScale", 0, iniPath) != 0;
        wchar_t tempWidthScale[32];
        GetPrivateProfileStringW(L"Font", L"WidthScale", L"1.0", tempWidthScale, 32, iniPath);
        FontWidthScale = _wtof(tempWidthScale);
        if (FontWidthScale <= 0.0) FontWidthScale = 1.0;

        EnableFontWeight = GetPrivateProfileIntW(L"Font", L"EnableWeight", 0, iniPath) != 0;
        FontWeight = GetPrivateProfileIntW(L"Font", L"Weight", FontWeight, iniPath);

        EnableWindowTitleHook = GetPrivateProfileIntW(L"Window", L"Enable", 0, iniPath) != 0;
        GetPrivateProfileStringW(L"Window", L"Title", CustomTitleW, CustomTitleW, 256, iniPath);
        WCharToChar(CustomTitleW, CustomTitleA, 256);

        EnableFileHook = GetPrivateProfileIntW(L"FileRedirect", L"Enable", 0, iniPath) != 0;
        GetPrivateProfileStringW(L"FileRedirect", L"Folder", RedirectFolderW, RedirectFolderW, MAX_PATH, iniPath);
        WCharToChar(RedirectFolderW, RedirectFolderA, MAX_PATH);
        GetPrivateProfileStringW(L"FileRedirect", L"ArchiveFile", ArchiveFileName, ArchiveFileName, MAX_PATH, iniPath);
        VFSMode = GetPrivateProfileIntW(L"FileHook", L"VFSMode", 0, iniPath);

        EnableLE = GetPrivateProfileIntW(L"LocaleEmulator", L"Enable", 1, iniPath) != 0;
        LE_Codepage = GetPrivateProfileIntW(L"LocaleEmulator", L"CodePage", 932, iniPath);
        LE_LocaleID = GetPrivateProfileIntW(L"LocaleEmulator", L"LocaleID", 1041, iniPath);
        LE_Charset = GetPrivateProfileIntW(L"LocaleEmulator", L"Charset", 128, iniPath);
        GetPrivateProfileStringW(L"LocaleEmulator", L"Timezone", L"Tokyo Standard Time", LE_Timezone, 128, iniPath);

        EnableDebug = GetPrivateProfileIntW(L"Debug", L"Enable", 0, iniPath) != 0;
        EnableLogToFile = GetPrivateProfileIntW(L"Debug", L"LogToFile", 0, iniPath) != 0;
    }
}
