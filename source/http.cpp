#include "stdafx.h"
#include "http.h"
#include "fileops.h"
#include "log.h"
#include "string_funcs.h"
#include <cpr/cpr.h>

namespace mu::http
{
    bool IsGitHubHost(const std::string& url)
    {
        auto begin = url.find("://");
        begin = (begin == std::string::npos) ? 0 : begin + 3;
        auto host = toLowerStr(url.substr(begin, url.find_first_of("/:?#", begin) - begin));
        return host == "github.com" || host == "api.github.com" || host == "www.github.com";
    }

    namespace
    {
        cpr::Header BuildHeaders(const std::string& url, const Options& options, uint64_t rangeFrom = 0)
        {
            cpr::Header header;
            if (!options.token.empty() && IsGitHubHost(url))
                header["Authorization"] = "Bearer " + options.token;
            if (rangeFrom > 0)
                header["Range"] = "bytes=" + std::to_string(rangeFrom) + "-";
            return header;
        }

        void Configure(cpr::Session& session, const std::string& url, const Options& options)
        {
            session.SetUrl(cpr::Url{ url });
            session.SetHeader(BuildHeaders(url, options));
            session.SetUserAgent(cpr::UserAgent{ UserAgent() });
            session.SetConnectTimeout(cpr::ConnectTimeout{ options.connectTimeoutMs });
            session.SetTimeout(cpr::Timeout{ options.timeoutMs });
        }

        Response Convert(const cpr::Response& r)
        {
            Response response;
            response.status = r.status_code;
            response.url = r.url.str();
            if (r.error)
                response.error = r.error.message.empty() ? "network error " + std::to_string(static_cast<int>(r.error.code)) : r.error.message;
            for (auto& [key, value] : r.header)
                response.headers[key] = value;
            response.body = r.text;
            return response;
        }

        std::wstring DescribeTransportError(const std::string& message)
        {
            return toWString(message);
        }

        bool IsTransientStatus(long status)
        {
            return status == 408 || status == 429 || status >= 500;
        }
    }

    const char* UserAgent()
    {
        return "modupdater/2.0 (+https://github.com/ThirteenAG/modupdater)";
    }

    std::wstring DescribeStatus(long status)
    {
        const wchar_t* text = L"";
        switch (status)
        {
        case 400: text = L"Bad Request"; break;
        case 401: text = L"Unauthorized"; break;
        case 403: text = L"Forbidden"; break;
        case 404: text = L"Not Found"; break;
        case 405: text = L"Method Not Allowed"; break;
        case 408: text = L"Request Timeout"; break;
        case 410: text = L"Gone"; break;
        case 416: text = L"Range Not Satisfiable"; break;
        case 429: text = L"Too Many Requests"; break;
        case 500: text = L"Internal Server Error"; break;
        case 502: text = L"Bad Gateway"; break;
        case 503: text = L"Service Unavailable"; break;
        case 504: text = L"Gateway Timeout"; break;
        }
        return *text ? Format(L"HTTP {} {}", status, text) : Format(L"HTTP {}", status);
    }

    std::string Response::Header(const std::string& name) const
    {
        auto it = headers.find(name);
        return it != headers.end() ? it->second : std::string();
    }

    std::wstring Response::Describe() const
    {
        if (!error.empty())
            return DescribeTransportError(error);
        if (status == 0)
            return L"no response from the server";
        return DescribeStatus(status);
    }

    Response Head(const std::string& url, const Options& options)
    {
        try
        {
            cpr::Session session;
            Configure(session, url, options);
            return Convert(session.Head());
        }
        catch (const std::exception& e)
        {
            Response r;
            r.error = e.what();
            return r;
        }
    }

    Response Get(const std::string& url, const Options& options)
    {
        try
        {
            cpr::Session session;
            Configure(session, url, options);
            return Convert(session.Get());
        }
        catch (const std::exception& e)
        {
            Response r;
            r.error = e.what();
            return r;
        }
    }

    Response Probe(const std::string& url, const Options& options)
    {
        try
        {
            cpr::Session session;
            Configure(session, url, options);
            auto headers = BuildHeaders(url, options);
            headers["Range"] = "bytes=0-0";
            session.SetHeader(headers);

            // stop as soon as the body starts, in case the server ignores the range
            auto r = session.Download(cpr::WriteCallback{ [](const std::string_view&, intptr_t) { return false; } });
            Response response = Convert(r);
            if (response.status == 200 || response.status == 206)
                response.error.clear(); // aborted on purpose

            if (response.status == 206)
            {
                // Content-Range: bytes 0-0/123456
                auto range = response.Header("Content-Range");
                auto slash = range.find('/');
                if (slash != std::string::npos && range.substr(slash + 1) != "*")
                    response.headers["Content-Length"] = range.substr(slash + 1);
                else
                    response.headers.erase("Content-Length");
                response.status = 200;
            }
            response.body.clear();
            return response;
        }
        catch (const std::exception& e)
        {
            Response r;
            r.error = e.what();
            return r;
        }
    }

