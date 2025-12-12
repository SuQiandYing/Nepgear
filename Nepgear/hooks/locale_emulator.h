#pragma once
#ifndef LOCALE_EMULATOR_H
#define LOCALE_EMULATOR_H

#include <windows.h>
#include <string>

class LocaleEmulator {
public:
    static LocaleEmulator& getInstance();
    bool initialize();
    bool performLocaleEmulation();
    bool isLocaleEmulationEnabled() const { return m_enabled; }

private:
    LocaleEmulator() = default;
    ~LocaleEmulator() = default;
    LocaleEmulator(const LocaleEmulator&) = delete;
    LocaleEmulator& operator=(const LocaleEmulator&) = delete;

    bool needsLocaleEmulation() const;
    bool relaunchProcess();

    bool m_enabled = false;
    bool m_initialized = false;
    unsigned int m_codepage = 932;
    unsigned int m_localeId = 1041;
    unsigned int m_charset = 128;
    std::wstring m_timezone = L"Tokyo Standard Time";
};

#endif
