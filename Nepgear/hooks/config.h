#pragma once
#include <windows.h>

namespace Config {

    extern bool    IsSystemEnabled;


    extern bool    EnableFontHook;
    extern wchar_t FontFileName[MAX_PATH];
    extern wchar_t ForcedFontNameW[64];
    extern char    ForcedFontNameA[64];
    extern DWORD   ForcedCharset;
    extern bool    EnableFaceNameReplace;
    extern bool    EnableCharsetReplace;
    extern bool    EnableFontHeightScale;
    extern double  FontHeightScale;
    extern bool    EnableFontWidthScale;
    extern double  FontWidthScale;
    extern bool    EnableFontWeight;
    extern int     FontWeight;


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
    extern bool    EnableMedFix;
    extern bool    EnableMajiroFix;


    extern bool    EnableLE;
    extern UINT    LE_Codepage;
    extern UINT    LE_Charset;
    extern UINT    LE_LocaleID;
    extern wchar_t LE_Timezone[128];


    extern bool    EnableCodepageSpoof;
    extern DWORD   SpoofFromCharset;
    extern DWORD   SpoofToCharset;

    extern bool    EnableCodePageHook;
    extern UINT    FromCodePage;
    extern UINT    ToCodePage;

    extern DWORD   DetectedCharset;
    extern bool    NeedFontReload;
    extern LONG    ConfigVersion;


    extern bool    EnableDebug;
    extern bool    EnableLogToFile;

    extern wchar_t IniFileName[MAX_PATH];

    void LoadConfiguration(HMODULE hModule);
}