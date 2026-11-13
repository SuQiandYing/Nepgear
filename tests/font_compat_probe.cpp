#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>
#include <mmsystem.h>
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#if defined(PROBE_BGI_IDENTITY)
static volatile char g_identity[] = "Buriko General Interpreter";
#elif defined(PROBE_TOPCAT_IDENTITY)
static volatile char g_identity[] = "TOPCAT SET_FONT";
#elif defined(PROBE_KRKR_IDENTITY)
static volatile char g_identity[] = "mapPrerenderedFont";
#else
static volatile char g_identity[] = "generic font probe";
#endif

static void WriteString(FILE* output, const wchar_t* text) {
    const int length = WideCharToMultiByte(CP_UTF8, 0, text, -1, nullptr, 0, nullptr, nullptr);
    std::vector<char> bytes(length > 0 ? length : 1, 0);
    if (length > 0) WideCharToMultiByte(CP_UTF8, 0, text, -1, bytes.data(), length, nullptr, nullptr);
    fputc('"', output);
    for (const unsigned char byte : bytes) {
        if (!byte) break;
        if (byte == '"' || byte == '\\') fputc('\\', output);
        if (byte < 32) fprintf(output, "\\u%04x", byte);
        else fputc(byte, output);
    }
    fputc('"', output);
}

static const wchar_t* OutlineString(const std::vector<BYTE>& bytes, const void* offset) {
    const uintptr_t start = reinterpret_cast<uintptr_t>(offset);
    if (start >= bytes.size() || (start & 1)) return L"";
    const wchar_t* text = reinterpret_cast<const wchar_t*>(bytes.data() + start);
    const size_t capacity = (bytes.size() - start) / sizeof(wchar_t);
    return wcsnlen_s(text, capacity) < capacity ? text : L"";
}

static FARPROC RawExport(HMODULE module, const char* wanted, bool* concrete) {
    BYTE* base = reinterpret_cast<BYTE*>(module);
    const auto dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
    const auto nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(base + dos->e_lfanew);
    const auto& directory = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT];
    if (!directory.VirtualAddress || !directory.Size) return nullptr;
    const auto exports = reinterpret_cast<const IMAGE_EXPORT_DIRECTORY*>(base + directory.VirtualAddress);
    const auto names = reinterpret_cast<const DWORD*>(base + exports->AddressOfNames);
    const auto ordinals = reinterpret_cast<const WORD*>(base + exports->AddressOfNameOrdinals);
    const auto functions = reinterpret_cast<const DWORD*>(base + exports->AddressOfFunctions);
    for (DWORD index = 0; index < exports->NumberOfNames; ++index) {
        const DWORD rva = functions[ordinals[index]];
        if (rva >= directory.VirtualAddress && rva - directory.VirtualAddress < directory.Size) {
            *concrete = false;
        }
        if (strcmp(reinterpret_cast<const char*>(base + names[index]), wanted) == 0) {
            return reinterpret_cast<FARPROC>(base + rva);
        }
    }
    return nullptr;
}

