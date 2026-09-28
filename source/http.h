#pragma once
#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <string>

namespace mu::http
{
    struct CaseInsensitiveLess
    {
        bool operator()(const std::string& a, const std::string& b) const
        {
            return _stricmp(a.c_str(), b.c_str()) < 0;
        }
    };

    struct Response
    {
        long status = 0;        // HTTP status code of the final response, 0 if there was none
        std::string error;      // transport level error (DNS, TLS, timeout...), empty on success
        std::string url;        // effective URL after redirects
        std::map<std::string, std::string, CaseInsensitiveLess> headers;
        std::string body;

        bool ok() const { return error.empty() && status >= 200 && status < 300; }
        std::string Header(const std::string& name) const;
        std::wstring Describe() const; // human readable error ("HTTP 404 Not Found", "Couldn't resolve host...")
    };

    struct Options
    {
        std::string token;          // sent as "Authorization: Bearer <token>" when not empty
        int connectTimeoutMs = 20000;
        int timeoutMs = 60000;      // total time limit for Head/Get
        int lowSpeedLimit = 512;    // DownloadToFile: abort when slower than this many bytes/s...
        int lowSpeedTimeSec = 45;   // ...for this many seconds (the transfer is then resumed)
        int maxAttempts = 4;        // DownloadToFile: attempts for transient failures
    };

    Response Head(const std::string& url, const Options& options = {});
    Response Get(const std::string& url, const Options& options = {});
    // Header-only GET (Range: bytes=0-0, body is not downloaded) for servers that refuse HEAD.
    // Content-Length is set to the full size of the resource when the server reports it.
    Response Probe(const std::string& url, const Options& options = {});

    // The token is only ever sent to GitHub hosts
    bool IsGitHubHost(const std::string& url);

    struct DownloadResult
    {
        bool ok = false;
        bool cancelled = false;
        long status = 0;
        uint64_t bytes = 0;
        std::wstring error;
    };

    // Receives the number of bytes downloaded so far and the total size (0 if unknown).
    // Return false to cancel.
    using Progress = std::function<bool(uint64_t done, uint64_t total)>;

    // Streams 'url' into 'file' (created/overwritten). Transient failures are retried and
    // interrupted transfers are resumed with range requests when the server supports it.
    DownloadResult DownloadToFile(const std::string& url, const std::filesystem::path& file, const Progress& progress, const Options& options = {});

    std::wstring DescribeStatus(long status);
    const char* UserAgent();
}
