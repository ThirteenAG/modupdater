#pragma once
#include <windows.h>
#include <filesystem>
#include <string>
#include <vector>

namespace mu
{
    // Minimal Valve KeyValues (VDF) document
    struct VdfNode
    {
        std::string key;
        std::string value;
        std::vector<VdfNode> children;
        bool isObject = false;

        const VdfNode* Child(std::string_view name) const; // case-insensitive
        std::string Value(std::string_view name) const;    // value of a direct child, or ""
    };

    bool ParseVdf(const std::string& text, VdfNode& root);

    // Library folders from steamapps\libraryfolders.vdf (current and legacy formats)
    std::vector<std::filesystem::path> ParseSteamLibraryFolders(const std::string& vdfText);

    struct SteamAppManifest
    {
        std::string appId;
        std::string name;
        std::string installDir;
    };
    bool ParseSteamAppManifest(const std::string& acfText, SteamAppManifest& manifest);

    // Returns the install folder (+ subfolder) of a Steam game given its AppID or name, or empty
    std::filesystem::path FindSteamGame(const std::string& appIdOrName, const std::string& subfolder);

    // Returns the install folder (+ subfolder) of a Rockstar Games Launcher game, or empty
    std::filesystem::path FindRockstarGame(const std::string& gameName, const std::string& subfolder);

    std::wstring ReadRegistryString(HKEY root, const std::wstring& subKey, const std::wstring& valueName);
}
