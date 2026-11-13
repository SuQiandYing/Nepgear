#include "../pch.h"
#include <windows.h>
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cctype>
#include <cerrno>
#include <cwctype>
#include <cmath>
#include <limits>
#include <unknwn.h>
#include <objidl.h>
#include <gdiplus.h>
#include <intrin.h>
#include <mutex>
#include <process.h>
#include <psapi.h>
#include <shlwapi.h>
#include <stdarg.h>
#include <string>
#include <unordered_map>
#include <vector>
#include <wincrypt.h>
#include <dwrite.h>
#include <commctrl.h>

#include "font_hook.h"
#include "config.h"
#include "utils.h"
#include "hook_policy.h"
#include "internal/font/engines/common/engine_common.h"
#include "internal/font/engines/common/engine_identity_policy.h"
#include "../font/font_patcher.h"
#include "../detours.h"

#ifdef min
#undef min
#endif
#ifdef max
#undef max
#endif

#ifdef _WIN64
#pragma comment(lib, "detours_x64.lib")
#else
#pragma comment(lib, "detours.lib")
#endif
#pragma comment(lib, "gdiplus.lib")
#pragma comment(lib, "psapi.lib")
#pragma comment(lib, "shlwapi.lib")
#pragma comment(lib, "version.lib")
#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "comctl32.lib")

#pragma intrinsic(_ReturnAddress)

// SimpleFontHook's BGI implementation intentionally addresses this probe
// depth from the global namespace. Keep that ABI while the rest of the hook
// implementation remains isolated in AdvancedFontHook.
static __declspec(thread) unsigned int g_internalFontProbeDepth = 0;

namespace AdvancedFontHook {

using namespace Gdiplus;

static bool IsPickerThread();
static std::wstring ArtemisFindSystemFontFileByFace(const wchar_t* faceName);
static bool SoftpalShouldReplaceFontDataQueries();
static bool MiraiShouldReplaceFontDataQueries();
static bool MajiroShouldReplaceFontDataQueries();
static bool DxLibShouldReplaceFontDataQueries();
static bool MajiroShouldDecodeAnsiTextAsCp932Wide();
namespace BgiCompat {
static bool ShouldPreserveLogicalOutlineFontNames();
static bool NormalizeReplacementFontMetrics(const LOGFONTW& sourceLogfont,
    LOGFONTW& replacementLogfont, const void* callerAddress = nullptr);
static bool ShouldCreateAnsiFontThroughWide();
static bool BeginTextOutput(HDC hdc, int* y);
static void EndTextOutput(bool entered);
static void AdjustTextExtentForHdc(HDC hdc, LPSIZE size);
static void AdjustTextMetricForHdc(HDC hdc, LPTEXTMETRICA metric);
static void AdjustTextMetricForHdc(HDC hdc, LPTEXTMETRICW metric);
}
static bool ArtemisLegacyTrySubstituteMultiByteToWideChar(UINT codepage, DWORD flags,
    LPCCH multiByteText, int multiByteCount, LPWSTR wideText, int wideCount, int* result);
static bool SubstituteDecodedTextInPlace(LPWSTR text, int count);
static bool MajiroShouldPreserveDefaultCharset(const LOGFONTW& sourceLogfont);
static bool SoftpalShouldUseNaturalReplacementWidth();

#include "internal/font/font_hooks_state.cppinc"
#include "internal/font/engines/unity/unity_mono.cppinc"
#include "internal/font/engines/unity/unity_tmp.cppinc"
#include "internal/font/font_hooks_font_model.cppinc"
#include "internal/font/font_hooks_runtime.cppinc"
#include "internal/font/font_hooks_codepage_redirect.cppinc"
#include "internal/font/font_hooks_text_substitution.cppinc"

} // namespace AdvancedFontHook

// Compile the imported exporter in its original global EngineCommon namespace
// while binding its GDI calls to Nepgear's detour trampolines.
#define orgCreateFontIndirectW AdvancedFontHook::orgCreateFontIndirectW
#define orgSelectObject AdvancedFontHook::orgSelectObject
#define orgGetFontData AdvancedFontHook::orgGetFontData
#include "internal/font/engines/common/engine_font_export.cppinc"
#undef orgGetFontData
#undef orgSelectObject
#undef orgCreateFontIndirectW

