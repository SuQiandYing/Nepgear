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
    extern wchar_t CustomTitleW[256];
    extern char    CustomTitleA[256];

    extern bool    EnableFileHook;
    extern wchar_t RedirectFolderW[MAX_PATH];
    extern char    RedirectFolderA[MAX_PATH];
    extern wchar_t ArchiveFileName[MAX_PATH];

    extern bool    EnableLE;
    extern UINT    LE_Codepage;
    extern UINT    LE_Charset;
    extern UINT    LE_LocaleID;
    extern wchar_t LE_Timezone[128];

    extern bool    EnableDebug;
    extern bool    EnableLogToFile;

    namespace AuthorInfo {
        const bool ShowPopup = true;
        const wchar_t* const AUTHOR_IDS[] = {
             L"是幼微鸭mua@ai2.moe (御爱同萌)",
             L"是幼微鸭mua@moyu.moe (鲲补丁站)"
        };
        const int AUTHOR_IDS_COUNT = 2;
        const wchar_t* const ADDITIONAL_NOTES = L"本补丁完全免费发布,严禁倒卖和移植至手机端；\n未经许可，禁止制作成整合包以及转载搬运至其他网站。";
    }

    void LoadConfiguration(HMODULE hModule);
}
