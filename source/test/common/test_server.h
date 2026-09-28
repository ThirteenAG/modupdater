#pragma once
#include <atomic>
#include <cstdint>
#include <functional>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

// Small HTTP/1.1 server on 127.0.0.1 for tests: serves in-memory files with the headers the
// updater looks at, supports HEAD, range requests, redirects, throttling and dropped connections.
namespace test
{
    struct Resource
    {
        std::string body;
        std::string contentType = "application/octet-stream";
        std::string lastModified;           // RFC 1123 date, e.g. HttpDate(hoursAgo)
        std::string contentDisposition;     // e.g. "attachment; filename=Mod.zip"
        std::string redirect;               // path or URL for a 302 response
        std::string link;                   // Link header (GitHub pagination)
        int status = 200;
        int headStatus = 0;                 // status for HEAD requests, 0 = same as GET
        bool ranges = true;                 // honour Range requests
        size_t dropAfter = 0;               // close the connection after this many body bytes (once)
        size_t bytesPerSecond = 0;          // throttling, 0 = unlimited
    };

    class Server
    {
    public:
        Server();
        ~Server();

        bool Start();
        void Stop();
        uint16_t Port() const { return port; }
        std::string Url(const std::string& path = "/") const;

        void Set(const std::string& path, Resource resource);
        int Requests(const std::string& path) const;
        std::vector<std::string> RequestLog() const; // "GET /path Range: bytes=10-"

    private:
        void AcceptLoop();
        void Handle(uintptr_t socket);

        uintptr_t listener;
        uint16_t port = 0;
        std::atomic<bool> running = false;
        std::thread acceptThread;
        std::vector<std::thread> workers;
        mutable std::mutex mutex;
        std::map<std::string, Resource> resources;
        std::map<std::string, int> counts;
        std::vector<std::string> log;
    };

    // "Tue, 12 May 2026 07:29:48 GMT" for a time 'hoursAgo' hours in the past
    std::string HttpDate(int hoursAgo);
    // "2026-05-12T07:29:48Z"
    std::string IsoDate(int hoursAgo);
}
