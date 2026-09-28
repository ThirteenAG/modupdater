#pragma once
#include <windows.h>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace test
{
    // Temporary folder, removed when the object is destroyed
    class TempDir
    {
    public:
        explicit TempDir(const std::wstring& name = L"test");
        ~TempDir();
        TempDir(const TempDir&) = delete;
        TempDir& operator=(const TempDir&) = delete;
        const std::filesystem::path& Path() const { return path; }
        std::filesystem::path operator/(const std::wstring& name) const { return path / name; }
        void Keep() { keep = true; }
    private:
        std::filesystem::path path;
        bool keep = false;
    };

    struct ZipItem
    {
        std::string name;       // UTF-8, '/' separated, ending with '/' for folders
        std::string data;
    };

    // Creates a zip archive with minizip; password enables traditional PKWARE encryption
    bool CreateZip(const std::filesystem::path& file, const std::vector<ZipItem>& items, const char* password = nullptr);
    std::string CreateZipData(const std::vector<ZipItem>& items, const char* password = nullptr);

    bool WriteFile(const std::filesystem::path& file, const std::string& data);
    std::string ReadFile(const std::filesystem::path& file);
    bool Exists(const std::filesystem::path& file);
    void SetFileTimeHoursAgo(const std::filesystem::path& file, int hours);

    // Deterministic pseudo random data
    std::string RandomData(size_t size, uint32_t seed = 1);

    // Runs a program and returns its exit code, or -1 if it could not be started / timed out
    int RunProcess(const std::wstring& commandLine, DWORD timeoutMs = 120000);

    // Folder of the running test executable (the other test binaries are next to it)
    std::filesystem::path BinDir();

    std::wstring Quote(const std::wstring& arg);

    // Adds a fake Authenticode certificate table to a PE file the way signtool does
    // (the table is 8-byte aligned and the security directory points to it)
    bool AddFakeSignature(const std::filesystem::path& file, size_t certificateSize = 4096);

    // Average brightness (0 black ... 1 white) of an image file, -1 if it cannot be read
    double AverageLuminance(const std::filesystem::path& image);
}
