#include "stdafx.h"
#include "registry.h"
#include "libmodupdater.h"
#include "log.h"
#include "string_funcs.h"
#include <set>

namespace mu
{
    namespace
    {
        constexpr uint32_t kTypeString = 1;
        constexpr uint32_t kTypeInt = 2;
        constexpr uint32_t kTypeBlob = 3;
        constexpr uint32_t kFlagAppend = 1;

        using EnumCallback = void(__cdecl*)(void* ctx, HMODULE module, uint32_t key, uint32_t type, const char* str, int64_t num, const void* blob, uint32_t blobSize);

        // Cross-module interface, only plain C types. Newer versions may append members,
        // check cbSize before using them.
        struct RegistryInterface
        {
            uint32_t cbSize;
            uint32_t version;
            void(__cdecl* Set)(HMODULE module, uint32_t key, uint32_t type, const char* str, int64_t num, const void* blob, uint32_t blobSize, uint32_t flags);
            void(__cdecl* Enumerate)(void* ctx, EnumCallback callback);
            int32_t(__cdecl* Claim)(uint32_t what);
        };

        struct StoredValue
        {
            uint32_t type = 0;
            std::string str;
            int64_t num = 0;
            std::vector<uint8_t> blob;
        };

        // Data of the registry owned by this module (only used when this module is the owner)
        std::mutex dataMutex;
        std::map<HMODULE, std::map<uint32_t, StoredValue>> data;
        std::set<uint32_t> claims;

        void __cdecl ImplSet(HMODULE module, uint32_t key, uint32_t type, const char* str, int64_t num, const void* blob, uint32_t blobSize, uint32_t flags)
        {
            std::lock_guard<std::mutex> lock(dataMutex);
            auto& value = data[module][key];
            value.type = type;
            switch (type)
            {
            case kTypeString:
                if ((flags & kFlagAppend) && !value.str.empty())
                    value.str += '\n';
                else if (!(flags & kFlagAppend))
                    value.str.clear();
                value.str += str ? str : "";
                break;
            case kTypeInt:
                value.num = num;
                break;
            case kTypeBlob:
                if (blob && blobSize)
                    value.blob.assign(static_cast<const uint8_t*>(blob), static_cast<const uint8_t*>(blob) + blobSize);
                else
                    value.blob.clear();
                break;
            }
        }

        void __cdecl ImplEnumerate(void* ctx, EnumCallback callback)
        {
            std::lock_guard<std::mutex> lock(dataMutex);
            for (auto& [module, values] : data)
            {
                for (auto& [key, value] : values)
                    callback(ctx, module, key, value.type, value.str.c_str(), value.num, value.blob.data(), static_cast<uint32_t>(value.blob.size()));
            }
        }

        int32_t __cdecl ImplClaim(uint32_t what)
        {
            std::lock_guard<std::mutex> lock(dataMutex);
            return claims.insert(what).second ? 1 : 0;
        }

        RegistryInterface localRegistry = { sizeof(RegistryInterface), 1, ImplSet, ImplEnumerate, ImplClaim };
        std::atomic<RegistryInterface*> activeRegistry{ nullptr };

