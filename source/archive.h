#pragma once
#include <windows.h>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace mu::zip
{
    struct Entry
    {
        std::wstring name;          // path inside the archive, '/' separated
        uint64_t compressedSize = 0;
        uint64_t uncompressedSize = 0;
        uint32_t crc = 0;
        bool isDirectory = false;
        bool encrypted = false;
        uint64_t dirPosition = 0;   // internal (unz64_file_pos)
        uint64_t fileIndex = 0;     // internal (unz64_file_pos)
    };

    // Receives decompressed data; return false to abort the extraction
    using Sink = std::function<bool(const uint8_t* data, size_t size)>;

    // Streaming zip reader (minizip). Archives are read directly from disk with Unicode paths,
    // nothing is loaded into memory as a whole.
    class Reader
    {
    public:
        Reader();
        ~Reader();
        Reader(const Reader&) = delete;
        Reader& operator=(const Reader&) = delete;

        // Opens a zip archive stored in 'file' in the range [offset, offset + length).
        // length 0 means "until the end of the file".
        bool Open(const std::filesystem::path& file, uint64_t offset = 0, uint64_t length = 0);
        void Close();
        bool IsOpen() const;

        const std::vector<Entry>& Entries() const;
        const Entry* Find(std::wstring_view name) const; // case-insensitive, '/' or '\\'
        uint64_t TotalUncompressedSize() const;
        bool HasEncryptedEntries() const;

        // Decompresses an entry into 'sink'. Returns false on failure (see Error()), CRC mismatch,
        // wrong password, or when 'sink' returned false (Error() is empty in that case).
        bool Extract(const Entry& entry, const std::string& password, const Sink& sink);
        // 'progress' receives the number of bytes written so far; return false to cancel
        bool ExtractToFile(const Entry& entry, const std::string& password, const std::filesystem::path& file, const std::function<bool(uint64_t)>& progress = {});
        bool ExtractToMemory(const Entry& entry, const std::string& password, std::vector<uint8_t>& data, uint64_t maxSize = 64 * 1024 * 1024);

        const std::wstring& Error() const;

    private:
        struct Impl;
        std::unique_ptr<Impl> impl;
    };

    // Decodes an entry name: UTF-8 when flagged (or valid UTF-8), OEM code page otherwise
    std::wstring DecodeEntryName(std::string_view raw, bool utf8Flag);
}

namespace mu::embedded
{
    // Archives can be appended to the installer executable:
    // [exe][zip data][zip name][uint32 name length][uint64 zip size][uint32 magic 'ZIPE']
    // Several archives can be chained this way. When the executable is code signed afterwards,
    // the certificate table (plus up to 7 bytes of alignment padding) follows the last footer.
    constexpr uint32_t kMagic = 0x5A495045;

    struct Archive
    {
        std::wstring name;
        uint64_t offset = 0;
        uint64_t size = 0;
    };

    // Returns the appended archives in the order they were appended
    std::vector<Archive> Find(const std::filesystem::path& file);

    // Copies 'exe' to 'output' without its Authenticode signature (so it can be signed again
    // after appending) and appends 'zip'.
    bool Append(const std::filesystem::path& exe, const std::filesystem::path& zip, const std::filesystem::path& output, std::wstring* error = nullptr);

    // Offset where the certificate table of a signed PE starts if it is located at the end of
    // the file, otherwise the file size.
    uint64_t GetDataEnd(HANDLE file, uint64_t fileSize, uint32_t* securityDirOffset = nullptr);
}
