#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#ifdef PROBE_INTERFACE_CDECL
#define TJS_INTERFACE __cdecl
#else
#define TJS_INTERFACE __stdcall
#endif

// This fixture implements the public exporter/dispatch contract, with explicit
// interface calling conventions. It does not model private game object layouts.
struct Object {
    void** vtable;
    ULONG references;
    int kind;
};
static Object g_global{}, g_font{}, g_map{}, g_unmap{}, g_probe{};
static LONG g_mapCalls = 0, g_unmapCalls = 0;
static LONG g_lastCount = -1;
static void* g_lastInstance = nullptr;
static void* g_lastResult = nullptr;
static bool g_exportArgumentsValid = true;
static volatile char g_marker[] = "mapPrerenderedFont";

static ULONG TJS_INTERFACE AddRef(void* object) {
    return ++static_cast<Object*>(object)->references;
}
static ULONG TJS_INTERFACE Release(void* object) {
    return --static_cast<Object*>(object)->references;
}

static __declspec(noinline) LONG TJS_INTERFACE NativeCall(void* method,
    ULONG, const wchar_t*, ULONG*, void* result, LONG count,
    void**, void* instance) {
    if (method == &g_map) ++g_mapCalls;
    if (method == &g_unmap) ++g_unmapCalls;
    g_lastCount = count;
    g_lastInstance = instance;
    g_lastResult = result;
    if (result) *static_cast<LONG*>(result) = method == &g_unmap ? 77 : 22;
    return method == &g_unmap ? 77 : 22;
}

static LONG TJS_INTERFACE PropGet(void* owner, ULONG flags,
    const wchar_t* name, ULONG*, void* result, void* instance) {
    g_exportArgumentsValid = g_exportArgumentsValid && flags == 0x800 && instance == owner;
    Object* value = nullptr;
    if (owner == &g_global && wcscmp(name, L"Font") == 0) value = &g_font;
    if (owner == &g_font && wcscmp(name, L"mapPrerenderedFont") == 0) value = &g_map;
    if (owner == &g_font && wcscmp(name, L"unmapPrerenderedFont") == 0) value = &g_unmap;
    *static_cast<Object**>(result) = value;
    if (value) AddRef(value);
    return value ? 0 : -1;
}

// Export helper stubs remain stdcall even in a cdecl-interface engine.
static void* __stdcall GetGlobal() { AddRef(&g_global); return &g_global; }
static void __stdcall VariantConstruct(void* storage) { *static_cast<Object**>(storage) = nullptr; }
static void __stdcall VariantClear(void* storage) {
    Object*& object = *static_cast<Object**>(storage);
    if (object) Release(object);
    object = nullptr;
}
static LONG __stdcall VariantType(const void* storage) {
    return *static_cast<Object* const*>(storage) ? 1 : 0;
}
static void* __stdcall VariantObject(const void* storage) {
    return *static_cast<Object* const*>(storage);
}
using NativeCallback = LONG (__stdcall*)(void*, LONG, void**, void*);
static void* __stdcall CreateMethod(NativeCallback) { AddRef(&g_probe); return &g_probe; }