        HMODULE ModuleFromAddress(const void* address)
        {
            HMODULE module = NULL;
            if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, static_cast<LPCWSTR>(address), &module))
                return NULL;
            return module;
        }

        // The pointer comes from a named object that another process could have created first (the
        // installer may run elevated): only accept a registry that lives in a module loaded in this
        // process, with its functions in that same module.
        bool IsValidRegistry(const RegistryInterface* registry)
        {
            HMODULE module = ModuleFromAddress(registry);
            if (!module)
                return false;
            if (registry->cbSize < sizeof(RegistryInterface) || registry->version < 1)
                return false;
            return ModuleFromAddress(reinterpret_cast<const void*>(registry->Set)) == module &&
                   ModuleFromAddress(reinterpret_cast<const void*>(registry->Enumerate)) == module &&
                   ModuleFromAddress(reinterpret_cast<const void*>(registry->Claim)) == module;
        }

        RegistryInterface* GetRegistry()
        {
            if (auto registry = activeRegistry.load(std::memory_order_acquire))
                return registry;

            RegistryInterface* result = &localRegistry;
            bool owner = false;

            wchar_t name[64];
            swprintf_s(name, L"Local\\modupdater.registry.v2.%lu", GetCurrentProcessId());
            HANDLE mapping = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0, 64, name);
            if (mapping)
            {
                auto slot = static_cast<PVOID volatile*>(MapViewOfFile(mapping, FILE_MAP_ALL_ACCESS, 0, 0, 64));
                if (slot)
                {
                    void* previous = InterlockedCompareExchangePointer(slot, &localRegistry, nullptr);
                    if (previous == nullptr)
                    {
                        owner = true; // keep the mapping for the lifetime of the process
                    }
                    else
                    {
                        auto other = static_cast<RegistryInterface*>(previous);
                        if (IsValidRegistry(other))
                            result = other;
                        else
                            Log(L"Ignoring an invalid modupdater registry");
                        UnmapViewOfFile(const_cast<void**>(slot));
                        CloseHandle(mapping);
                    }
                }
                else
                {
                    CloseHandle(mapping);
                }
            }

            RegistryInterface* expected = nullptr;
            if (!activeRegistry.compare_exchange_strong(expected, result))
                return expected;

            if (owner)
            {
                // other modules keep pointers into this one, it must never be unloaded
                HMODULE self = NULL;
                GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN, reinterpret_cast<LPCWSTR>(&localRegistry), &self);
            }
            return result;
        }
    }

    void RegistrySetString(HMODULE module, uint32_t key, const char* value, bool append)
    {
        GetRegistry()->Set(module, key, kTypeString, value ? value : "", 0, nullptr, 0, append ? kFlagAppend : 0);
    }

    void RegistrySetInt(HMODULE module, uint32_t key, int64_t value)
    {
        GetRegistry()->Set(module, key, kTypeInt, nullptr, value, nullptr, 0, 0);
    }

    void RegistrySetBlob(HMODULE module, uint32_t key, const void* data, size_t size)
    {
        GetRegistry()->Set(module, key, kTypeBlob, nullptr, 0, data, static_cast<uint32_t>(size), 0);
    }

    RegistrySnapshot RegistryGetSnapshot()
    {
        RegistrySnapshot snapshot;
        GetRegistry()->Enumerate(&snapshot, [](void* ctx, HMODULE module, uint32_t key, uint32_t type, const char* str, int64_t num, const void* blob, uint32_t blobSize)
        {
            auto& value = (*static_cast<RegistrySnapshot*>(ctx))[module][key];
            if (type == kTypeString && str)
                value.str = str;
            else if (type == kTypeInt)
                value.num = num;
            else if (type == kTypeBlob && blob && blobSize)
                value.blob.assign(static_cast<const uint8_t*>(blob), static_cast<const uint8_t*>(blob) + blobSize);
        });
        return snapshot;
    }

    bool RegistryClaim(uint32_t what)
    {
        return GetRegistry()->Claim(what) != 0;
    }

    const RegistryValue* FindValue(const ModuleSettings& settings, uint32_t key)
    {
        auto it = settings.find(key);
        return it != settings.end() ? &it->second : nullptr;
    }

    std::string GetString(const ModuleSettings& settings, uint32_t key, const std::string& fallback)
    {
        auto v = FindValue(settings, key);
        return v ? v->str : fallback;
    }

    int64_t GetInt(const ModuleSettings& settings, uint32_t key, int64_t fallback)
    {
        auto v = FindValue(settings, key);
        return v ? v->num : fallback;
    }

    bool HasValue(const ModuleSettings& settings, uint32_t key)
    {
        return settings.contains(key);
    }

    namespace
    {
        void SetResource(HMODULE hModule, uint32_t key, const char* name, const char* type)
        {
            HRSRC resource = FindResourceA(hModule, name, type);
            HGLOBAL handle = resource ? LoadResource(hModule, resource) : nullptr;
            const void* bytes = handle ? LockResource(handle) : nullptr;
            DWORD size = resource ? SizeofResource(hModule, resource) : 0;
            if (bytes && size)
                RegistrySetBlob(hModule, key, bytes, size);
            else
                Log(L"Installer resource {} was not found", IS_INTRESOURCE(name) ? std::to_wstring(reinterpret_cast<uintptr_t>(name)) : toWString(name));
        }

        bool IsThemeValid(int theme)
        {
            if (theme == MU_THEME_LIGHT || theme == MU_THEME_DARK)
                return true;
            Log(L"Invalid installer theme {}, use MU_THEME_LIGHT or MU_THEME_DARK", theme);
            return false;
        }
    }
}

