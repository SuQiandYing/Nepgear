#pragma once

#include <windows.h>
#include <cstddef>

// Standalone dynamic-call declarations from the public LocaleEmulatorPlus-Core
// ABI. Keep this layout independent of the compiler and process pointer size.
namespace LepApi {

constexpr ULONG kEnvironmentVersion = 4;
constexpr LONG kInvalidParameter = static_cast<LONG>(0xC000000Du);

#pragma pack(push, 8)
struct EnvironmentBlock {
    ULONG Size;
    ULONG Version;
    ULONG AnsiCodePage;
    ULONG OemCodePage;
    ULONG LocaleID;
    ULONG FontCharset;
    ULONG RegistryRedirectionMode;
    ULONG HookUILanguageMode;
    WCHAR TimezoneId[128];
    WCHAR StandardNameOverride[32];
    WCHAR DaylightNameOverride[32];
};

struct TimeFields {
    SHORT Year, Month, Day, Hour, Minute, Second, Milliseconds, Weekday;
};

struct TimeZoneInformation {
    LONG Bias;
    WCHAR StandardName[32];
    TimeFields StandardStart;
    LONG StandardBias;
    WCHAR DaylightName[32];
    TimeFields DaylightStart;
    LONG DaylightBias;
};

// Early 2.x Loaders accept v2/204 only. Current Loaders also accept it, but v4
// lets the Loader resolve stable time-zone IDs and localized names correctly.
struct EnvironmentBlockV2 {
    ULONG Size;
    ULONG Version;
    ULONG AnsiCodePage;
    ULONG OemCodePage;
    ULONG LocaleID;
    ULONG FontCharset;
    ULONG RegistryRedirectionMode;
    ULONG HookUILanguageMode;
    TimeZoneInformation Timezone;
};
#pragma pack(pop)

using CreateProcessFunction = LONG (WINAPI*)(
    const void* environmentBlock,
    PCWSTR applicationName,
    PWSTR commandLine,
    PCWSTR currentDirectory,
    ULONG creationFlags,
    LPSTARTUPINFOW startupInfo,
    LPPROCESS_INFORMATION processInformation,
    LPSECURITY_ATTRIBUTES processAttributes,
    LPSECURITY_ATTRIBUTES threadAttributes,
    PVOID environment,
    HANDLE token);

static_assert(sizeof(EnvironmentBlock) == 416);
static_assert(offsetof(EnvironmentBlock, TimezoneId) == 32);
static_assert(offsetof(EnvironmentBlock, StandardNameOverride) == 288);
static_assert(offsetof(EnvironmentBlock, DaylightNameOverride) == 352);
static_assert(sizeof(TimeFields) == 16);
static_assert(sizeof(TimeZoneInformation) == 172);
static_assert(sizeof(EnvironmentBlockV2) == 204);
static_assert(offsetof(EnvironmentBlockV2, Timezone) == 32);
static_assert(sizeof(PROCESS_INFORMATION) == (sizeof(void*) == 8 ? 24 : 16));

} // namespace LepApi
