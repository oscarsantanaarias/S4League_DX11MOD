#pragma once
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cstdarg>
#include <mutex>

namespace ne {
inline void Log(const char* fmt, ...);

// wvsprintfA ignores precision (%.600s), so a shader compile error was always
// truncated to nothing useful and the fog wrapper failure was never diagnosable.
// This prints a long blob in chunks instead.
inline void LogLong(const char* tag, const char* text) {
    if (!text) { return; }
    char buf[220];
    for (int off = 0; text[off]; ) {
        int n = 0;
        while (n < (int)sizeof(buf) - 1 && text[off + n] && text[off + n] != '\n') { buf[n] = text[off + n]; n++; }
        buf[n] = 0;
        if (n) Log("%s %s\n", tag, buf);
        off += n;
        while (text[off] == '\n') off++;
    }
}

inline void Log(const char* fmt, ...) {
    char b[1024];
    va_list a; va_start(a, fmt); wvsprintfA(b, fmt, a); va_end(a);
    OutputDebugStringA(b);
    static std::mutex m;
    static HANDLE h = INVALID_HANDLE_VALUE;
    std::lock_guard<std::mutex> lock(m);
    if (h == INVALID_HANDLE_VALUE)
        h = CreateFileA("nativeengine.log", FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE,
                        nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h != INVALID_HANDLE_VALUE) { DWORD w; WriteFile(h, b, lstrlenA(b), &w, nullptr); }
}
} // namespace ne
