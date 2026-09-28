#include "stdafx.h"
#include "remote.h"
#include "http.h"
#include "log.h"
#include "string_funcs.h"
#include <date.h>
#include <json/json.h>

namespace mu
{
    namespace
    {
        std::string gitHubApiBase = "https://api.github.com";

        std::string FileNameFromUrl(const std::string& url)
        {
            auto path = url.substr(0, url.find_first_of("?#"));
            auto slash = path.find_last_of('/');
            auto name = (slash != std::string::npos) ? path.substr(slash + 1) : path;
            // minimal percent-decoding for display and file names
            std::string decoded;
            for (size_t i = 0; i < name.size(); i++)
            {
                if (name[i] == '%' && i + 2 < name.size() && isxdigit(static_cast<unsigned char>(name[i + 1])) && isxdigit(static_cast<unsigned char>(name[i + 2])))
                {
                    decoded.push_back(static_cast<char>(std::stoi(name.substr(i + 1, 2), nullptr, 16)));
                    i += 2;
                }
                else
                    decoded.push_back(name[i]);
            }
            return decoded;
        }

        bool IsTextContent(const http::Response& r)
        {
            auto type = r.Header("Content-Type");
            return starts_with(type, "text", false) || starts_with(type, "application/json", false);
        }

        bool ParseJson(const std::string& text, Json::Value& root)
        {
            Json::CharReaderBuilder builder;
            std::unique_ptr<Json::CharReader> reader(builder.newCharReader());
            std::string errors;
            return reader->parse(text.data(), text.data() + text.size(), &root, &errors);
        }

        uint64_t ParseSize(const std::string& text)
        {
            return text.empty() ? 0 : std::strtoull(text.c_str(), nullptr, 10);
        }

        uint64_t JsonSize(const Json::Value& v)
        {
            if (v.isUInt64())
                return v.asUInt64();
            if (v.isString())
                return ParseSize(v.asString());
            return 0;
        }

        bool IsForOtherArchitecture(std::string assetName, const std::wstring& machine)
        {
            assetName = toLowerStr(assetName);
            if (machine == L"x64")
            {
                // "x86_64" is a 64-bit name, don't let it match "x86"
                string_replace_all(assetName, std::string("x86_64"), std::string("amd64"));
                string_replace_all(assetName, std::string("x86-64"), std::string("amd64"));
                for (auto token : { "x86", "32bit", "32-bit", "win32", "win-32" })
                {
                    if (assetName.find(token) != std::string::npos)
                        return true;
                }
            }
            else if (machine == L"x86")
            {
                for (auto token : { "x64", "64bit", "64-bit", "x86_64", "x86-64", "amd64", "win64", "win-64" })
                {
                    if (assetName.find(token) != std::string::npos)
                        return true;
                }
            }
            return false;
        }

        int32_t AssetHoursAgo(const Json::Value& asset, std::chrono::system_clock::time_point now)
        {
            std::chrono::system_clock::time_point updated;
            if (ParseIsoDate(asset["updated_at"].asString(), updated))
                return HoursSince(updated, now);
            return kRemoteDateUnknown;
        }

        std::string GitHubReleasesUrl(const std::string& user, const std::string& repo, int perPage)
        {
            return gitHubApiBase + "/repos/" + user + "/" + repo + "/releases?per_page=" + std::to_string(perPage);
        }

        bool IsGitHubPage(const std::string& url, const http::Response& head)
        {
            return url.find("github.com") != std::string::npos && IsTextContent(head);
        }