    DownloadResult DownloadToFile(const std::string& url, const std::filesystem::path& file, const Progress& progress, const Options& options)
    {
        DownloadResult result;

        HANDLE hFile = CreateFileW(file.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (hFile == INVALID_HANDLE_VALUE)
        {
            result.error = Format(L"Cannot create {}: {}", file.wstring(), Win32ErrorMessage(GetLastError()));
            return result;
        }

        uint64_t written = 0;
        uint64_t expectedTotal = 0;
        std::wstring lastError;
        bool cancelled = false;

        // waits between attempts but stays responsive to cancellation
        auto backoff = [&](int attempt)
        {
            for (int i = 0; i < attempt * 10 && !cancelled; i++)
            {
                if (progress && !progress(written, expectedTotal))
                    cancelled = true;
                else
                    Sleep(100);
            }
        };

        for (int attempt = 1; attempt <= std::max(1, options.maxAttempts) && !cancelled; attempt++)
        {
            const uint64_t rangeFrom = written;
            long responseStatus = 0;
            uint64_t contentLength = 0;
            bool bodyChecked = false;
            bool acceptBody = false;
            DWORD writeError = ERROR_SUCCESS;

            cpr::Session session;
            Configure(session, url, options);
            session.SetTimeout(cpr::Timeout{ 0 }); // no total limit for large files, LowSpeed detects stalls
            session.SetHeader(BuildHeaders(url, options, rangeFrom));
            session.SetLowSpeed(cpr::LowSpeed{ options.lowSpeedLimit, std::chrono::seconds(options.lowSpeedTimeSec) });
            session.SetHeaderCallback(cpr::HeaderCallback{ [&](const std::string_view& line, intptr_t) -> bool
            {
                // every response in a redirect chain starts with a status line
                if (line.starts_with("HTTP/"))
                {
                    auto space = line.find(' ');
                    responseStatus = (space != std::string_view::npos) ? std::atol(std::string(line.substr(space + 1, 3)).c_str()) : 0;
                    contentLength = 0;
                }
                else if (starts_with(line, "content-length:", false))
                {
                    contentLength = std::strtoull(std::string(line.substr(15)).c_str(), nullptr, 10);
                }
                return true;
            } });
            session.SetProgressCallback(cpr::ProgressCallback{ [&](cpr::cpr_pf_arg_t, cpr::cpr_pf_arg_t, cpr::cpr_pf_arg_t, cpr::cpr_pf_arg_t, intptr_t) -> bool
            {
                if (progress && !progress(written, expectedTotal))
                    cancelled = true;
                return !cancelled;
            } });

            cpr::Response r;
            try
            {
                r = session.Download(cpr::WriteCallback{ [&](const std::string_view& data, intptr_t) -> bool
                {
                    if (!bodyChecked)
                    {
                        bodyChecked = true;
                        acceptBody = (responseStatus == 200 || responseStatus == 206);
                        if (acceptBody)
                        {
                            if (rangeFrom > 0 && responseStatus == 200)
                            {
                                // the server ignored the range request, start from scratch
                                LARGE_INTEGER zero = {};
                                SetFilePointerEx(hFile, zero, nullptr, FILE_BEGIN);
                                SetEndOfFile(hFile);
                                written = 0;
                            }
                            if (contentLength)
                                expectedTotal = written + contentLength;
                        }
                    }

                    if (!acceptBody)
                        return true; // error page, discard

                    DWORD done = 0;
                    if (!WriteFile(hFile, data.data(), static_cast<DWORD>(data.size()), &done, nullptr) || done != data.size())
                    {
                        writeError = GetLastError();
                        return false;
                    }
                    written += done;
                    return !cancelled;
                } });
            }
            catch (const std::exception& e)
            {
                r.error = cpr::Error(CURLE_FAILED_INIT, std::string(e.what()));
            }

            result.status = r.status_code;

            if (cancelled)
                break;

            if (writeError != ERROR_SUCCESS)
            {
                lastError = Format(L"Cannot write {}: {}", file.filename().wstring(), Win32ErrorMessage(writeError));
                break; // disk full or similar, retrying will not help
            }

            if (!r.error && (r.status_code == 200 || r.status_code == 206))
            {
                if (expectedTotal == 0 || written >= expectedTotal)
                {
                    result.ok = true;
                    break;
                }
                lastError = Format(L"the connection was closed after {} of {} bytes", written, expectedTotal);
            }
            else if (!r.error && r.status_code == 416 && rangeFrom > 0)
            {
                if (expectedTotal && written == expectedTotal)
                {
                    result.ok = true;
                    break;
                }
                // our partial file does not match, start over
                LARGE_INTEGER zero = {};
                SetFilePointerEx(hFile, zero, nullptr, FILE_BEGIN);
                SetEndOfFile(hFile);
                written = 0;
                lastError = DescribeStatus(416);
            }
            else if (!r.error && !IsTransientStatus(r.status_code))
            {
                lastError = DescribeStatus(r.status_code);
                break; // 404 and friends are final
            }
            else
            {
                lastError = r.error ? DescribeTransportError(r.error.message) : DescribeStatus(r.status_code);
            }

            if (attempt < options.maxAttempts)
            {
                Log(L"Download attempt {} failed ({}), retrying{}", attempt, lastError, written ? Format(L" from byte {}", written) : L"");
                backoff(attempt);
            }
        }

        CloseHandle(hFile);

        result.bytes = written;
        result.cancelled = cancelled && !result.ok;
        if (!result.ok)
        {
            result.error = result.cancelled ? L"Download cancelled" : lastError;
            DeleteFileW(file.c_str());
        }
        return result;
    }
}
