#pragma once
#include <windows.h>
#include <array>
#include <atomic>
#include <cstdint>
#include <filesystem>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>
#include "libmodupdater.h"
#include "extract.h"

namespace mu::installer
{
    // Settings for the light or the dark theme only (muSetInstallerTheme*)
    struct ThemeOverrides
    {
        std::array<std::optional<COLORREF>, MU_COLOR_COUNT> colors;
        std::vector<uint8_t> logo;
        std::vector<uint8_t> background;
        std::optional<int> backgroundOverlay;
    };

    // Installer settings collected from the API (see libmodupdater.h)
    struct Config
    {
        HMODULE module = nullptr;
        std::wstring windowTitle;
        std::wstring mainInstruction;
        std::wstring content;
        std::wstring footer;
        HICON icon = nullptr;
        std::string updateUrl;
        std::string password;
        std::string steamAppId, steamSubfolder;
        std::string rglAppId, rglSubfolder;
        int ui = MU_UI_CLASSIC;
        std::vector<uint8_t> logo;
        std::vector<uint8_t> background;
        int backgroundOverlay = 70;
        int backgroundBlur = 0;         // DIP
        int textBackdropBlur = 0;       // DIP, the background picture behind texts only
        std::vector<uint8_t> fontData;
        std::wstring fontFamily;
        int width = 0;
        int height = 0;
        std::array<std::optional<COLORREF>, MU_COLOR_COUNT> colors;
        int theme = MU_THEME_AUTO;
        ThemeOverrides light, dark;
        std::array<std::optional<std::wstring>, MU_STR_COUNT> strings;
        std::vector<std::wstring> gameExecutables;
        std::vector<std::wstring> extraPaths;
        IniMode iniMode = IniMode::Merge;
        bool iniSelectable = false;
        std::wstring logFile;
        std::filesystem::path package; // file with the embedded archives, empty = the running executable

        const ThemeOverrides& Overrides(int resolvedTheme) const { return resolvedTheme == MU_THEME_DARK ? dark : light; }
    };

    // Reads the settings registered for 'module' (or the first module with installer settings)
    Config LoadConfig(HMODULE module);

    // MU_THEME_LIGHT or MU_THEME_DARK: config.theme, or the Windows app mode for MU_THEME_AUTO
    int ResolveTheme(const Config& config);

    // Built-in or user supplied text (MU_STR_*) with placeholders replaced, {exe} is 'exe' or the first game executable
    std::wstring Text(const Config& config, int id, const std::filesystem::path& path = {}, const std::wstring& file = {}, const std::wstring& error = {}, const std::wstring& exe = {});
    std::wstring WindowTitle(const Config& config);

    struct Location
    {
        std::filesystem::path path;
        std::wstring source; // "Steam", "Rockstar Games Launcher", ...
    };

    // Suggested install folders: Steam, Rockstar Games Launcher, muAddInstallerPath and the
    // installer's own folder when it contains the game
    std::vector<Location> DetectLocations(const Config& config);

    // Shortest end of each path that the other paths don't share: "GTAIV", "Library 2\Grand Theft Auto IV"
    std::vector<std::wstring> ShortLocationNames(const std::vector<Location>& locations);

    struct PathCheck
    {
        bool writable = false;
        bool hasGameExe = true;     // always true when no executable is configured
        std::wstring exeName;       // first configured executable
    };
    PathCheck CheckPath(const Config& config, const std::filesystem::path& path);

    // First configured game executable that exists in 'folder'
    std::filesystem::path FindGameExecutable(const Config& config, const std::filesystem::path& folder);

    // "Launch GTAIV.exe" with the executable that LaunchGame starts from 'folder', empty if there is none
    std::wstring LaunchText(const Config& config, const std::filesystem::path& folder);

    // Name of a configured game executable that is running from 'folder', or empty
    std::wstring GetRunningGame(const Config& config, const std::filesystem::path& folder);

    // Asks the user to close the game when it is running. Returns false if the installation should not start.
    bool ConfirmGameClosed(HWND owner, const Config& config, const std::filesystem::path& folder);

    // Asks whether to install into a folder that does not contain the game executable
    bool ConfirmMissingExecutable(HWND owner, const Config& config, const std::filesystem::path& folder, const std::wstring& exeName);