static std::wstring NativeArialFullName() {
    HDC dc = CreateCompatibleDC(nullptr);
    if (!dc) return {};
    LOGFONTW request = {};
    request.lfHeight = -24;
    request.lfCharSet = DEFAULT_CHARSET;
    wcscpy_s(request.lfFaceName, L"Arial");
    HFONT font = CreateFontIndirectW(&request);
    const HGDIOBJ old = font ? SelectObject(dc, font) : nullptr;
    const UINT size = font ? GetOutlineTextMetricsW(dc, 0, nullptr) : 0;
    std::wstring fullName;
    if (size >= sizeof(OUTLINETEXTMETRICW)) {
        std::vector<BYTE> buffer(size);
        auto metric = reinterpret_cast<OUTLINETEXTMETRICW*>(buffer.data());
        if (GetOutlineTextMetricsW(dc, size, metric)) {
            fullName = OutlineString(buffer, metric->otmpFullName);
        }
    }
    if (old) SelectObject(dc, old);
    if (font) DeleteObject(font);
    DeleteDC(dc);
    return fullName;
}

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
    int argc = 0;
    wchar_t** argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (!argv || argc < 3) return 1;
    const char identityFirst = g_identity[0]; // Keep fixture identity in the PE.
    const std::wstring expectedFullName = NativeArialFullName();
    HMODULE proxy = LoadLibraryW(argv[1]);
    if (!proxy) return 2;

    bool concrete = true;
    const FARPROC rawTime = RawExport(proxy, "timeGetTime", &concrete);
    const FARPROC rawCaps = RawExport(proxy, "timeGetDevCaps", &concrete);
    const FARPROC rawDevice = RawExport(proxy, "mciGetDeviceIDW", &concrete);
    bool proxyCalls = concrete && rawTime && rawCaps && rawDevice &&
        rawTime == GetProcAddress(proxy, "timeGetTime");
    if (proxyCalls) {
        using Time = DWORD (WINAPI*)();
        using Caps = MMRESULT (WINAPI*)(LPTIMECAPS, UINT);
        using Device = MCIDEVICEID (WINAPI*)(LPCWSTR);
        TIMECAPS caps = {};
#if defined(_M_IX86)
        uintptr_t before = 0, after = 0;
        __asm { mov before, esp }
#endif
        for (int index = 0; index < 32; ++index) reinterpret_cast<Time>(rawTime)();
        proxyCalls = reinterpret_cast<Caps>(rawCaps)(&caps, sizeof(caps)) == MMSYSERR_NOERROR &&
            reinterpret_cast<Device>(rawDevice)(L"Nepgear regression nonexistent MCI device") == 0;
#if defined(_M_IX86)
        __asm { mov after, esp }
        proxyCalls = proxyCalls && before == after;
#endif
    }

    HDC dc = CreateCompatibleDC(nullptr);
    if (!dc) return 3;
    const HGDIOBJ stock = GetCurrentObject(dc, OBJ_FONT);
    LOGFONTW requested = {};
    requested.lfHeight = -24;
    requested.lfCharSet = DEFAULT_CHARSET;
    wcscpy_s(requested.lfFaceName, L"Times New Roman");
    std::wstring physicalFace, outlineFace, outlineFullName;
    bool outlineBounds = true, smallBufferRejected = true, ansiLogicalFace = false;
    int attempts = 0;
    for (; attempts < 3; ++attempts) {
        HFONT font = CreateFontIndirectW(&requested);
        if (!font) return 4;
        SelectObject(dc, font);
        wchar_t face[LF_FACESIZE] = {};
        GetTextFaceW(dc, _countof(face), face);
        physicalFace = face;
        const UINT required = GetOutlineTextMetricsW(dc, 0, nullptr);
        if (required < sizeof(OUTLINETEXTMETRICW)) return 5;
        std::vector<BYTE> buffer(required + 16, 0xA5);
        auto metrics = reinterpret_cast<OUTLINETEXTMETRICW*>(buffer.data());
        const UINT result = GetOutlineTextMetricsW(dc, required, metrics);
        if (!result || result > required) return 6;
        outlineFace = OutlineString(buffer, metrics->otmpFaceName);
        outlineFullName = OutlineString(buffer, metrics->otmpFullName);
        outlineBounds = outlineBounds && std::all_of(buffer.begin() + required, buffer.end(),
            [](BYTE byte) { return byte == 0xA5; });
        std::vector<BYTE> small(required - 1, 0xA5);
        smallBufferRejected = smallBufferRejected && GetOutlineTextMetricsW(dc, required - 1,
            reinterpret_cast<OUTLINETEXTMETRICW*>(small.data())) == 0;

        const UINT ansiSize = GetOutlineTextMetricsA(dc, 0, nullptr);
        if (ansiSize >= sizeof(OUTLINETEXTMETRICA)) {
            std::vector<BYTE> ansi(ansiSize);
            auto ansiMetrics = reinterpret_cast<OUTLINETEXTMETRICA*>(ansi.data());
            if (GetOutlineTextMetricsA(dc, ansiSize, ansiMetrics)) {
                const uintptr_t offset = reinterpret_cast<uintptr_t>(ansiMetrics->otmpFaceName);
                ansiLogicalFace = offset < ansi.size() &&
                    strcmp(reinterpret_cast<const char*>(ansi.data() + offset), "Times New Roman") == 0;
            }
        }
        SelectObject(dc, stock);
        DeleteObject(font);
        // This is the BGI recreate-until-requested-face-is-returned pattern.
        // Bound it so an unfixed DLL yields a deterministic failure, not a crash.
        if (outlineFace == requested.lfFaceName) { ++attempts; break; }
    }

    LOGFONTA tallFont = {};
    tallFont.lfHeight = -700;
    tallFont.lfCharSet = DEFAULT_CHARSET;
    strcpy_s(tallFont.lfFaceName, "Arial");
    HFONT tall = CreateFontIndirectA(&tallFont);
    if (!tall) return 7;
    SelectObject(dc, tall);
    TEXTMETRICA textMetrics = {};
    const BOOL measured = GetTextMetricsA(dc, &textMetrics);
    SelectObject(dc, stock);
    DeleteObject(tall);
    DeleteDC(dc);

    const bool memberIntact = identityFirst != 'm' ||
        (g_identity[0] == 'm' && g_identity[1] == 'a' && g_identity[2] == 'p');
    const bool tftVisible = GetFileAttributesW(L"probe.tft") != INVALID_FILE_ATTRIBUTES;
    const std::wstring report = argv[2];
    const std::wstring temporary = report + L".tmp";
    FILE* output = nullptr;
    if (_wfopen_s(&output, temporary.c_str(), L"wb") || !output) return 8;
    fprintf(output, "{\"proxyConcreteExports\":%s,\"proxyRawCalls\":%s,\"attempts\":%d,"
        "\"outlineBounds\":%s,\"smallBufferRejected\":%s,\"ansiLogicalFace\":%s,\"fullNamePreserved\":%s,"
        "\"metricsMeasured\":%s,\"metricHeight\":%ld,\"metricWidth\":%ld,"
        "\"krkrMemberIntact\":%s,\"tftVisible\":%s,\"physicalFace\":",
        concrete ? "true" : "false", proxyCalls ? "true" : "false", attempts,
        outlineBounds ? "true" : "false", smallBufferRejected ? "true" : "false", ansiLogicalFace ? "true" : "false",
        !expectedFullName.empty() && expectedFullName == outlineFullName ? "true" : "false",
        measured ? "true" : "false", textMetrics.tmHeight, textMetrics.tmMaxCharWidth,
        memberIntact ? "true" : "false", tftVisible ? "true" : "false");
    WriteString(output, physicalFace.c_str());
    fputs(",\"outlineFace\":", output);
    WriteString(output, outlineFace.c_str());
    fputs(",\"outlineFullName\":", output);
    WriteString(output, outlineFullName.c_str());
    fputs("}\n", output);
    fclose(output);
    const bool published = MoveFileExW(temporary.c_str(), report.c_str(), MOVEFILE_REPLACE_EXISTING) != FALSE;
    LocalFree(argv);
    return published ? 0 : 9;
}
