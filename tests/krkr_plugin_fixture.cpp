#define WIN32_LEAN_AND_MEAN
#include <windows.h>

extern "C" HRESULT __stdcall ProbeV2Link(void*) {
    return S_OK;
}

#ifdef _WIN64
#pragma comment(linker, "/EXPORT:V2Link=ProbeV2Link")
#else
#pragma comment(linker, "/EXPORT:V2Link=_ProbeV2Link@4")
#endif