    std::filesystem::path BrowseForFolder(HWND owner, const Config& config, const std::filesystem::path& initial = {});

    struct CommandLine
    {
        std::filesystem::path installDir;   // --mu-install-dir=<path>
        bool autoStart = false;             // --mu-autostart: start installing into installDir right away
        bool silent = false;                // --mu-silent: no UI, needs --mu-install-dir or a detected folder
        std::optional<int> ui;              // --mu-ui=classic|modern
        std::optional<IniMode> iniMode;     // --mu-ini=merge|replace|skip
        std::optional<int> theme;           // --mu-theme=auto|light|dark
        std::filesystem::path screenshot;   // --mu-screenshot=<file.png>: renders the modern UI into a file, no window
        std::wstring screenshotPage = L"main"; // --mu-screenshot-page=<page>, see RenderModernPreview
        UINT screenshotDpi = 96;            // --mu-screenshot-dpi=<dpi>
    };
    CommandLine ParseCommandLine();

    // Restarts the installer as administrator and lets it install into 'installDir' right away
    bool RestartElevated(const std::filesystem::path& installDir, IniMode iniMode);

    enum class Phase
    {
        Preparing,
        Downloading,
        Extracting,
        Finished,
    };

    struct Progress
    {
        Phase phase = Phase::Preparing;
        uint64_t done = 0;
        uint64_t total = 0;         // 0 = unknown
        double bytesPerSecond = 0;  // while downloading
        std::wstring file;          // download name or current file
    };

    enum class Result
    {
        Success,
        Cancelled,
        Failed,
    };

    struct Outcome
    {
        Result result = Result::Failed;
        std::wstring error;                 // main error message
        std::vector<std::wstring> details;  // additional errors
        std::vector<std::wstring> warnings;
        int filesWritten = 0;
    };

    // Installs the embedded archives (offline installer) or the download on a worker thread
    class Job
    {
    public:
        Job(const Config& config, std::filesystem::path target, IniMode iniMode);
        ~Job();
        Job(const Job&) = delete;
        Job& operator=(const Job&) = delete;

        void Start();
        void Cancel();
        bool IsCancelling() const { return cancel; }
        bool IsFinished() const { return finished; }
        Progress GetProgress() const;
        Outcome GetOutcome() const;
        const std::filesystem::path& Target() const { return target; }
        std::wstring StatusText() const; // text for the current phase

    private:
        void Run();
        void RunOffline();
        void RunOnline();
        bool Extract(zip::Reader& reader, uint64_t base, uint64_t total, ExtractReport& report);
        void SetProgress(Phase phase, uint64_t done, uint64_t total, const std::wstring& file);
        void Fail(const std::wstring& error, const std::vector<std::wstring>& details = {});

        Config config;
        std::filesystem::path target;
        IniMode iniMode;
        std::thread thread;
        std::atomic<bool> cancel = false;
        std::atomic<bool> finished = false;
        mutable std::mutex mutex;
        Progress progress;
        Outcome outcome;
        // download speed estimate
        uint64_t speedBytes = 0;
        uint64_t speedTick = 0;
    };

    // Common summary text of an outcome
    std::wstring DescribeOutcome(const Config& config, const Outcome& outcome, const std::filesystem::path& target);

    // "45% · 93.1 MB of 206.7 MB · 12.4 MB/s"
    std::wstring ProgressLine(const Progress& progress);

    // Starts the game after the installation
    bool LaunchGame(const Config& config, const std::filesystem::path& folder);

    int RunClassic(Config& config, const CommandLine& commandLine);
    int RunSilent(Config& config, const CommandLine& commandLine);
    // Returns -1 if the window could not be created (Direct2D unavailable), the caller falls back to RunClassic
    int RunModern(Config& config, const CommandLine& commandLine);

    // Renders a page of the modern UI into a PNG file without showing a window (tests, screenshots).
    // page: "main", "locations" (list of install folders open), "tooltip", "progress", "success", "failure", "cancelled"
    bool RenderModernPreview(Config& config, const std::wstring& page, UINT dpi, const std::filesystem::path& png);
}
