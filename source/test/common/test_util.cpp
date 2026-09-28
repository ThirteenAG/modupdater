#define NOMINMAX
#include <windows.h>
#include "test_util.h"
#include <zip.h>
#include <iowin32.h>
#include <initguid.h>
#include <wincodec.h>
#include <wrl/client.h>
#include <algorithm>
#include <atomic>
#include <fstream>
#include <random>

namespace test
{
    TempDir::TempDir(const std::wstring& name)
    {
        static std::atomic<int> counter = 0;
        path = std::filesystem::temp_directory_path() / L"modupdater-tests" /
            (name + L"-" + std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(GetTickCount64()) + L"-" + std::to_wstring(counter++));
        std::error_code ec;
        std::filesystem::remove_all(path, ec);
        std::filesystem::create_directories(path, ec);
    }

    TempDir::~TempDir()
    {
        if (keep)
            return;
        std::error_code ec;
        // files may still be read-only or in use for a moment
        for (int i = 0; i < 5; i++)
        {
            for (auto& entry : std::filesystem::recursive_directory_iterator(path, ec))
                SetFileAttributesW(entry.path().c_str(), FILE_ATTRIBUTE_NORMAL);
            if (std::filesystem::remove_all(path, ec) != static_cast<std::uintmax_t>(-1) && !std::filesystem::exists(path))
                return;
            Sleep(100);
        }
    }

    bool CreateZip(const std::filesystem::path& file, const std::vector<ZipItem>& items, const char* password)
    {
        zlib_filefunc64_def functions;
        fill_win32_filefunc64W(&functions);
        zipFile zf = zipOpen2_64(file.c_str(), APPEND_STATUS_CREATE, nullptr, &functions);
        if (!zf)
            return false;

        bool ok = true;
        for (auto& item : items)
        {
            zip_fileinfo info = {};
            info.tmz_date.tm_year = 2026;
            info.tmz_date.tm_mon = 4;
            info.tmz_date.tm_mday = 12;
            info.tmz_date.tm_hour = 7;
            bool utf8 = std::any_of(item.name.begin(), item.name.end(), [](char c) { return static_cast<unsigned char>(c) >= 0x80; });
            int err = zipOpenNewFileInZip4_64(zf, item.name.c_str(), &info, nullptr, 0, nullptr, 0, nullptr,
                Z_DEFLATED, Z_DEFAULT_COMPRESSION, 0, -MAX_WBITS, DEF_MEM_LEVEL, Z_DEFAULT_STRATEGY,
                password, 0, 0, static_cast<uint16_t>(utf8 ? (1 << 11) : 0), 0);
            if (err != ZIP_OK)
            {
                ok = false;
                break;
            }
            if (!item.data.empty())
                ok = zipWriteInFileInZip(zf, item.data.data(), static_cast<uint32_t>(item.data.size())) == ZIP_OK;
            ok = zipCloseFileInZip(zf) == ZIP_OK && ok;
            if (!ok)
                break;
        }
        ok = zipClose(zf, nullptr) == ZIP_OK && ok;
        return ok;
    }

    std::string CreateZipData(const std::vector<ZipItem>& items, const char* password)
    {
        TempDir dir(L"zip");
        auto file = dir / L"data.zip";
        if (!CreateZip(file, items, password))
            return {};
        return ReadFile(file);
    }

    bool WriteFile(const std::filesystem::path& file, const std::string& data)
    {
        std::error_code ec;
        if (file.has_parent_path())
            std::filesystem::create_directories(file.parent_path(), ec);
        std::ofstream stream(file, std::ios::binary | std::ios::trunc);
        stream.write(data.data(), static_cast<std::streamsize>(data.size()));
        return stream.good();
    }

