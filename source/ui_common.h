#pragma once
#include <windows.h>
#include <string>
#include <string_view>

namespace mu::ui
{
    // Makes windows created by the calling thread per-monitor DPI aware (v2) so dialogs are
    // rendered sharply even inside DPI-unaware processes. No-op before Windows 10 1607.
    class ScopedDpiAwareness
    {
    public:
        ScopedDpiAwareness();
        ~ScopedDpiAwareness();
        ScopedDpiAwareness(const ScopedDpiAwareness&) = delete;
        ScopedDpiAwareness& operator=(const ScopedDpiAwareness&) = delete;
    private:
        void* previous = nullptr;
    };

    // Loads a DLL from System32 only (an installer in the Downloads folder may run elevated,
    // a DLL with the same name next to it must not be picked up)
    HMODULE LoadSystemLibrary(const wchar_t* name);

    UINT GetWindowDpi(HWND window);
    UINT GetMonitorDpi(HMONITOR monitor);
    int GetSystemMetricForDpi(int index, UINT dpi);
    BOOL AdjustWindowRectForDpi(RECT* rect, DWORD style, DWORD exStyle, UINT dpi);

    // Windows "Choose your app mode" is set to dark
    bool IsDarkModeEnabled();

    // Light or dark window frame (border and shadow colors on Windows 11)
    void SetWindowDarkMode(HWND window, bool dark);

    // Icons loaded with LoadIcon have the size of a DPI unaware process (32x32). Returns a copy with
    // the size a dialog icon has at the DPI of the monitor with the mouse cursor (the result is cached).
    HICON GetDpiScaledIcon(HICON icon);

    // Opens http(s) and mailto links, ignores everything else
    void OpenUrl(HWND owner, const std::wstring& url);

    // Opens a folder (or selects a file) in Explorer
    void OpenInExplorer(const std::wstring& path, bool select = false);

    // Arguments of the current process, without the executable
    std::wstring GetProcessArguments();

    // Starts the current executable again. Returns false if it could not be started
    // (or the user declined the UAC prompt when 'elevated' is set).
    bool RestartProcess(bool elevated, const std::wstring& arguments);

    // Returns the first visible top-level window of the current process (to give focus back to the game)
    HWND FindProcessMainWindow();

    // Text with <a href="...">links</a> as used by TaskDialog: returns the visible text length
    size_t GetVisibleTextLength(std::wstring_view s);
}
