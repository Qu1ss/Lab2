#pragma once
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

class Utf8Console {
public:
    Utf8Console() {
#ifdef _WIN32
        previousCodePage_ = GetConsoleOutputCP();
        SetConsoleOutputCP(CP_UTF8);
#endif
    }

    ~Utf8Console() {
#ifdef _WIN32
        if (previousCodePage_ != 0) {
            SetConsoleOutputCP(previousCodePage_);
        }
#endif
    }

    Utf8Console(const Utf8Console&) = delete;
    Utf8Console& operator=(const Utf8Console&) = delete;

private:
#ifdef _WIN32
    UINT previousCodePage_ = 0;
#endif
};
