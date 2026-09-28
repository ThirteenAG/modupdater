#include "stdafx.h"
#include "gamepaths.h"
#include "log.h"
#include "string_funcs.h"

namespace mu
{
    namespace
    {
        class VdfTokenizer
        {
        public:
            explicit VdfTokenizer(const std::string& text) : s(text) {}

            // returns false at the end of the input; 'quoted' tells strings from braces
            bool Next(std::string& token, bool& quoted)
            {
                token.clear();
                quoted = false;
                for (;;)
                {
                    while (pos < s.size() && isspace(static_cast<unsigned char>(s[pos])))
                        pos++;
                    if (pos + 1 < s.size() && s[pos] == '/' && s[pos + 1] == '/')
                    {
                        while (pos < s.size() && s[pos] != '\n')
                            pos++;
                        continue;
                    }
                    break;
                }

                if (pos >= s.size())
                    return false;

                char c = s[pos];
                if (c == '{' || c == '}')
                {
                    token = c;
                    pos++;
                    return true;
                }

                if (c == '"')
                {
                    quoted = true;
                    pos++;
                    while (pos < s.size() && s[pos] != '"')
                    {
                        if (s[pos] == '\\' && pos + 1 < s.size())
                        {
                            char e = s[pos + 1];
                            switch (e)
                            {
                            case 'n': token.push_back('\n'); break;
                            case 't': token.push_back('\t'); break;
                            case '\\': token.push_back('\\'); break;
                            case '"': token.push_back('"'); break;
                            default: token.push_back('\\'); token.push_back(e); break;
                            }
                            pos += 2;
                        }
                        else
                            token.push_back(s[pos++]);
                    }
                    pos++; // closing quote
                    return true;
                }

                // unquoted token
                quoted = true;
                while (pos < s.size() && !isspace(static_cast<unsigned char>(s[pos])) && s[pos] != '{' && s[pos] != '}' && s[pos] != '"')
                    token.push_back(s[pos++]);
                return true;
            }

        private:
            const std::string& s;
            size_t pos = 0;
        };

        bool ParseObject(VdfTokenizer& tokenizer, std::vector<VdfNode>& children, int depth)
        {
            if (depth > 64)
                return false;

            std::string token;
            bool quoted = false;
            while (tokenizer.Next(token, quoted))
            {
                if (!quoted && token == "}")
                    return true;
                if (!quoted)
                    return false;

                // conditionals like [$WIN32] after a value
                if (token.size() > 1 && token.front() == '[' && token.back() == ']')
                    continue;

                VdfNode node;
                node.key = token;
                if (!tokenizer.Next(token, quoted))
                    return false;

                if (!quoted && token == "{")
                {
                    node.isObject = true;
                    if (!ParseObject(tokenizer, node.children, depth + 1))
                        return false;
                }
                else if (quoted)
                {
                    node.value = token;
                }
                else
                {
                    return false;
                }
                children.push_back(std::move(node));
            }
            return depth == 0;
        }