        RemoteFileInfo DirectLinkInfo(const std::string& url, const std::wstring& fallbackName, const std::string& token)
        {
            RemoteFileInfo info;
            Log(L"Connecting to {}", toWString(url));

            http::Options options;
            options.token = token;
            auto head = http::Head(url, options);
            if (head.status == 405 || head.status == 501)
            {
                // HEAD is not allowed, look at the headers of a GET request instead
                auto probe = http::Probe(url, options);
                if (probe.ok())
                    head = std::move(probe);
            }

            if (head.ok())
            {
                info.url = head.url.empty() ? url : head.url;
                info.name = ParseContentDispositionFileName(head.Header("Content-Disposition"));
                if (info.name.empty())
                    info.name = FileNameFromUrl(info.url);
                if (info.name.empty())
                    info.name = toString(fallbackName);
                info.size = ParseSize(head.Header("Content-Length"));

                std::chrono::system_clock::time_point modified;
                info.hoursAgo = ParseHttpDate(head.Header("Last-Modified"), modified) ? HoursSince(modified) : kRemoteDateUnknown;
                Log(L"Found {} ({})", toWString(info.name), toWString(head.Header("Content-Type")));
            }
            else if (head.status == 403)
            {
                // some hosts reject HEAD requests, assume the link is valid and let the download decide
                info.url = url;
                info.name = FileNameFromUrl(url);
                auto end = info.name.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ01234567890_.-");
                if (end != std::string::npos && end != 0)
                    info.name.erase(end);
                info.hoursAgo = 0;
                Log(L"Found {} (server did not allow HEAD request)", toWString(info.name));
            }
            else
            {
                info.error = head.Describe();
                Log(L"Seems like this archive is invalid or the url is broken: {}", info.error);
            }
            return info;
        }
    }

    void SetGitHubApiBase(const std::string& base)
    {
        gitHubApiBase = base;
    }

    bool ParseGitHubRepo(const std::string& url, std::string& user, std::string& repo)
    {
        static const std::string host = "github.com/";
        auto pos = url.find(host);
        if (pos == std::string::npos)
            return false;

        auto path = url.substr(pos + host.size());
        if (path.starts_with("repos/"))
            path.erase(0, 6);

        auto slash = path.find('/');
        if (slash == std::string::npos || slash == 0)
            return false;

        user = path.substr(0, slash);
        repo = path.substr(slash + 1);
        repo = repo.substr(0, repo.find_first_of("/?#"));
        if (repo.ends_with(".git"))
            repo.erase(repo.size() - 4);
        return !user.empty() && !repo.empty();
    }

    std::string ParseContentDispositionFileName(const std::string& header)
    {
        auto lower = toLowerStr(header);
        std::string name;

        // RFC 5987: filename*=UTF-8''name%20with%20spaces.zip
        auto ext = lower.find("filename*=");
        if (ext != std::string::npos)
        {
            auto value = header.substr(ext + 10);
            value = value.substr(0, value.find(';'));
            value = trimString(value);
            auto quotes = value.find("''");
            if (quotes != std::string::npos)
                value = value.substr(quotes + 2);
            removeQuotesFromString(value);
            name = FileNameFromUrl(value); // percent-decoding
        }

        if (name.empty())
        {
            size_t search = 0;
            while ((search = lower.find("filename", search)) != std::string::npos)
            {
                auto afterKey = search + 8;
                while (afterKey < lower.size() && lower[afterKey] == ' ')
                    afterKey++;
                if (afterKey < lower.size() && lower[afterKey] == '=')
                {
                    auto value = header.substr(afterKey + 1);
                    if (!value.empty() && value.front() == '"')
                        value = value.substr(1, value.find('"', 1) - 1);
                    else
                        value = trimString(value.substr(0, value.find(';')));
                    name = value;
                    break;
                }
                search = afterKey;
            }
        }

        // never trust a path from the server
        auto slash = name.find_last_of("/\\");
        if (slash != std::string::npos)
            name = name.substr(slash + 1);
        return trimString(name);
    }

    int ParseLastPageFromLinkHeader(const std::string& link)
    {
        // <https://api.github.com/...&page=2>; rel="next", <https://api.github.com/...&page=5>; rel="last"
        size_t pos = 0;
        while (pos < link.size())
        {
            auto end = link.find(',', pos);
            auto part = link.substr(pos, end == std::string::npos ? std::string::npos : end - pos);
            if (part.find("rel=\"last\"") != std::string::npos)
            {
                auto urlEnd = part.find('>');
                auto url = part.substr(0, urlEnd);
                for (auto key : { "?page=", "&page=" })
                {
                    auto p = url.find(key);
                    if (p != std::string::npos)
                    {
                        int page = std::atoi(url.c_str() + p + 6);
                        return std::clamp(page, 1, 50);
                    }
                }
            }
            if (end == std::string::npos)
                break;
            pos = end + 1;
        }
        return 1;
    }

