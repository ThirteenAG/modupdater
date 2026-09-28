#pragma once
#include <windows.h>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

// Settings made through the public API are kept per module in a registry that is shared by all
// modules of the process that link modupdater (every .asi has its own copy of the library).
// The first module creates it; the others find it through a per-process named file mapping and
// talk to it through a small versioned C interface, so modules built with different compilers,
// runtimes or versions of this library can safely share it.
namespace mu
{
    // Registry keys. The values are part of the cross-module interface: never change or reuse them.
    enum class Key : uint32_t
    {
        UpdateUrl = 1,
        DevUpdateUrl = 2,
        ArchivePassword = 3,
        SkipUpdateCompleteDialog = 4,
        AlwaysUpdate = 5,

        InstallerIcon = 100,
        InstallerWindowTitle = 101,
        InstallerMainInstruction = 102,
        InstallerContent = 103,
        InstallerFooter = 104,
        RglAppId = 105,
        RglSubfolder = 106,
        SteamAppId = 107,
        SteamSubfolder = 108,

        InstallerUI = 200,
        InstallerLogo = 201,
        InstallerBackground = 202,
        InstallerBackgroundOverlay = 203,
        InstallerFontFamily = 204,
        InstallerFontData = 205,
        InstallerWidth = 206,
        InstallerHeight = 207,
        InstallerGameExecutables = 208,
        InstallerExtraPaths = 209,     // '\n' separated, appended to
        InstallerIniMode = 210,
        InstallerIniSelectable = 211,
        LogFile = 212,
        InstallerTheme = 213,
        InstallerLightLogo = 214,
        InstallerDarkLogo = 215,
        InstallerLightBackground = 216,
        InstallerDarkBackground = 217,
        InstallerLightBackgroundOverlay = 218,
        InstallerDarkBackgroundOverlay = 219,
        InstallerBackgroundBlur = 220,
        InstallerTextBackdropBlur = 221,

        InstallerColorBase = 1000,     // + MU_COLOR_*
        InstallerStringBase = 2000,    // + MU_STR_*
        InstallerLightColorBase = 3000, // + MU_COLOR_*
        InstallerDarkColorBase = 4000, // + MU_COLOR_*
    };

    constexpr uint32_t operator+(Key key, int offset) { return static_cast<uint32_t>(key) + static_cast<uint32_t>(offset); }

    struct RegistryValue
    {
        std::string str;
        int64_t num = 0;
        std::vector<uint8_t> blob;
    };

    using ModuleSettings = std::map<uint32_t, RegistryValue>;
    using RegistrySnapshot = std::map<HMODULE, ModuleSettings>;

    void RegistrySetString(HMODULE module, uint32_t key, const char* value, bool append = false);
    void RegistrySetInt(HMODULE module, uint32_t key, int64_t value);
    void RegistrySetBlob(HMODULE module, uint32_t key, const void* data, size_t size);
    RegistrySnapshot RegistryGetSnapshot();

    // Returns true for exactly one caller per process and 'what'
    bool RegistryClaim(uint32_t what);
    constexpr uint32_t kClaimUpdater = 1;

    // Accessors for snapshots
    const RegistryValue* FindValue(const ModuleSettings& settings, uint32_t key);
    std::string GetString(const ModuleSettings& settings, uint32_t key, const std::string& fallback = {});
    int64_t GetInt(const ModuleSettings& settings, uint32_t key, int64_t fallback = 0);
    bool HasValue(const ModuleSettings& settings, uint32_t key);

    inline uint32_t K(Key key) { return static_cast<uint32_t>(key); }
}
