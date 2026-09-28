#define NOMINMAX
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include "test_server.h"
#include <algorithm>
#include <chrono>
#include <ctime>
#include <sstream>
#pragma comment(lib, "Ws2_32.lib")

namespace test
{
    namespace
    {
        const char* Reason(int status)
        {
            switch (status)
            {
            case 200: return "OK";
            case 206: return "Partial Content";
            case 302: return "Found";
            case 403: return "Forbidden";
            case 404: return "Not Found";
            case 405: return "Method Not Allowed";
            case 416: return "Range Not Satisfiable";
            case 500: return "Internal Server Error";
            case 503: return "Service Unavailable";
            }
            return "Status";
        }

        bool SendAll(SOCKET s, const char* data, size_t size)
        {
            while (size > 0)
            {
                int sent = send(s, data, static_cast<int>(std::min<size_t>(size, 64 * 1024)), 0);
                if (sent <= 0)
                    return false;
                data += sent;
                size -= sent;
            }
            return true;
        }

        std::string FormatTime(int hoursAgo, const char* format)
        {
            auto time = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now() - std::chrono::hours(hoursAgo));
            std::tm tm = {};
            gmtime_s(&tm, &time);
            char buffer[64];
            strftime(buffer, sizeof(buffer), format, &tm);
            return buffer;
        }
    }

    std::string HttpDate(int hoursAgo)
    {
        return FormatTime(hoursAgo, "%a, %d %b %Y %H:%M:%S GMT");
    }

    std::string IsoDate(int hoursAgo)
    {
        return FormatTime(hoursAgo, "%Y-%m-%dT%H:%M:%SZ");
    }

    Server::Server() : listener(static_cast<uintptr_t>(INVALID_SOCKET))
    {
        WSADATA data;
        WSAStartup(MAKEWORD(2, 2), &data);
    }

    Server::~Server()
    {
        Stop();
        WSACleanup();
    }

    bool Server::Start()
    {
        SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (s == INVALID_SOCKET)
            return false;

        sockaddr_in address = {};
        address.sin_family = AF_INET;
        address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        address.sin_port = 0;
        int length = sizeof(address);
        if (bind(s, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0 ||
            listen(s, SOMAXCONN) != 0 ||
            getsockname(s, reinterpret_cast<sockaddr*>(&address), &length) != 0)
        {
            closesocket(s);
            return false;
        }

        listener = static_cast<uintptr_t>(s);
        port = ntohs(address.sin_port);
        running = true;
        acceptThread = std::thread([this] { AcceptLoop(); });
        return true;
    }

    void Server::Stop()
    {
        if (!running.exchange(false))
            return;
        closesocket(static_cast<SOCKET>(listener));
        if (acceptThread.joinable())
            acceptThread.join();
        for (auto& worker : workers)
        {
            if (worker.joinable())
                worker.join();
        }
        workers.clear();
    }

    std::string Server::Url(const std::string& path) const
    {
        return "http://127.0.0.1:" + std::to_string(port) + path;
    }

    void Server::Set(const std::string& path, Resource resource)
    {
        std::lock_guard<std::mutex> lock(mutex);
        resources[path] = std::move(resource);
    }

    int Server::Requests(const std::string& path) const
    {
        std::lock_guard<std::mutex> lock(mutex);
        auto it = counts.find(path);
        return it != counts.end() ? it->second : 0;
    }

    std::vector<std::string> Server::RequestLog() const
    {
        std::lock_guard<std::mutex> lock(mutex);
        return log;
    }

    void Server::AcceptLoop()
    {
        while (running)
        {
            SOCKET client = accept(static_cast<SOCKET>(listener), nullptr, nullptr);
            if (client == INVALID_SOCKET)
                break;
            std::lock_guard<std::mutex> lock(mutex);
            workers.emplace_back([this, client] { Handle(static_cast<uintptr_t>(client)); });
        }
    }

    void Server::Handle(uintptr_t socketHandle)
    {
        SOCKET s = static_cast<SOCKET>(socketHandle);
        std::string request;
        char buffer[4096];
        while (request.find("\r\n\r\n") == std::string::npos)
        {
            int received = recv(s, buffer, sizeof(buffer), 0);
            if (received <= 0)
            {
                closesocket(s);
                return;
            }
            request.append(buffer, received);
        }

        std::istringstream lines(request);
        std::string method, target, version;
        lines >> method >> target >> version;
        std::string range;
        std::string line;
        std::getline(lines, line);
        while (std::getline(lines, line) && line != "\r")
        {
            auto colon = line.find(':');
            if (colon == std::string::npos)
                continue;
            auto key = line.substr(0, colon);
            std::transform(key.begin(), key.end(), key.begin(), ::tolower);
            auto value = line.substr(colon + 1);
            value.erase(0, value.find_first_not_of(' '));
            if (!value.empty() && value.back() == '\r')
                value.pop_back();
            if (key == "range")
                range = value;
        }

        auto path = target.substr(0, target.find('?'));
        Resource resource;
        bool found = false;
        {
            std::lock_guard<std::mutex> lock(mutex);
            counts[path]++;
            log.push_back(method + " " + target + (range.empty() ? "" : " Range: " + range));
            auto it = resources.find(target);
            if (it == resources.end())
                it = resources.find(path);
            if (it != resources.end())
            {
                resource = it->second;
                found = true;
                it->second.dropAfter = 0; // drop only once
            }
        }

        const bool head = method == "HEAD";
        int status = found ? resource.status : 404;
        if (head && found && resource.headStatus)
            status = resource.headStatus;

        std::string body = found ? resource.body : "not found";
        std::ostringstream header;

        if (found && !resource.redirect.empty())
        {
            header << "HTTP/1.1 302 Found\r\nLocation: " << resource.redirect << "\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
            auto text = header.str();
            SendAll(s, text.data(), text.size());
            shutdown(s, SD_SEND);
            closesocket(s);
            return;
        }

        size_t begin = 0, end = body.size();
        if (status == 200 && resource.ranges && !range.empty() && range.starts_with("bytes="))
        {
            auto spec = range.substr(6);
            auto dash = spec.find('-');
            begin = static_cast<size_t>(std::stoull(spec.substr(0, dash)));
            if (dash != std::string::npos && dash + 1 < spec.size())
                end = std::min<size_t>(body.size(), static_cast<size_t>(std::stoull(spec.substr(dash + 1))) + 1);
            if (begin >= body.size())
            {
                status = 416;
                begin = end = 0;
            }
            else
            {
                status = 206;
            }
        }

        header << "HTTP/1.1 " << status << " " << Reason(status) << "\r\n";
        header << "Content-Type: " << (found ? resource.contentType : "text/plain") << "\r\n";
        header << "Content-Length: " << (status == 416 ? 0 : end - begin) << "\r\n";
        if (status == 206)
            header << "Content-Range: bytes " << begin << "-" << (end - 1) << "/" << body.size() << "\r\n";
        if (status == 416)
            header << "Content-Range: bytes */" << body.size() << "\r\n";
        if (found && resource.ranges)
            header << "Accept-Ranges: bytes\r\n";
        if (found && !resource.lastModified.empty())
            header << "Last-Modified: " << resource.lastModified << "\r\n";
        if (found && !resource.contentDisposition.empty())
            header << "Content-Disposition: " << resource.contentDisposition << "\r\n";
        if (found && !resource.link.empty())
            header << "Link: " << resource.link << "\r\n";
        header << "Connection: close\r\n\r\n";

        auto text = header.str();
        bool ok = SendAll(s, text.data(), text.size());

        if (ok && !head && status != 416)
        {
            size_t limit = end;
            if (resource.dropAfter && begin + resource.dropAfter < end)
                limit = begin + resource.dropAfter;

            size_t chunk = resource.bytesPerSecond ? std::max<size_t>(1, resource.bytesPerSecond / 20) : 256 * 1024;
            for (size_t pos = begin; pos < limit && ok && running; pos += chunk)
            {
                size_t count = std::min(chunk, limit - pos);
                ok = SendAll(s, body.data() + pos, count);
                if (resource.bytesPerSecond)
                    Sleep(50);
            }

            if (limit < end)
            {
                // simulate a broken connection
                LINGER linger = { 1, 0 };
                setsockopt(s, SOL_SOCKET, SO_LINGER, reinterpret_cast<const char*>(&linger), sizeof(linger));
                closesocket(s);
                return;
            }
        }

        shutdown(s, SD_SEND);
        closesocket(s);
    }
}