using namespace mu;

void muSetUpdateURL(HMODULE hModule, const char* url)
{
    RegistrySetString(hModule, K(Key::UpdateUrl), url);
}

void muSetDevUpdateURL(HMODULE hModule, const char* url)
{
    RegistrySetString(hModule, K(Key::DevUpdateUrl), url);
}

void muSetArchivePassword(HMODULE hModule, const char* password)
{
    RegistrySetString(hModule, K(Key::ArchivePassword), password);
}

void muSetSkipUpdateCompleteDialog(HMODULE hModule, bool skipcompletedialog)
{
    RegistrySetInt(hModule, K(Key::SkipUpdateCompleteDialog), skipcompletedialog);
}

void muSetAlwaysUpdate(HMODULE hModule, bool alwaysupdate)
{
    RegistrySetInt(hModule, K(Key::AlwaysUpdate), alwaysupdate);
}

void muSetInstallerIcon(HMODULE hModule, HICON icon)
{
    RegistrySetInt(hModule, K(Key::InstallerIcon), reinterpret_cast<intptr_t>(icon));
}

void muSetInstallerWindowTitle(HMODULE hModule, const char* title)
{
    RegistrySetString(hModule, K(Key::InstallerWindowTitle), title);
}

void muSetInstallerMainInstruction(HMODULE hModule, const char* maininstr)
{
    RegistrySetString(hModule, K(Key::InstallerMainInstruction), maininstr);
}

void muSetInstallerContent(HMODULE hModule, const char* content)
{
    RegistrySetString(hModule, K(Key::InstallerContent), content);
}

void muSetInstallerFooter(HMODULE hModule, const char* footer)
{
    RegistrySetString(hModule, K(Key::InstallerFooter), footer);
}

void muSetRGLAppID(HMODULE hModule, const char* id, const char* subfolder)
{
    RegistrySetString(hModule, K(Key::RglAppId), id);
    RegistrySetString(hModule, K(Key::RglSubfolder), subfolder);
}

void muSetSteamAppID(HMODULE hModule, const char* id, const char* subfolder)
{
    RegistrySetString(hModule, K(Key::SteamAppId), id);
    RegistrySetString(hModule, K(Key::SteamSubfolder), subfolder);
}

void muSetInstallerUI(HMODULE hModule, int ui)
{
    RegistrySetInt(hModule, K(Key::InstallerUI), ui);
}

void muSetInstallerLogo(HMODULE hModule, const void* image, unsigned int size)
{
    RegistrySetBlob(hModule, K(Key::InstallerLogo), image, size);
}

void muSetInstallerLogoResource(HMODULE hModule, const char* name, const char* type)
{
    SetResource(hModule, K(Key::InstallerLogo), name, type);
}

void muSetInstallerBackground(HMODULE hModule, const void* image, unsigned int size)
{
    RegistrySetBlob(hModule, K(Key::InstallerBackground), image, size);
}

void muSetInstallerBackgroundResource(HMODULE hModule, const char* name, const char* type)
{
    SetResource(hModule, K(Key::InstallerBackground), name, type);
}

void muSetInstallerBackgroundOverlay(HMODULE hModule, int opacityPercent)
{
    RegistrySetInt(hModule, K(Key::InstallerBackgroundOverlay), std::clamp(opacityPercent, 0, 100));
}

void muSetInstallerBackgroundBlur(HMODULE hModule, int radius)
{
    RegistrySetInt(hModule, K(Key::InstallerBackgroundBlur), std::clamp(radius, 0, 100));
}

void muSetInstallerTextBackdropBlur(HMODULE hModule, int radius)
{
    RegistrySetInt(hModule, K(Key::InstallerTextBackdropBlur), std::clamp(radius, 0, 100));
}

void muSetInstallerColor(HMODULE hModule, int element, COLORREF color)
{
    if (element >= 0 && element < MU_COLOR_COUNT)
        RegistrySetInt(hModule, Key::InstallerColorBase + element, color & 0x00FFFFFF);
}

void muSetInstallerGradient(HMODULE hModule, COLORREF top, COLORREF bottom)
{
    muSetInstallerColor(hModule, MU_COLOR_GRADIENT_TOP, top);
    muSetInstallerColor(hModule, MU_COLOR_GRADIENT_BOTTOM, bottom);
}

