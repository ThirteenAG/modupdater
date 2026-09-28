#include "stdafx.h"
#include "archive.h"
#include "fileops.h"
#include "log.h"
#include "string_funcs.h"
#include <unzip.h>

namespace mu::zip
{
    namespace
    {
        // minizip I/O over a region of a Win32 file. minizip opens the "file" twice (central
        // directory + data), every stream keeps its own position and uses positional reads.
        struct IoFile
        {
            HANDLE handle = INVALID_HANDLE_VALUE;
            uint64_t base = 0;
            uint64_t size = 0;
        };

        struct IoStream
        {
            IoFile* file;
            uint64_t pos;
            bool error;
        };

        voidpf ZCALLBACK IoOpen(voidpf opaque, const void*, int mode)
        {
            if ((mode & ZLIB_FILEFUNC_MODE_READWRITEFILTER) != ZLIB_FILEFUNC_MODE_READ)
                return nullptr;
            return new (std::nothrow) IoStream{ static_cast<IoFile*>(opaque), 0, false };
        }

        voidpf ZCALLBACK IoOpenDisk(voidpf, voidpf, uint32_t, int)
        {
            return nullptr; // split archives are not supported
        }

        uint32_t ZCALLBACK IoRead(voidpf, voidpf stream, void* buf, uint32_t size)
        {
            auto s = static_cast<IoStream*>(stream);
            if (s->pos >= s->file->size || size == 0)
                return 0;

            auto toRead = static_cast<DWORD>(std::min<uint64_t>(size, s->file->size - s->pos));
            uint64_t offset = s->file->base + s->pos;
            OVERLAPPED ov = {};
            ov.Offset = static_cast<DWORD>(offset);
            ov.OffsetHigh = static_cast<DWORD>(offset >> 32);
            DWORD read = 0;
            if (!ReadFile(s->file->handle, buf, toRead, &read, &ov))
            {
                s->error = true;
                return 0;
            }
            s->pos += read;
            return read;
        }

        uint32_t ZCALLBACK IoWrite(voidpf, voidpf, const void*, uint32_t)
        {
            return 0;
        }

        uint64_t ZCALLBACK IoTell(voidpf, voidpf stream)
        {
            return static_cast<IoStream*>(stream)->pos;
        }

        long ZCALLBACK IoSeek(voidpf, voidpf stream, uint64_t offset, int origin)
        {
            auto s = static_cast<IoStream*>(stream);
            uint64_t newPos = 0;
            switch (origin)
            {
            case ZLIB_FILEFUNC_SEEK_SET: newPos = offset; break;
            case ZLIB_FILEFUNC_SEEK_CUR: newPos = s->pos + offset; break;
            case ZLIB_FILEFUNC_SEEK_END: newPos = s->file->size + offset; break;
            default: return -1;
            }
            if (newPos > s->file->size)
                return -1;
            s->pos = newPos;
            return 0;
        }

        int ZCALLBACK IoClose(voidpf, voidpf stream)
        {
            delete static_cast<IoStream*>(stream);
            return 0;
        }

        int ZCALLBACK IoError(voidpf, voidpf stream)
        {
            return static_cast<IoStream*>(stream)->error ? 1 : 0;
        }