    std::string ReadFile(const std::filesystem::path& file)
    {
        std::ifstream stream(file, std::ios::binary);
        if (!stream)
            return {};
        return std::string((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
    }

    bool Exists(const std::filesystem::path& file)
    {
        return GetFileAttributesW(file.c_str()) != INVALID_FILE_ATTRIBUTES;
    }

    void SetFileTimeHoursAgo(const std::filesystem::path& file, int hours)
    {
        HANDLE h = CreateFileW(file.c_str(), FILE_WRITE_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);
        if (h == INVALID_HANDLE_VALUE)
            return;
        FILETIME now;
        GetSystemTimeAsFileTime(&now);
        ULARGE_INTEGER t;
        t.LowPart = now.dwLowDateTime;
        t.HighPart = now.dwHighDateTime;
        t.QuadPart -= static_cast<ULONGLONG>(hours) * 3600ULL * 10000000ULL;
        FILETIME ft = { t.LowPart, t.HighPart };
        SetFileTime(h, &ft, &ft, &ft);
        CloseHandle(h);
    }

    std::string RandomData(size_t size, uint32_t seed)
    {
        std::mt19937 rng(seed);
        std::string data(size, '\0');
        for (auto& c : data)
            c = static_cast<char>(rng() & 0xFF);
        return data;
    }

    int RunProcess(const std::wstring& commandLine, DWORD timeoutMs)
    {
        STARTUPINFOW si = { sizeof(si) };
        PROCESS_INFORMATION pi = {};
        std::wstring cmd = commandLine;
        if (!CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si, &pi))
            return -1;
        DWORD code = static_cast<DWORD>(-1);
        if (WaitForSingleObject(pi.hProcess, timeoutMs) == WAIT_OBJECT_0)
            GetExitCodeProcess(pi.hProcess, &code);
        else
            TerminateProcess(pi.hProcess, static_cast<UINT>(-1));
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
        return static_cast<int>(code);
    }

    std::filesystem::path BinDir()
    {
        wchar_t buffer[MAX_PATH * 4];
        GetModuleFileNameW(nullptr, buffer, static_cast<DWORD>(std::size(buffer)));
        return std::filesystem::path(buffer).parent_path();
    }

    std::wstring Quote(const std::wstring& arg)
    {
        std::wstring quoted = L"\"";
        size_t backslashes = 0;
        for (wchar_t c : arg)
        {
            if (c == L'\\')
                backslashes++;
            else if (c == L'"')
            {
                quoted.append(backslashes * 2 + 1, L'\\');
                backslashes = 0;
            }
            else
                backslashes = 0;
            quoted.push_back(c);
        }
        quoted.append(backslashes, L'\\');
        return quoted + L"\"";
    }

    bool AddFakeSignature(const std::filesystem::path& file, size_t certificateSize)
    {
        auto data = ReadFile(file);
        if (data.size() < sizeof(IMAGE_DOS_HEADER))
            return false;

        auto dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(data.data());
        size_t nt = static_cast<size_t>(dos->e_lfanew);
        size_t optional = nt + sizeof(DWORD) + sizeof(IMAGE_FILE_HEADER);
        WORD magic = *reinterpret_cast<const WORD*>(data.data() + optional);
        size_t directory = optional + (magic == IMAGE_NT_OPTIONAL_HDR64_MAGIC ? offsetof(IMAGE_OPTIONAL_HEADER64, DataDirectory) : offsetof(IMAGE_OPTIONAL_HEADER32, DataDirectory))
            + IMAGE_DIRECTORY_ENTRY_SECURITY * sizeof(IMAGE_DATA_DIRECTORY);

        // signtool pads the file to 8 bytes before the certificate table
        while (data.size() % 8)
            data.push_back('\0');

        IMAGE_DATA_DIRECTORY security = { static_cast<DWORD>(data.size()), static_cast<DWORD>(certificateSize) };
        memcpy(data.data() + directory, &security, sizeof(security));
        data += RandomData(certificateSize, 42);
        return WriteFile(file, data);
    }

    double AverageLuminance(const std::filesystem::path& image)
    {
        using Microsoft::WRL::ComPtr;
        constexpr GUID clsidWicImagingFactory = { 0xcacaf262, 0x9370, 0x4615, { 0xa1, 0x3b, 0x9f, 0x55, 0x39, 0xda, 0x4c, 0x0a } };
        HRESULT co = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

        double result = -1.0;
        {
            ComPtr<IWICImagingFactory> wic;
            ComPtr<IWICBitmapDecoder> decoder;
            ComPtr<IWICBitmapFrameDecode> frame;
            ComPtr<IWICFormatConverter> converter;
            UINT width = 0, height = 0;
            if (SUCCEEDED(CoCreateInstance(clsidWicImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&wic))) &&
                SUCCEEDED(wic->CreateDecoderFromFilename(image.c_str(), nullptr, GENERIC_READ, WICDecodeMetadataCacheOnDemand, &decoder)) &&
                SUCCEEDED(decoder->GetFrame(0, &frame)) &&
                SUCCEEDED(wic->CreateFormatConverter(&converter)) &&
                SUCCEEDED(converter->Initialize(frame.Get(), GUID_WICPixelFormat32bppBGRA, WICBitmapDitherTypeNone, nullptr, 0, WICBitmapPaletteTypeCustom)) &&
                SUCCEEDED(converter->GetSize(&width, &height)) && width && height)
            {
                std::vector<uint8_t> pixels(static_cast<size_t>(width) * height * 4);
                if (SUCCEEDED(converter->CopyPixels(nullptr, width * 4, static_cast<UINT>(pixels.size()), pixels.data())))
                {
                    double sum = 0;
                    for (size_t i = 0; i < pixels.size(); i += 4)
                        sum += 0.0722 * pixels[i] + 0.7152 * pixels[i + 1] + 0.2126 * pixels[i + 2];
                    result = sum / (255.0 * width * height);
                }
            }
        }

        if (SUCCEEDED(co))
            CoUninitialize();
        return result;
    }
}