    bool ParseHttpDate(const std::string& text, std::chrono::system_clock::time_point& time)
    {
        if (text.empty())
            return false;
        date::sys_seconds tp;
        std::istringstream ss{ text };
        ss >> date::parse("%a, %d %b %Y %H:%M:%S %Z", tp); // Tue, 12 May 2026 07:29:48 GMT
        if (!ss)
            return false;
        time = tp;
        return true;
    }

    bool ParseIsoDate(const std::string& text, std::chrono::system_clock::time_point& time)
    {
        if (text.empty())
            return false;
        date::sys_seconds tp;
        std::istringstream ss{ text };
        ss >> date::parse("%FT%TZ", tp); // 2016-08-16T11:42:53Z
        if (!ss)
            return false;
        time = tp;
        return true;
    }

    int32_t HoursSince(std::chrono::system_clock::time_point time, std::chrono::system_clock::time_point now)
    {
        auto hours = std::chrono::duration_cast<std::chrono::hours>(now - time).count();
        return static_cast<int32_t>(std::clamp<int64_t>(hours, 0, INT32_MAX));
    }

    RemoteFileInfo SelectGitHubAsset(const Json::Value& releases, const std::wstring& fileName, const std::wstring& machine, std::chrono::system_clock::time_point now)
    {
        RemoteFileInfo best;
        if (!releases.isArray())
            return best;

        auto stem = fileName;
        auto dot = stem.find_last_of(L'.');
        if (dot != std::wstring::npos)
            stem.erase(dot);
        else
            stem.append(L".asi");
        auto prefix = toLowerStr(toString(stem));

        // default use case: the file is inside an archive of the same name (or one that starts with it)
        for (const auto& release : releases)
        {
            if (release["draft"].asBool())
                continue;

            for (const auto& asset : release["assets"])
            {
                auto name = asset["name"].asString();
                if (!toLowerStr(name).starts_with(prefix))
                    continue;
                if (!machine.empty() && IsForOtherArchitecture(name, machine))
                    continue;

                auto hours = AssetHoursAgo(asset, now);
                bool newer = !best.found() || (hours != kRemoteDateUnknown && (best.hoursAgo == kRemoteDateUnknown || hours < best.hoursAgo));
                if (newer)
                {
                    best.hoursAgo = hours;
                    best.url = asset["browser_download_url"].asString();
                    best.name = name;
                    best.size = JsonSize(asset["size"]);
                }
            }
        }

        if (best.found())
            return best;

        // alternative use case: files and archives are named differently, use the latest release if it has a single asset
        for (const auto& release : releases)
        {
            if (release["draft"].asBool() || release["prerelease"].asBool())
                continue;

            const auto& assets = release["assets"];
            if (assets.size() == 1)
            {
                best.hoursAgo = AssetHoursAgo(assets[0], now);
                best.url = assets[0]["browser_download_url"].asString();
                best.name = assets[0]["name"].asString();
                best.size = JsonSize(assets[0]["size"]);
            }
            break;
        }
        return best;
    }