namespace AdvancedFontHook {

#include "internal/font/engines/artemis/artemis_detection_contract.cppinc"
#include "internal/font/engines/artemis_legacy/artemis_legacy_paths.cppinc"
#include "internal/font/engines/artemis_legacy/artemis_legacy_text_substitution.cppinc"
#include "internal/font/engines/artemis_legacy/artemis_legacy_iet_patch.cppinc"
#include "internal/font/engines/artemis_legacy/artemis_legacy_file_hooks.cppinc"
#include "internal/font/engines/artemis_legacy/artemis_legacy_notify.cppinc"
#include "internal/font/engines/artemis/artemis_paths.cppinc"
#include "internal/font/engines/escude/escude_config.cppinc"
#include "internal/font/engines/catsystem2/catsystem2_font.cppinc"
#include "internal/font/engines/mirai/mirai_font_data.cppinc"
#include "internal/font/engines/krkr/krkr_paths.cppinc"
#include "internal/font/engines/entis/entis_font.cppinc"
#include "internal/font/engines/softpal/softpal_default_font.cppinc"
#include "internal/font/engines/majiro/majiro_font_cache.cppinc"
#include "internal/font/engines/dxlib/dxlib_font_cache.cppinc"
#include "internal/font/engines/artemis/artemis_pfs.cppinc"
#include "internal/font/engines/artemis/artemis_table_patch.cppinc"
#include "internal/font/engines/artemis/artemis_cache.cppinc"
#include "internal/font/engines/artemis/artemis_file_hooks.cppinc"
#include "internal/font/engines/tyrano/tyrano_web_fonts.cppinc"
#include "internal/font/engines/renpy/renpy_font.cppinc"
#include "internal/font/engines/yuris/yuris_font.cppinc"
#include "internal/font/engines/engine_file_dispatch.cppinc"
#include "internal/font/engine_file_bridge.cppinc"
#include "internal/font/font_hooks_text_render.cppinc"
#include "internal/font/font_hooks_font_queries.cppinc"
#include "internal/font/font_hooks_font_creation.cppinc"
#include "internal/font/engines/topcat/topcat_font.cppinc"

} // namespace AdvancedFontHook

// The imported BGI files use the original project's global helper ABI. These
// forwarding functions keep those files byte-identical without exposing the
// rest of Nepgear's font state globally.
static bool SameSourceLogFont(const LOGFONTW& left, const LOGFONTW& right) {
    return AdvancedFontHook::SameSourceLogFont(left, right);
}

static bool TryGetReplacementInfo(HFONT font,
    AdvancedFontHook::ReplacementFontInfo* info) {
    return AdvancedFontHook::TryGetReplacementInfo(font, info);
}

namespace AdvancedFontHook {

#include "internal/font/engines/bgi/bgi_compat.cppinc"
#include "internal/font/font_hooks_platform.cppinc"
#include "internal/font/font_hooks_install.cppinc"

} // namespace AdvancedFontHook

static HMODULE g_fontHookModule = NULL;

void SetFontHookModule(HMODULE hModule) {
    g_fontHookModule = hModule;
}

namespace Hooks {
    void InstallPatchProfileHook() {
        AdvancedFontHook::FontHooks::InstallPatchProfileHook();
    }

    void InstallFontHook() {
        AdvancedFontHook::FontHooks::Install(g_fontHookModule);
    }
}

namespace FontHooks {
    void NotifyConfigChanged(LONG version) {
        AdvancedFontHook::FontHooks::NotifyConfigChanged(version);
    }

    void PrepareForProcessExit() {
        AdvancedFontHook::FontHooks::PrepareForProcessExit();
    }
}

namespace FontEngineBridge {
    bool IsRequired() {
        return AdvancedFontHook::EngineFileHooksRequired();
    }

    void SetOriginalFileFunctions(
        void* createFileA, void* createFileW,
        void* getFileAttributesA, void* getFileAttributesW,
        void* getFileAttributesExA, void* getFileAttributesExW,
        void* findFirstFileA, void* findFirstFileW,
        void* findFirstFileExA, void* findFirstFileExW) {
        AdvancedFontHook::SetEngineOriginalFileFunctions(
            createFileA, createFileW,
            getFileAttributesA, getFileAttributesW,
            getFileAttributesExA, getFileAttributesExW,
            findFirstFileA, findFirstFileW,
            findFirstFileExA, findFirstFileExW);
    }