        bool IsValidUtf8(std::string_view s)
        {
            return MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s.data(), static_cast<int>(s.size()), nullptr, 0) > 0;
        }
    }

    std::wstring DecodeEntryName(std::string_view raw, bool utf8Flag)
    {
        if (raw.empty())
            return {};
        bool ascii = std::all_of(raw.begin(), raw.end(), [](char c) { return static_cast<unsigned char>(c) < 0x80; });
        if (ascii || utf8Flag || IsValidUtf8(raw))
            return toWString(raw);
        return toWStringCP(raw, CP_OEMCP);
    }

    struct Reader::Impl
    {
        IoFile file;
        unzFile uf = nullptr;
        std::vector<Entry> entries;
        std::wstring error;
        std::vector<uint8_t> buffer;
    };

    Reader::Reader() : impl(std::make_unique<Impl>())
    {
    }

    Reader::~Reader()
    {
        Close();
    }

    void Reader::Close()
    {
        if (impl->uf)
        {
            unzClose(impl->uf);
            impl->uf = nullptr;
        }
        if (impl->file.handle != INVALID_HANDLE_VALUE)
        {
            CloseHandle(impl->file.handle);
            impl->file.handle = INVALID_HANDLE_VALUE;
        }
        impl->entries.clear();
    }

    bool Reader::IsOpen() const
    {
        return impl->uf != nullptr;
    }

    bool Reader::Open(const std::filesystem::path& file, uint64_t offset, uint64_t length)
    {
        Close();
        impl->error.clear();

        impl->file.handle = CreateFileW(file.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (impl->file.handle == INVALID_HANDLE_VALUE)
        {
            impl->error = Format(L"Cannot open {}: {}", file.wstring(), Win32ErrorMessage(GetLastError()));
            return false;
        }

        LARGE_INTEGER fileSize = {};
        GetFileSizeEx(impl->file.handle, &fileSize);
        uint64_t total = static_cast<uint64_t>(fileSize.QuadPart);
        if (offset > total)
        {
            impl->error = L"Archive offset is outside of the file";
            Close();
            return false;
        }

        impl->file.base = offset;
        impl->file.size = length ? std::min(length, total - offset) : total - offset;

        zlib_filefunc64_def functions = {};
        functions.zopen64_file = IoOpen;
        functions.zopendisk64_file = IoOpenDisk;
        functions.zread_file = IoRead;
        functions.zwrite_file = IoWrite;
        functions.ztell64_file = IoTell;
        functions.zseek64_file = IoSeek;
        functions.zclose_file = IoClose;
        functions.zerror_file = IoError;
        functions.opaque = &impl->file;

        impl->uf = unzOpen2_64(L"", &functions);
        if (!impl->uf)
        {
            impl->error = Format(L"{} is not a valid zip archive or is damaged", file.filename().wstring());
            Close();
            return false;
        }

        int err = unzGoToFirstFile(impl->uf);
        while (err == UNZ_OK)
        {
            unz_file_info64 info = {};
            err = unzGetCurrentFileInfo64(impl->uf, &info, nullptr, 0, nullptr, 0, nullptr, 0);
            if (err != UNZ_OK)
                break;

            std::string rawName(info.size_filename + 1, '\0');
            err = unzGetCurrentFileInfo64(impl->uf, &info, rawName.data(), static_cast<uint16_t>(rawName.size()), nullptr, 0, nullptr, 0);
            if (err != UNZ_OK)
                break;
            rawName.resize(info.size_filename);

            Entry entry;
            entry.name = DecodeEntryName(rawName, (info.flag & (1 << 11)) != 0);
            entry.compressedSize = info.compressed_size;
            entry.uncompressedSize = info.uncompressed_size;
            entry.crc = info.crc;
            entry.encrypted = (info.flag & 1) != 0;
            entry.isDirectory = !rawName.empty() && (rawName.back() == '/' || rawName.back() == '\\');

            unz64_file_pos pos = {};
            unzGetFilePos64(impl->uf, &pos);
            entry.dirPosition = pos.pos_in_zip_directory;
            entry.fileIndex = pos.num_of_file;

            impl->entries.push_back(std::move(entry));
            err = unzGoToNextFile(impl->uf);
        }

        if (err != UNZ_END_OF_LIST_OF_FILE)
        {
            impl->error = Format(L"The central directory of {} is damaged (error {})", file.filename().wstring(), err);
            Close();
            return false;
        }

        return true;
    }

    const std::vector<Entry>& Reader::Entries() const
    {
        return impl->entries;
    }

    const Entry* Reader::Find(std::wstring_view name) const
    {
        auto normalize = [](std::wstring_view s)
        {
            std::wstring r(s);
            std::replace(r.begin(), r.end(), L'\\', L'/');
            return toLowerWStr(r);
        };
        auto wanted = normalize(name);
        for (auto& e : impl->entries)
        {
            if (normalize(e.name) == wanted)
                return &e;
        }
        return nullptr;
    }

    uint64_t Reader::TotalUncompressedSize() const
    {
        uint64_t total = 0;
        for (auto& e : impl->entries)
        {
            if (!e.isDirectory)
                total += e.uncompressedSize;
        }
        return total;
    }

    bool Reader::HasEncryptedEntries() const
    {
        return std::any_of(impl->entries.begin(), impl->entries.end(), [](const Entry& e) { return e.encrypted; });
    }

    const std::wstring& Reader::Error() const
    {
        return impl->error;
    }

    bool Reader::Extract(const Entry& entry, const std::string& password, const Sink& sink)
    {
        impl->error.clear();
        if (!impl->uf)
        {
            impl->error = L"Archive is not open";
            return false;
        }

        if (entry.encrypted && password.empty())
        {
            impl->error = Format(L"{} is password protected", entry.name);
            return false;
        }

        unz64_file_pos pos = { entry.dirPosition, entry.fileIndex };
        int err = unzGoToFilePos64(impl->uf, &pos);
        if (err == UNZ_OK)
            err = unzOpenCurrentFilePassword(impl->uf, entry.encrypted ? password.c_str() : nullptr);
        if (err != UNZ_OK)
        {
            impl->error = (err == UNZ_BADPASSWORD) ? Format(L"Wrong password for {}", entry.name) : Format(L"Cannot read {} from the archive (error {})", entry.name, err);
            return false;
        }

        // minizip reads at most 64 KB per call
        if (impl->buffer.empty())
            impl->buffer.resize(60 * 1024);

        bool aborted = false;
        int readResult = 0;
        for (;;)
        {
            readResult = unzReadCurrentFile(impl->uf, impl->buffer.data(), static_cast<uint32_t>(impl->buffer.size()));
            if (readResult <= 0)
                break;
            if (!sink(impl->buffer.data(), static_cast<size_t>(readResult)))
            {
                aborted = true;
                break;
            }
        }

        int closeResult = unzCloseCurrentFile(impl->uf);
        if (aborted)
            return false;

        if (readResult < 0 || closeResult == UNZ_CRCERROR)
        {
            if (entry.encrypted)
                impl->error = Format(L"Cannot decrypt {}: the password is wrong or the archive is damaged", entry.name);
            else if (closeResult == UNZ_CRCERROR)
                impl->error = Format(L"{} is damaged (CRC mismatch)", entry.name);
            else
                impl->error = Format(L"Cannot decompress {} (error {}), the archive is damaged", entry.name, readResult);
            return false;
        }

        return true;
    }

    bool Reader::ExtractToFile(const Entry& entry, const std::string& password, const std::filesystem::path& file, const std::function<bool(uint64_t)>& progress)
    {
        HANDLE out = CreateFileW(file.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
        if (out == INVALID_HANDLE_VALUE)
        {
            impl->error = Format(L"Cannot create {}: {}", file.wstring(), Win32ErrorMessage(GetLastError()));
            return false;
        }

        uint64_t written = 0;
        std::wstring writeError;
        bool ok = Extract(entry, password, [&](const uint8_t* data, size_t size)
        {
            DWORD done = 0;
            if (!WriteFile(out, data, static_cast<DWORD>(size), &done, nullptr) || done != size)
            {
                writeError = Format(L"Cannot write {}: {}", file.wstring(), Win32ErrorMessage(GetLastError()));
                return false;
            }
            written += done;
            return !progress || progress(written);
        });

        CloseHandle(out);
        if (!ok)
        {
            if (!writeError.empty())
                impl->error = writeError;
            DeleteFileW(file.c_str());
        }
        return ok;
    }

    bool Reader::ExtractToMemory(const Entry& entry, const std::string& password, std::vector<uint8_t>& data, uint64_t maxSize)
    {
        data.clear();
        if (entry.uncompressedSize > maxSize)
        {
            impl->error = Format(L"{} is too large", entry.name);
            return false;
        }
        data.reserve(static_cast<size_t>(entry.uncompressedSize));
        return Extract(entry, password, [&](const uint8_t* p, size_t size)
        {
            if (data.size() + size > maxSize)
                return false;
            data.insert(data.end(), p, p + size);
            return true;
        });
    }
}

namespace mu::embedded
{
    namespace
    {
        bool ReadAt(HANDLE file, uint64_t offset, void* buffer, DWORD size)
        {
            OVERLAPPED ov = {};
            ov.Offset = static_cast<DWORD>(offset);
            ov.OffsetHigh = static_cast<DWORD>(offset >> 32);
            DWORD read = 0;
            return ReadFile(file, buffer, size, &read, &ov) && read == size;
        }

        template<typename T>
        bool ReadValue(HANDLE file, uint64_t offset, T& value)
        {
            return ReadAt(file, offset, &value, sizeof(T));
        }

        // Skips the zero padding that signtool inserts between the overlay and the certificate table
        uint64_t FindFooterEnd(HANDLE file, uint64_t dataEnd)
        {
            for (uint64_t pad = 0; pad < 8 && pad + 16 <= dataEnd; pad++)
            {
                uint32_t magic = 0;
                if (!ReadValue(file, dataEnd - pad - 4, magic))
                    return 0;
                if (magic == kMagic)
                    return dataEnd - pad;

                uint8_t b = 0;
                if (!ReadValue(file, dataEnd - pad - 1, b) || b != 0)
                    return 0;
            }
            return 0;
        }
    }

    uint64_t GetDataEnd(HANDLE file, uint64_t fileSize, uint32_t* securityDirOffset)
    {
        IMAGE_DOS_HEADER dos = {};
        if (!ReadValue(file, 0, dos) || dos.e_magic != IMAGE_DOS_SIGNATURE || dos.e_lfanew <= 0)
            return fileSize;

        uint64_t ntOffset = static_cast<uint64_t>(dos.e_lfanew);
        DWORD signature = 0;
        WORD optionalMagic = 0;
        if (!ReadValue(file, ntOffset, signature) || signature != IMAGE_NT_SIGNATURE)
            return fileSize;

        uint64_t optionalOffset = ntOffset + sizeof(DWORD) + sizeof(IMAGE_FILE_HEADER);
        if (!ReadValue(file, optionalOffset, optionalMagic))
            return fileSize;

        uint64_t dirOffset = 0;
        if (optionalMagic == IMAGE_NT_OPTIONAL_HDR32_MAGIC)
            dirOffset = optionalOffset + offsetof(IMAGE_OPTIONAL_HEADER32, DataDirectory) + IMAGE_DIRECTORY_ENTRY_SECURITY * sizeof(IMAGE_DATA_DIRECTORY);
        else if (optionalMagic == IMAGE_NT_OPTIONAL_HDR64_MAGIC)
            dirOffset = optionalOffset + offsetof(IMAGE_OPTIONAL_HEADER64, DataDirectory) + IMAGE_DIRECTORY_ENTRY_SECURITY * sizeof(IMAGE_DATA_DIRECTORY);
        else
            return fileSize;

        IMAGE_DATA_DIRECTORY security = {};
        if (!ReadValue(file, dirOffset, security))
            return fileSize;

        if (securityDirOffset)
            *securityDirOffset = static_cast<uint32_t>(dirOffset);

        // for the security directory, VirtualAddress is a file offset
        if (security.VirtualAddress != 0 && security.Size != 0 && static_cast<uint64_t>(security.VirtualAddress) + security.Size == fileSize)
            return security.VirtualAddress;

        return fileSize;
    }

    std::vector<Archive> Find(const std::filesystem::path& path)
    {
        std::vector<Archive> archives;

        HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file == INVALID_HANDLE_VALUE)
            return archives;

        LARGE_INTEGER size = {};
        GetFileSizeEx(file, &size);

        uint64_t pos = FindFooterEnd(file, GetDataEnd(file, static_cast<uint64_t>(size.QuadPart)));
        while (pos >= 16 && archives.size() < 64)
        {
            uint32_t magic = 0, nameLength = 0;
            uint64_t zipSize = 0;
            if (!ReadValue(file, pos - 4, magic) || magic != kMagic)
                break;
            if (!ReadValue(file, pos - 12, zipSize) || !ReadValue(file, pos - 16, nameLength))
                break;
            if (nameLength == 0 || nameLength > 1024 || pos - 16 < nameLength)
                break;

            uint64_t nameOffset = pos - 16 - nameLength;
            if (nameOffset < zipSize)
                break;

            std::string name(nameLength, '\0');
            if (!ReadAt(file, nameOffset, name.data(), nameLength))
                break;

            uint64_t zipOffset = nameOffset - zipSize;
            uint32_t header = 0;
            if (!ReadValue(file, zipOffset, header) || (header != 0x04034B50 && header != 0x06054B50))
            {
                Log(L"Embedded archive footer found, but no zip data at offset {}", zipOffset);
                break;
            }

            archives.push_back({ zip::DecodeEntryName(name, false), zipOffset, zipSize });
            pos = zipOffset;
        }

        CloseHandle(file);
        std::reverse(archives.begin(), archives.end());
        return archives;
    }

    bool Append(const std::filesystem::path& exe, const std::filesystem::path& zipPath, const std::filesystem::path& output, std::wstring* error)
    {
        auto fail = [&](std::wstring message)
        {
            if (error)
                *error = std::move(message);
            return false;
        };

        {
            zip::Reader test;
            if (!test.Open(zipPath))
                return fail(test.Error());
        }

        HANDLE src = CreateFileW(exe.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (src == INVALID_HANDLE_VALUE)
            return fail(Format(L"Cannot open {}: {}", exe.wstring(), Win32ErrorMessage(GetLastError())));

        LARGE_INTEGER srcSize = {};
        GetFileSizeEx(src, &srcSize);
        uint32_t securityDirOffset = 0;
        uint64_t dataEnd = GetDataEnd(src, static_cast<uint64_t>(srcSize.QuadPart), &securityDirOffset);
        bool stripSignature = dataEnd != static_cast<uint64_t>(srcSize.QuadPart);
        if (stripSignature)
        {
            // keep previously appended archives chained: drop the padding between their footer and the signature
            if (uint64_t footerEnd = FindFooterEnd(src, dataEnd))
                dataEnd = footerEnd;
        }

        if (dataEnd > 512ull * 1024 * 1024 * 1024)
        {
            CloseHandle(src);
            return fail(L"Executable is too large");
        }

        HANDLE dst = CreateFileW(output.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (dst == INVALID_HANDLE_VALUE)
        {
            CloseHandle(src);
            return fail(Format(L"Cannot create {}: {}", output.wstring(), Win32ErrorMessage(GetLastError())));
        }

        std::vector<uint8_t> buffer(1024 * 1024);
        auto copyRange = [&](HANDLE from, uint64_t offset, uint64_t length) -> bool
        {
            while (length > 0)
            {
                auto chunk = static_cast<DWORD>(std::min<uint64_t>(length, buffer.size()));
                DWORD written = 0;
                if (!ReadAt(from, offset, buffer.data(), chunk) || !WriteFile(dst, buffer.data(), chunk, &written, nullptr) || written != chunk)
                    return false;
                offset += chunk;
                length -= chunk;
            }
            return true;
        };

        bool ok = copyRange(src, 0, dataEnd);
        CloseHandle(src);

        if (ok && stripSignature && securityDirOffset)
        {
            // the certificate table is gone, clear the security directory entry
            IMAGE_DATA_DIRECTORY empty = {};
            LARGE_INTEGER pos = {}, zero = {};
            pos.QuadPart = securityDirOffset;
            DWORD written = 0;
            ok = SetFilePointerEx(dst, pos, nullptr, FILE_BEGIN) && WriteFile(dst, &empty, sizeof(empty), &written, nullptr) &&
                 SetFilePointerEx(dst, zero, nullptr, FILE_END);
        }

        if (ok)
        {
            HANDLE zip = CreateFileW(zipPath.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
            if (zip == INVALID_HANDLE_VALUE)
            {
                ok = false;
            }
            else
            {
                LARGE_INTEGER zipSize = {};
                GetFileSizeEx(zip, &zipSize);
                ok = copyRange(zip, 0, static_cast<uint64_t>(zipSize.QuadPart));
                CloseHandle(zip);

                auto name = toString(zipPath.filename().wstring());
                uint32_t nameLength = static_cast<uint32_t>(name.size());
                uint64_t size64 = static_cast<uint64_t>(zipSize.QuadPart);
                uint32_t magic = kMagic;
                DWORD written = 0;
                ok = ok && nameLength > 0 && nameLength <= 1024 &&
                     WriteFile(dst, name.data(), nameLength, &written, nullptr) &&
                     WriteFile(dst, &nameLength, sizeof(nameLength), &written, nullptr) &&
                     WriteFile(dst, &size64, sizeof(size64), &written, nullptr) &&
                     WriteFile(dst, &magic, sizeof(magic), &written, nullptr);
            }
        }

        DWORD lastError = GetLastError();
        CloseHandle(dst);

        if (!ok)
        {
            DeleteFileW(output.c_str());
            return fail(Format(L"Cannot write {}: {}", output.wstring(), Win32ErrorMessage(lastError)));
        }

        if (stripSignature)
            Log(L"Removed the Authenticode signature from the copy of {}, sign {} again", exe.filename().wstring(), output.filename().wstring());

        return true;
    }
}