static bool TJS_INTERFACE QueryFunctions(void*, const char** names,
    void** functions, ULONG count) {
    static const char* expected[] = {
        "iTJSDispatch2 * ::TVPGetScriptDispatch()",
        "tTJSVariant::tTJSVariant()",
        "void tTJSVariant::Clear()",
        "tTJSVariantType tTJSVariant::Type() const",
        "iTJSDispatch2 * tTJSVariant::AsObjectNoAddRef() const",
        "tTJSNativeClassMethod * ::TJSCreateNativeClassMethod(tTJSNativeClassMethodCallback)"
    };
    void* targets[] = {
        reinterpret_cast<void*>(&GetGlobal), reinterpret_cast<void*>(&VariantConstruct),
        reinterpret_cast<void*>(&VariantClear), reinterpret_cast<void*>(&VariantType),
        reinterpret_cast<void*>(&VariantObject), reinterpret_cast<void*>(&CreateMethod)
    };
    if (count != _countof(expected)) return false;
    for (ULONG index = 0; index < count; ++index) {
        if (strcmp(names[index], expected[index]) != 0) return false;
        functions[index] = targets[index];
    }
    return true;
}

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
    int argc = 0;
    wchar_t** argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (!argv || argc != 4) return 1;
    void* dispatchVtable[] = {
        reinterpret_cast<void*>(&AddRef), reinterpret_cast<void*>(&Release),
        reinterpret_cast<void*>(&NativeCall), nullptr, reinterpret_cast<void*>(&PropGet)
    };
    g_global = { dispatchVtable, 1, 0 };
    g_font = { dispatchVtable, 1, 1 };
    g_map = { dispatchVtable, 1, 2 };
    g_unmap = { dispatchVtable, 1, 3 };
    g_probe = { dispatchVtable, 1, 4 };
    void* exporterVtable[] = { nullptr, reinterpret_cast<void*>(&QueryFunctions) };
    struct { void** vtable; } exporter = { exporterVtable };
    const bool markerPresent = g_marker[0] == 'm';
    HMODULE plugin = LoadLibraryW(argv[2]);
    const FARPROC original = plugin ? GetProcAddress(plugin, "V2Link") : nullptr;
    if (!original) return 2;
    HMODULE proxy = LoadLibraryW(argv[1]);
    if (!proxy) return 3;
    const FARPROC wrapped = GetProcAddress(plugin, "V2Link");
    using V2Link = HRESULT (__stdcall*)(void*);
    if (!wrapped || reinterpret_cast<V2Link>(wrapped)(&exporter) != S_OK) return 4;

    LONG result = -1;
    LONG instance = 123;
    void* parameter = nullptr;
    using Call = LONG (TJS_INTERFACE*)(void*, ULONG, const wchar_t*, ULONG*, void*, LONG, void**, void*);
#if defined(_M_IX86)
    uintptr_t before = 0, after = 0;
    __asm { mov before, esp }
#endif
    const LONG status = reinterpret_cast<Call>(dispatchVtable[2])(
        &g_map, 0, nullptr, nullptr, &result, 1, &parameter, &instance);
#if defined(_M_IX86)
    __asm { mov after, esp }
    const bool stackBalanced = before == after;
#else
    const bool stackBalanced = true;
#endif
    const LONG mapCalls = g_mapCalls, unmapCalls = g_unmapCalls;
    const bool forwardedArguments = g_lastCount == 0 && g_lastInstance == &instance && g_lastResult == &result;
    LONG otherResult = -1;
    const LONG otherStatus = reinterpret_cast<Call>(dispatchVtable[2])(
        &g_font, 0, nullptr, nullptr, &otherResult, 1, &parameter, &instance);
    const bool unrelatedPreserved = otherStatus == 22 && otherResult == 22 && g_lastCount == 1 && g_lastInstance == &instance;
    const std::wstring report = argv[3];
    const std::wstring temporary = report + L".tmp";
    FILE* output = nullptr;
    if (_wfopen_s(&output, temporary.c_str(), L"wb") || !output) return 5;
    fprintf(output, "{\"wrapped\":%s,\"status\":%ld,\"result\":%ld,\"mapCalls\":%ld,\"unmapCalls\":%ld,"
        "\"stackBalanced\":%s,\"forwardedArguments\":%s,\"exportArgumentsValid\":%s,\"unrelatedPreserved\":%s,\"memberIntact\":%s}\n",
        wrapped != original ? "true" : "false", status, result, mapCalls, unmapCalls,
        stackBalanced ? "true" : "false", forwardedArguments ? "true" : "false",
        g_exportArgumentsValid ? "true" : "false", unrelatedPreserved ? "true" : "false",
        markerPresent && g_marker[0] == 'm' ? "true" : "false");
    fclose(output);
    const bool published = MoveFileExW(temporary.c_str(), report.c_str(), MOVEFILE_REPLACE_EXISTING) != FALSE;
    LocalFree(argv);
    return published ? 0 : 6;
}