    RemoteFileInfo GetRemoteFileInfo(const std::wstring& fileName, const std::string& url, const std::wstring& machine, const std::string& token)
    {
        RemoteFileInfo info;
        if (url.empty())
        {
            info.error = L"no update URL";
            return info;
        }

        try
        {
            http::Options options;
            options.token = token;
            auto head = http::Head(url, options);

            std::string user, repo;
            if (!IsGitHubPage(url, head) || !ParseGitHubRepo(url, user, repo))
                return DirectLinkInfo(url, fileName, token);

            auto apiUrl = GitHubReleasesUrl(user, repo, 100);
            Log(L"Connecting to GitHub: {}", toWString(apiUrl));

            int pages = 1;
            auto link = http::Head(apiUrl, options);
            if (link.ok())
                pages = ParseLastPageFromLinkHeader(link.Header("Link"));

            for (int page = 1; page <= pages; page++)
            {
                auto r = http::Get(apiUrl + "&page=" + std::to_string(page), options);
                if (!r.ok())
                {
                    info.error = r.Describe();
                    if (r.status == 403 || r.status == 429)
                        info.error += L" (GitHub API rate limit?)";
                    Log(L"GitHub request failed: {}", info.error);
                    break;
                }

                Json::Value releases;
                if (!ParseJson(r.body, releases))
                {
                    info.error = L"GitHub returned an invalid response";
                    Log(L"{}", info.error);
                    break;
                }

                Log(L"GitHub's response parsed successfully. Page {}.", page);
                auto result = SelectGitHubAsset(releases, fileName, machine, std::chrono::system_clock::now());
                if (result.found())
                {
                    Log(L"Found {} on GitHub.", toWString(result.name));
                    return result;
                }
                Log(L"Nothing is found on GitHub.");
            }

            if (info.error.empty())
                info.error = Format(L"no release asset matching {} was found on GitHub", fileName);
        }
        catch (const std::exception& e)
        {
            info = {};
            info.error = toWString(e.what());
            Log(L"Error while checking {}: {}", toWString(url), info.error);
        }
        return info;
    }

    RemoteFileInfo GetInstallerDownloadInfo(const std::string& url, const std::string& token)
    {
        RemoteFileInfo info;
        try
        {
            http::Options options;
            options.token = token;
            auto head = http::Head(url, options);

            if (head.status == 403 || head.status == 405 || head.status == 501)
            {
                auto probe = http::Probe(url, options);
                if (probe.ok())
                    head = std::move(probe);
            }

            std::string user, repo;
            if (!IsGitHubPage(url, head) || !ParseGitHubRepo(url, user, repo))
            {
                if (head.ok())
                {
                    info.url = head.url.empty() ? url : head.url;
                    info.name = ParseContentDispositionFileName(head.Header("Content-Disposition"));
                    if (info.name.empty())
                        info.name = FileNameFromUrl(info.url);
                    info.size = ParseSize(head.Header("Content-Length"));
                    info.hoursAgo = 0;
                }
                else if (head.status == 403 || head.status == 405 || head.status == 501)
                {
                    // HEAD not supported by the host, the download will report real errors
                    info.url = url;
                    info.name = FileNameFromUrl(url);
                    info.hoursAgo = 0;
                }
                else
                {
                    info.error = head.Describe();
                }
                return info;
            }

            auto r = http::Get(GitHubReleasesUrl(user, repo, 30), options);
            if (!r.ok())
            {
                info.error = r.Describe();
                return info;
            }

            Json::Value releases;
            if (!ParseJson(r.body, releases) || !releases.isArray())
            {
                info.error = L"GitHub returned an invalid response";
                return info;
            }

            for (const auto& release : releases)
            {
                if (release["draft"].asBool() || release["prerelease"].asBool())
                    continue;

                const Json::Value* chosen = nullptr;
                const auto& assets = release["assets"];
                if (assets.size() == 1)
                    chosen = &assets[0];
                else
                {
                    for (const auto& asset : assets)
                    {
                        if (ends_with(asset["name"].asString(), ".zip", false))
                        {
                            chosen = &asset;
                            break;
                        }
                    }
                }

                if (chosen)
                {
                    info.url = (*chosen)["browser_download_url"].asString();
                    info.name = (*chosen)["name"].asString();
                    info.size = JsonSize((*chosen)["size"]);
                    info.hoursAgo = AssetHoursAgo(*chosen, std::chrono::system_clock::now());
                    return info;
                }
                info.error = Format(L"the latest release of {}/{} has no zip archive", toWString(user), toWString(repo));
                return info;
            }
            info.error = Format(L"{}/{} has no releases", toWString(user), toWString(repo));
        }
        catch (const std::exception& e)
        {
            info = {};
            info.error = toWString(e.what());
        }
        return info;
    }
}