void muSetInstallerFont(HMODULE hModule, const char* family)
{
    RegistrySetString(hModule, K(Key::InstallerFontFamily), family);
}

void muSetInstallerFontData(HMODULE hModule, const void* font, unsigned int size)
{
    RegistrySetBlob(hModule, K(Key::InstallerFontData), font, size);
}

void muSetInstallerWindowSize(HMODULE hModule, int width, int height)
{
    RegistrySetInt(hModule, K(Key::InstallerWidth), std::max(0, width));
    RegistrySetInt(hModule, K(Key::InstallerHeight), std::max(0, height));
}

void muSetInstallerString(HMODULE hModule, int id, const char* text)
{
    if (id >= 0 && id < MU_STR_COUNT)
        RegistrySetString(hModule, Key::InstallerStringBase + id, text);
}

void muSetInstallerGameExecutable(HMODULE hModule, const char* exeNames)
{
    RegistrySetString(hModule, K(Key::InstallerGameExecutables), exeNames);
}

void muAddInstallerPath(HMODULE hModule, const char* path)
{
    if (path && *path)
        RegistrySetString(hModule, K(Key::InstallerExtraPaths), path, true);
}

void muSetInstallerIniMode(HMODULE hModule, int mode, bool userSelectable)
{
    RegistrySetInt(hModule, K(Key::InstallerIniMode), std::clamp(mode, MU_INI_MERGE, MU_INI_SKIP));
    RegistrySetInt(hModule, K(Key::InstallerIniSelectable), userSelectable);
}

void muSetLogFile(HMODULE hModule, const char* path)
{
    RegistrySetString(hModule, K(Key::LogFile), path);
}

void muSetInstallerTheme(HMODULE hModule, int theme)
{
    RegistrySetInt(hModule, K(Key::InstallerTheme), std::clamp(theme, MU_THEME_AUTO, MU_THEME_DARK));
}

void muSetInstallerThemeColor(HMODULE hModule, int theme, int element, COLORREF color)
{
    if (IsThemeValid(theme) && element >= 0 && element < MU_COLOR_COUNT)
        RegistrySetInt(hModule, (theme == MU_THEME_DARK ? Key::InstallerDarkColorBase : Key::InstallerLightColorBase) + element, color & 0x00FFFFFF);
}

void muSetInstallerThemeGradient(HMODULE hModule, int theme, COLORREF top, COLORREF bottom)
{
    muSetInstallerThemeColor(hModule, theme, MU_COLOR_GRADIENT_TOP, top);
    muSetInstallerThemeColor(hModule, theme, MU_COLOR_GRADIENT_BOTTOM, bottom);
}

void muSetInstallerThemeLogo(HMODULE hModule, int theme, const void* image, unsigned int size)
{
    if (IsThemeValid(theme))
        RegistrySetBlob(hModule, K(theme == MU_THEME_DARK ? Key::InstallerDarkLogo : Key::InstallerLightLogo), image, size);
}

void muSetInstallerThemeLogoResource(HMODULE hModule, int theme, const char* name, const char* type)
{
    if (IsThemeValid(theme))
        SetResource(hModule, K(theme == MU_THEME_DARK ? Key::InstallerDarkLogo : Key::InstallerLightLogo), name, type);
}

void muSetInstallerThemeBackground(HMODULE hModule, int theme, const void* image, unsigned int size)
{
    if (IsThemeValid(theme))
        RegistrySetBlob(hModule, K(theme == MU_THEME_DARK ? Key::InstallerDarkBackground : Key::InstallerLightBackground), image, size);
}

void muSetInstallerThemeBackgroundResource(HMODULE hModule, int theme, const char* name, const char* type)
{
    if (IsThemeValid(theme))
        SetResource(hModule, K(theme == MU_THEME_DARK ? Key::InstallerDarkBackground : Key::InstallerLightBackground), name, type);
}

void muSetInstallerThemeBackgroundOverlay(HMODULE hModule, int theme, int opacityPercent)
{
    if (IsThemeValid(theme))
        RegistrySetInt(hModule, K(theme == MU_THEME_DARK ? Key::InstallerDarkBackgroundOverlay : Key::InstallerLightBackgroundOverlay), std::clamp(opacityPercent, 0, 100));
}