    bool TryCreateFileA(LPCSTR fileName, DWORD desiredAccess, DWORD shareMode,
        LPSECURITY_ATTRIBUTES securityAttributes, DWORD creationDisposition,
        DWORD flagsAndAttributes, HANDLE templateFile, HANDLE* result) {
        return AdvancedFontHook::TryEngineCreateFileA(fileName, desiredAccess, shareMode,
            securityAttributes, creationDisposition, flagsAndAttributes, templateFile, result);
    }

    bool TryCreateFileW(LPCWSTR fileName, DWORD desiredAccess, DWORD shareMode,
        LPSECURITY_ATTRIBUTES securityAttributes, DWORD creationDisposition,
        DWORD flagsAndAttributes, HANDLE templateFile, HANDLE* result) {
        return AdvancedFontHook::TryEngineCreateFileW(fileName, desiredAccess, shareMode,
            securityAttributes, creationDisposition, flagsAndAttributes, templateFile, result);
    }

    bool TryGetFileAttributesA(LPCSTR fileName, DWORD* result) {
        return AdvancedFontHook::TryEngineGetFileAttributesA(fileName, result);
    }

    bool TryGetFileAttributesW(LPCWSTR fileName, DWORD* result) {
        return AdvancedFontHook::TryEngineGetFileAttributesW(fileName, result);
    }

    bool TryGetFileAttributesExA(LPCSTR fileName, GET_FILEEX_INFO_LEVELS infoLevel,
        LPVOID fileInformation, BOOL* result) {
        return AdvancedFontHook::TryEngineGetFileAttributesExA(
            fileName, infoLevel, fileInformation, result);
    }

    bool TryGetFileAttributesExW(LPCWSTR fileName, GET_FILEEX_INFO_LEVELS infoLevel,
        LPVOID fileInformation, BOOL* result) {
        return AdvancedFontHook::TryEngineGetFileAttributesExW(
            fileName, infoLevel, fileInformation, result);
    }

    bool TryFindFirstFileA(LPCSTR fileName, LPWIN32_FIND_DATAA findData, HANDLE* result) {
        return AdvancedFontHook::TryEngineFindFirstFileA(fileName, findData, result);
    }

    bool TryFindFirstFileW(LPCWSTR fileName, LPWIN32_FIND_DATAW findData, HANDLE* result) {
        return AdvancedFontHook::TryEngineFindFirstFileW(fileName, findData, result);
    }

    bool TryFindFirstFileFallbackA(LPCSTR fileName, LPWIN32_FIND_DATAA findData, HANDLE* result) {
        return AdvancedFontHook::TryEngineFindFirstFileFallbackA(fileName, findData, result);
    }

    bool TryFindFirstFileFallbackW(LPCWSTR fileName, LPWIN32_FIND_DATAW findData, HANDLE* result) {
        return AdvancedFontHook::TryEngineFindFirstFileFallbackW(fileName, findData, result);
    }

    bool TryFindFirstFileExA(LPCSTR fileName, FINDEX_INFO_LEVELS infoLevel,
        LPVOID findData, FINDEX_SEARCH_OPS searchOp, LPVOID searchFilter,
        DWORD additionalFlags, HANDLE* result) {
        return AdvancedFontHook::TryEngineFindFirstFileExA(fileName, infoLevel,
            findData, searchOp, searchFilter, additionalFlags, result);
    }

    bool TryFindFirstFileExW(LPCWSTR fileName, FINDEX_INFO_LEVELS infoLevel,
        LPVOID findData, FINDEX_SEARCH_OPS searchOp, LPVOID searchFilter,
        DWORD additionalFlags, HANDLE* result) {
        return AdvancedFontHook::TryEngineFindFirstFileExW(fileName, infoLevel,
            findData, searchOp, searchFilter, additionalFlags, result);
    }

    bool TryFindFirstFileExFallbackA(LPCSTR fileName, FINDEX_INFO_LEVELS infoLevel,
        LPVOID findData, FINDEX_SEARCH_OPS searchOp, HANDLE* result) {
        return AdvancedFontHook::TryEngineFindFirstFileExFallbackA(fileName,
            infoLevel, findData, searchOp, result);
    }

    bool TryFindFirstFileExFallbackW(LPCWSTR fileName, FINDEX_INFO_LEVELS infoLevel,
        LPVOID findData, FINDEX_SEARCH_OPS searchOp, HANDLE* result) {
        return AdvancedFontHook::TryEngineFindFirstFileExFallbackW(fileName,
            infoLevel, findData, searchOp, result);
    }
}
