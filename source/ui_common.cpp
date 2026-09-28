#include "stdafx.h"
#include "ui_common.h"
#include "fileops.h"
#include "log.h"
#include "string_funcs.h"
#include <shellapi.h>

namespace mu::ui
{
    namespace
    {
        template<typename T>
        T GetUser32Function(const char* name)
        {
            return reinterpret_cast<T>(GetProcAddress(GetModuleHandleW(L"user32.dll"), name));
        }

        UINT GetSystemDpi()
        {
            HDC dc = GetDC(nullptr);
            UINT dpi = dc ? static_cast<UINT>(GetDeviceCaps(dc, LOGPIXELSX)) : 96;
            if (dc)
                ReleaseDC(nullptr, dc);
            return dpi ? dpi : 96;
        }
    }

    ScopedDpiAwareness::ScopedDpiAwareness()
    {
        using Fn = DPI_AWARENESS_CONTEXT(WINAPI*)(DPI_AWARENESS_CONTEXT);
        static auto setContext = GetUser32Function<Fn>("SetThreadDpiAwarenessContext");
        if (setContext)
        {
            previous = setContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
            if (!previous)
                previous = setContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE);
        }
    }

    ScopedDpiAwareness::~ScopedDpiAwareness()
    {
        using Fn = DPI_AWARENESS_CONTEXT(WINAPI*)(DPI_AWARENESS_CONTEXT);
        static auto setContext = GetUser32Function<Fn>("SetThreadDpiAwarenessContext");
        if (setContext && previous)
            setContext(static_cast<DPI_AWARENESS_CONTEXT>(previous));
    }

    HMODULE LoadSystemLibrary(const wchar_t* name)
    {
        wchar_t path[MAX_PATH];
        UINT length = GetSystemDirectoryW(path, MAX_PATH);
        if (length == 0 || length + wcslen(name) + 2 > MAX_PATH)
            return nullptr;
        std::wstring full = std::wstring(path, length) + L"\\" + name;
        return LoadLibraryW(full.c_str());
    }

    UINT GetMonitorDpi(HMONITOR monitor)
    {
        using Fn = HRESULT(WINAPI*)(HMONITOR, int, UINT*, UINT*);
        static auto getDpiForMonitor = []() -> Fn
        {
            HMODULE shcore = LoadSystemLibrary(L"shcore.dll");
            return shcore ? reinterpret_cast<Fn>(GetProcAddress(shcore, "GetDpiForMonitor")) : nullptr;
        }();

        UINT dpiX = 0, dpiY = 0;
        if (monitor && getDpiForMonitor && SUCCEEDED(getDpiForMonitor(monitor, 0 /* MDT_EFFECTIVE_DPI */, &dpiX, &dpiY)) && dpiX)
            return dpiX;
        return GetSystemDpi();
    }

    UINT GetWindowDpi(HWND window)
    {
        using Fn = UINT(WINAPI*)(HWND);
        static auto getDpiForWindow = GetUser32Function<Fn>("GetDpiForWindow");
        if (getDpiForWindow && window)
        {
            if (UINT dpi = getDpiForWindow(window))
                return dpi;
        }
        return GetMonitorDpi(MonitorFromWindow(window, MONITOR_DEFAULTTONEAREST));
    }

    int GetSystemMetricForDpi(int index, UINT dpi)
    {
        using Fn = int(WINAPI*)(int, UINT);
        static auto getSystemMetricsForDpi = GetUser32Function<Fn>("GetSystemMetricsForDpi");
        if (getSystemMetricsForDpi)
            return getSystemMetricsForDpi(index, dpi);
        return MulDiv(GetSystemMetrics(index), dpi, GetSystemDpi());
    }

    BOOL AdjustWindowRectForDpi(RECT* rect, DWORD style, DWORD exStyle, UINT dpi)
    {
        using Fn = BOOL(WINAPI*)(LPRECT, DWORD, BOOL, DWORD, UINT);
        static auto adjustWindowRectExForDpi = GetUser32Function<Fn>("AdjustWindowRectExForDpi");
        if (adjustWindowRectExForDpi)
            return adjustWindowRectExForDpi(rect, style, FALSE, exStyle, dpi);
        return AdjustWindowRectEx(rect, style, FALSE, exStyle);
    }

    bool IsDarkModeEnabled()
    {
        DWORD light = 1, size = sizeof(light);
        if (RegGetValueW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize", L"AppsUseLightTheme",
                RRF_RT_REG_DWORD, nullptr, &light, &size) != ERROR_SUCCESS)
        {
            return false; // before Windows 10 1809
        }
        return light == 0;
    }

    void SetWindowDarkMode(HWND window, bool dark)
    {
        using Fn = HRESULT(WINAPI*)(HWND, DWORD, LPCVOID, DWORD);
        static auto setAttribute = [] { HMODULE dwm = LoadSystemLibrary(L"dwmapi.dll"); return dwm ? reinterpret_cast<Fn>(GetProcAddress(dwm, "DwmSetWindowAttribute")) : nullptr; }();
        if (!setAttribute)
            return;
        BOOL value = dark;
        // DWMWA_USE_IMMERSIVE_DARK_MODE, 19 before Windows 10 20H1
        if (FAILED(setAttribute(window, 20, &value, sizeof(value))))
            setAttribute(window, 19, &value, sizeof(value));
    }

    HICON GetDpiScaledIcon(HICON icon)
    {
        if (!icon)
            return nullptr;

        POINT cursor = {};
        GetCursorPos(&cursor);
        UINT dpi = GetMonitorDpi(MonitorFromPoint(cursor, MONITOR_DEFAULTTOPRIMARY));
        int size = GetSystemMetricForDpi(SM_CXICON, dpi);

        static std::mutex mutex;
        static std::map<std::pair<HICON, int>, HICON> cache;
        std::lock_guard<std::mutex> lock(mutex);
        auto key = std::make_pair(icon, size);
        if (auto it = cache.find(key); it != cache.end())
            return it->second;

        ICONINFO info = {};
        BITMAP bitmap = {};
        if (GetIconInfo(icon, &info))
        {
            GetObjectW(info.hbmColor ? info.hbmColor : info.hbmMask, sizeof(bitmap), &bitmap);
            if (info.hbmColor)
                DeleteObject(info.hbmColor);
            if (info.hbmMask)
                DeleteObject(info.hbmMask);
        }

        HICON scaled = icon;
        if (bitmap.bmWidth != size)
        {
            // an icon that was loaded from resources is copied from its best matching image
            if (HICON copy = static_cast<HICON>(CopyImage(icon, IMAGE_ICON, size, size, LR_COPYFROMRESOURCE)))
                scaled = copy;
        }
        cache[key] = scaled;
        return scaled;
    }

    void OpenUrl(HWND owner, const std::wstring& url)
    {
        if (starts_with(url, L"https://", false) || starts_with(url, L"http://", false) || starts_with(url, L"mailto:", false))
            ShellExecuteW(owner, L"open", url.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
        else
            Log(L"Ignored link {}", url);
    }

    void OpenInExplorer(const std::wstring& path, bool select)
    {
        if (select)
        {
            auto args = L"/select,\"" + path + L"\"";
            ShellExecuteW(nullptr, L"open", L"explorer.exe", args.c_str(), nullptr, SW_SHOWNORMAL);
        }
        else
        {
            ShellExecuteW(nullptr, L"explore", path.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
        }
    }

    std::wstring GetProcessArguments()
    {
        const wchar_t* cmd = GetCommandLineW();
        if (!cmd)
            return {};

        // argv[0] follows simpler rules than the other arguments
        if (*cmd == L'"')
        {
            cmd++;
            while (*cmd && *cmd != L'"')
                cmd++;
            if (*cmd == L'"')
                cmd++;
        }
        else
        {
            while (*cmd && *cmd != L' ' && *cmd != L'\t')
                cmd++;
        }
        while (*cmd == L' ' || *cmd == L'\t')
            cmd++;
        return cmd;
    }

    bool RestartProcess(bool elevated, const std::wstring& arguments)
    {
        auto exe = GetModuleFilePath(nullptr);
        auto dir = exe.parent_path();

        SHELLEXECUTEINFOW info = { sizeof(info) };
        info.fMask = SEE_MASK_NOASYNC;
        info.lpVerb = elevated ? L"runas" : nullptr;
        info.lpFile = exe.c_str();
        info.lpParameters = arguments.empty() ? nullptr : arguments.c_str();
        info.lpDirectory = dir.c_str();
        info.nShow = SW_SHOWNORMAL;
        if (ShellExecuteExW(&info))
            return true;

        DWORD error = GetLastError();
        Log(L"Cannot restart {}{}: {}", exe.filename().wstring(), elevated ? L" as administrator" : L"", Win32ErrorMessage(error));
        return false;
    }

    HWND FindProcessMainWindow()
    {
        struct Search { DWORD pid; HWND result; } search = { GetCurrentProcessId(), nullptr };
        EnumWindows([](HWND hwnd, LPARAM lParam) -> BOOL
        {
            auto s = reinterpret_cast<Search*>(lParam);
            DWORD pid = 0;
            GetWindowThreadProcessId(hwnd, &pid);
            if (pid == s->pid && IsWindowVisible(hwnd) && !GetWindow(hwnd, GW_OWNER))
            {
                s->result = hwnd;
                return FALSE;
            }
            return TRUE;
        }, reinterpret_cast<LPARAM>(&search));
        return search.result;
    }

    size_t GetVisibleTextLength(std::wstring_view s)
    {
        size_t length = 0;
        size_t i = 0;
        while (i < s.length())
        {
            if (s[i] == L'<')
            {
                // <a ...> or <a>
                if (i + 1 < s.length() && (s[i + 1] == L'a' || s[i + 1] == L'A'))
                {
                    size_t tagEndPos = s.find(L'>', i + 1);
                    if (tagEndPos != std::wstring_view::npos)
                    {
                        i = tagEndPos + 1;
                        continue;
                    }
                }
                // </a>
                else if (i + 3 < s.length() && s[i + 1] == L'/' && (s[i + 2] == L'a' || s[i + 2] == L'A') && s[i + 3] == L'>')
                {
                    i += 4;
                    continue;
                }
            }
            length++;
            i++;
        }
        return length;
    }
}
