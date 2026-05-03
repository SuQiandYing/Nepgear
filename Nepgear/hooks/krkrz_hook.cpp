#ifndef FASTCALL
#define FASTCALL __fastcall
#endif

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <objidl.h>

#include "krkrz_sdk/tp_stub.h"
#include "../pch.h"
#include "krkrz_hook.h"
#include "utils.h"
#include "config.h"
#include "../detours.h"

#include <string>
#include <shlwapi.h>


// Original function pointers
static tTJSBinaryStream* (FASTCALL *orgTVPCreateStream)(ttstr* name, tjs_uint32 flags) = nullptr;
static iTVPFunctionExporter* g_exporter = nullptr; // 用于检查 SDK 是否已初始化


// Hook implementation
tTJSBinaryStream* FASTCALL newTVPCreateStream(ttstr* name, tjs_uint32 flags) {
    // Ensure SDK is initialized before using TVP types
    if (!g_exporter || flags != 0 || !name) {
        return orgTVPCreateStream(name, flags);
    }

    try {
        const wchar_t* inpath = name->c_str();
        const wchar_t* inname = nullptr;

        if (wcsstr(inpath, L"arc://")) {
            inname = inpath + 6;
            if (wcsncmp(inname, L"./", 2) == 0) inname += 2;
        }
        else {
            const wchar_t* p = wcsstr(inpath, L".xp3/");
            if (p) inname = p + 5;
        }

        if (inname) {
            // Safely compute redirect path using ttstr
            ttstr name_redirect = ttstr(Config::KrkrzPatchFile) + ttstr(L">") + ttstr(inname);
            ttstr name_full = TVPGetAppPath() + L"/" + name_redirect;

            if (TVPIsExistentStorageNoSearchNoNormalize(name_full)) {
                Utils::LogW(L"[Krkrz] Redirecting: %s -> %s", inpath, name_full.c_str());
                return orgTVPCreateStream(&name_full, flags);
            }
        }
    } catch (...) {
        // Guard against crashes during path redirection
    }

    return orgTVPCreateStream(name, flags);
}

typedef FARPROC (WINAPI* pGetProcAddress)(HMODULE hModule, LPCSTR lpProcName);
static pGetProcAddress g_orgGetProcAddress = GetProcAddress;

typedef HRESULT (__stdcall* pV2Link)(iTVPFunctionExporter* exporter);
static pV2Link g_orgV2Link = nullptr;

HRESULT __stdcall newV2Link(iTVPFunctionExporter* exporter) {
    // Mark SDK as available
    g_exporter = exporter;
    TVPInitImportStub(exporter);
    
    Utils::Log("[Krkrz] V2Link caught, exporter: %p, Stub initialized.", exporter);
    return g_orgV2Link(exporter);
}

FARPROC WINAPI newGetProcAddress(HMODULE hModule, LPCSTR lpProcName) {
    FARPROC res = g_orgGetProcAddress(hModule, lpProcName);
    if (lpProcName && ((uintptr_t)lpProcName > 0xFFFF) && strcmp(lpProcName, "V2Link") == 0) {
        if (!g_orgV2Link) {
            g_orgV2Link = (pV2Link)res;
            DetourTransactionBegin();
            DetourUpdateThread(GetCurrentThread());
            DetourAttach(&(PVOID&)g_orgV2Link, newV2Link);
            DetourTransactionCommit();
        }
    }
    return res;
}

namespace Hooks {
    void InstallKrkrzHook() {
        if (!Config::EnableKrkrzHook) return;

        HMODULE hExe = GetModuleHandleW(NULL);
        
        DetourTransactionBegin();
        DetourUpdateThread(GetCurrentThread());
        DetourAttach(&(PVOID&)g_orgGetProcAddress, newGetProcAddress);
        DetourTransactionCommit();

        const char* TVPCreateStream_sig = "55 8b ec 6a ff 68 ? ? ? ? 64 a1 ? ? ? ? 50 83 ec 5c 53 56 57 a1 ? ? ? ? 33 c5 50 8d 45 f4 64 a3 ? ? ? ? 89 65 f0 89 4d ec c7 45 ? ? ? ? ? e8 ? ? ? ? 8b 4d f4 64 89 0d ? ? ? ? 59 5f 5e 5b 8b e5 5d c3";
        
        PVOID addr = Utils::FindPattern(hExe, TVPCreateStream_sig);
        if (addr) {
            orgTVPCreateStream = (tTJSBinaryStream* (FASTCALL *)(ttstr*, tjs_uint32))addr;
            DetourTransactionBegin();
            DetourUpdateThread(GetCurrentThread());
            DetourAttach(&(PVOID&)orgTVPCreateStream, newTVPCreateStream);
            DetourTransactionCommit();
            Utils::Log("[Krkrz] TVPCreateStream hook installed at %p", addr);
        }
        
        Utils::Log("[Krkrz] Krkrz module initialized.");
    }
}