        std::string ReadTextFile(const std::filesystem::path& path)
        {
            std::ifstream file(path, std::ios::binary);
            if (!file)
                return {};
            return std::string((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
        }

        std::filesystem::path FromUtf8Path(const std::string& path)
        {
            auto w = toWString(path);
            std::replace(w.begin(), w.end(), L'/', L'\\');
            return std::filesystem::path(w).lexically_normal();
        }

        bool IsDirectory(const std::filesystem::path& p)
        {
            std::error_code ec;
            return !p.empty() && std::filesystem::is_directory(p, ec);
        }

        std::filesystem::path WithSubfolder(const std::filesystem::path& base, const std::string& subfolder)
        {
            auto path = base;
            if (!subfolder.empty())
                path /= toWString(subfolder);
            return path.lexically_normal();
        }
    }

    const VdfNode* VdfNode::Child(std::string_view name) const
    {
        for (auto& c : children)
        {
            if (iequals(c.key, name))
                return &c;
        }
        return nullptr;
    }

    std::string VdfNode::Value(std::string_view name) const
    {
        auto c = Child(name);
        return (c && !c->isObject) ? c->value : std::string();
    }

    bool ParseVdf(const std::string& text, VdfNode& root)
    {
        root = {};
        root.isObject = true;
        size_t start = (text.size() >= 3 && text.compare(0, 3, "\xEF\xBB\xBF") == 0) ? 3 : 0;
        std::string body = text.substr(start);
        VdfTokenizer tokenizer(body);
        return ParseObject(tokenizer, root.children, 0);
    }

    std::vector<std::filesystem::path> ParseSteamLibraryFolders(const std::string& vdfText)
    {
        std::vector<std::filesystem::path> result;
        VdfNode root;
        if (!ParseVdf(vdfText, root))
            return result;

        auto folders = root.Child("libraryfolders");
        if (!folders || !folders->isObject)
            return result;

        for (auto& entry : folders->children)
        {
            std::string path;
            if (entry.isObject)
                path = entry.Value("path");                     // current format
            else if (!entry.key.empty() && std::all_of(entry.key.begin(), entry.key.end(), [](char c) { return c >= '0' && c <= '9'; }))
                path = entry.value;                             // legacy format: "1" "D:\\SteamLibrary"

            if (!path.empty())
                result.push_back(FromUtf8Path(path));
        }
        return result;
    }

    bool ParseSteamAppManifest(const std::string& acfText, SteamAppManifest& manifest)
    {
        VdfNode root;
        if (!ParseVdf(acfText, root))
            return false;
        auto state = root.Child("AppState");
        if (!state)
            return false;
        manifest.appId = state->Value("appid");
        manifest.name = state->Value("name");
        manifest.installDir = state->Value("installdir");
        return !manifest.installDir.empty();
    }

    std::wstring ReadRegistryString(HKEY root, const std::wstring& subKey, const std::wstring& valueName)
    {
        DWORD size = 0;
        if (RegGetValueW(root, subKey.c_str(), valueName.c_str(), RRF_RT_REG_SZ | RRF_RT_REG_EXPAND_SZ, nullptr, nullptr, &size) != ERROR_SUCCESS || size == 0)
            return {};

        std::wstring value(size / sizeof(wchar_t) + 1, L'\0');
        size = static_cast<DWORD>(value.size() * sizeof(wchar_t));
        if (RegGetValueW(root, subKey.c_str(), valueName.c_str(), RRF_RT_REG_SZ | RRF_RT_REG_EXPAND_SZ, nullptr, value.data(), &size) != ERROR_SUCCESS)
            return {};

        value.resize(wcsnlen(value.c_str(), value.size()));
        return value;
    }

    std::filesystem::path FindSteamGame(const std::string& appIdOrName, const std::string& subfolder)
    {
        if (appIdOrName.empty())
            return {};

        const bool isAppId = std::all_of(appIdOrName.begin(), appIdOrName.end(), [](char c) { return c >= '0' && c <= '9'; });

        std::wstring steamPath = ReadRegistryString(HKEY_CURRENT_USER, L"Software\\Valve\\Steam", L"SteamPath");
        if (steamPath.empty())
            steamPath = ReadRegistryString(HKEY_LOCAL_MACHINE, L"SOFTWARE\\WOW6432Node\\Valve\\Steam", L"InstallPath");
        if (steamPath.empty())
            steamPath = ReadRegistryString(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Valve\\Steam", L"InstallPath");

        std::vector<std::filesystem::path> libraries;
        if (!steamPath.empty())
        {
            std::replace(steamPath.begin(), steamPath.end(), L'/', L'\\');
            std::filesystem::path steamRoot = std::filesystem::path(steamPath).lexically_normal();
            if (IsDirectory(steamRoot))
                libraries.push_back(steamRoot);

            for (auto& library : ParseSteamLibraryFolders(ReadTextFile(steamRoot / L"steamapps" / L"libraryfolders.vdf")))
            {
                bool known = std::any_of(libraries.begin(), libraries.end(), [&](const std::filesystem::path& p)
                {
                    std::error_code ec;
                    return iequals(p.wstring(), library.wstring()) || std::filesystem::equivalent(p, library, ec);
                });
                if (!known && IsDirectory(library))
                    libraries.push_back(library);
            }
        }

        for (auto& library : libraries)
        {
            auto steamApps = library / L"steamapps";
            if (!IsDirectory(steamApps))
                continue;

            auto check = [&](const std::filesystem::path& manifestPath) -> std::filesystem::path
            {
                SteamAppManifest manifest;
                if (!ParseSteamAppManifest(ReadTextFile(manifestPath), manifest))
                    return {};
                bool matches = isAppId ? manifest.appId == appIdOrName : iequals(manifest.name, appIdOrName);
                if (!matches)
                    return {};
                auto gamePath = WithSubfolder(steamApps / L"common" / toWString(manifest.installDir), subfolder);
                return IsDirectory(gamePath) ? gamePath : std::filesystem::path();
            };

            if (isAppId)
            {
                if (auto found = check(steamApps / (L"appmanifest_" + toWString(appIdOrName) + L".acf")); !found.empty())
                    return found;
                continue;
            }

            std::error_code ec;
            for (auto& entry : std::filesystem::directory_iterator(steamApps, ec))
            {
                auto name = entry.path().filename().wstring();
                if (starts_with(name, L"appmanifest_", false) && ends_with(name, L".acf", false))
                {
                    if (auto found = check(entry.path()); !found.empty())
                        return found;
                }
            }
        }

        // Steam also registers an uninstall entry for every installed game
        if (isAppId)
        {
            for (auto root : { L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\Steam App ", L"SOFTWARE\\WOW6432Node\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\Steam App " })
            {
                auto location = ReadRegistryString(HKEY_LOCAL_MACHINE, root + toWString(appIdOrName), L"InstallLocation");
                if (!location.empty())
                {
                    auto gamePath = WithSubfolder(location, subfolder);
                    if (IsDirectory(gamePath))
                        return gamePath;
                }
            }
        }

        return {};
    }

    std::filesystem::path FindRockstarGame(const std::string& gameName, const std::string& subfolder)
    {
        if (gameName.empty())
            return {};

        for (auto root : { L"SOFTWARE\\WOW6432Node\\Rockstar Games\\", L"SOFTWARE\\Rockstar Games\\" })
        {
            auto installFolder = ReadRegistryString(HKEY_LOCAL_MACHINE, root + toWString(gameName), L"InstallFolder");
            if (installFolder.empty())
                continue;
            auto gamePath = WithSubfolder(installFolder, subfolder);
            if (IsDirectory(gamePath))
                return gamePath;
        }
        return {};
    }
}
