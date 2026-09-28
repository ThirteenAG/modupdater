#pragma once
#include <chrono>
#include <cstdint>
#include <string>

namespace Json { class Value; }

namespace mu
{
    constexpr int32_t kRemoteNotFound = -1;     // nothing to download
    constexpr int32_t kRemoteDateUnknown = -2;  // found, but the server did not tell when it was updated

    struct RemoteFileInfo
    {
        int32_t hoursAgo = kRemoteNotFound;    // hours since the remote file was updated, or kRemote*
        std::string url;                       // download URL
        std::string name;                      // file name of the download
        uint64_t size = 0;                     // 0 = unknown
        std::wstring error;                    // why nothing was found (for logs and error messages)

        bool found() const { return hoursAgo != kRemoteNotFound && !url.empty(); }
    };

    // Resolves an update URL for a local file. GitHub repository pages are resolved with the GitHub
    // API: the newest asset whose name starts with the local file name (without extension) is used,
    // assets for the other architecture ('machine' = "x86"/"x64") are skipped, and when nothing
    // matches the latest release is used if it has a single asset. Everything else is treated as
    // a direct download link.
    RemoteFileInfo GetRemoteFileInfo(const std::wstring& fileName, const std::string& url, const std::wstring& machine, const std::string& token);

    // Resolves a download for the installer: direct links are used as they are, GitHub repository
    // pages resolve to the latest release (its only asset, or its only .zip asset).
    RemoteFileInfo GetInstallerDownloadInfo(const std::string& url, const std::string& token);

    // Helpers, exposed for tests
    bool ParseGitHubRepo(const std::string& url, std::string& user, std::string& repo);
    std::string ParseContentDispositionFileName(const std::string& header);
    int ParseLastPageFromLinkHeader(const std::string& link);
    bool ParseHttpDate(const std::string& text, std::chrono::system_clock::time_point& time);
    bool ParseIsoDate(const std::string& text, std::chrono::system_clock::time_point& time);
    int32_t HoursSince(std::chrono::system_clock::time_point time, std::chrono::system_clock::time_point now = std::chrono::system_clock::now());
    RemoteFileInfo SelectGitHubAsset(const Json::Value& releases, const std::wstring& fileName, const std::wstring& machine, std::chrono::system_clock::time_point now);
    void SetGitHubApiBase(const std::string& base); // default https://api.github.com
}
