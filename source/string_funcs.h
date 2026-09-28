#pragma once
#include <windows.h>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cwctype>
#include <iomanip>
#include <sstream>
#include <string>
#include <string_view>

inline bool starts_with(const std::string_view str, const std::string_view prefix, bool case_sensitive)
{
    if (!case_sensitive)
    {
        if (str.size() < prefix.size())
            return false;
        return std::equal(prefix.begin(), prefix.end(), str.begin(), [](char a, char b)
        {
            return ::tolower(static_cast<unsigned char>(a)) == ::tolower(static_cast<unsigned char>(b));
        });
    }
    return str.starts_with(prefix);
}

inline bool starts_with(const std::wstring_view str, const std::wstring_view prefix, bool case_sensitive)
{
    if (!case_sensitive)
    {
        if (str.size() < prefix.size())
            return false;
        if (prefix.empty())
            return true;
        // same rules as the file system, not limited to ASCII like towlower in the "C" locale
        return CompareStringOrdinal(str.data(), static_cast<int>(prefix.size()), prefix.data(), static_cast<int>(prefix.size()), TRUE) == CSTR_EQUAL;
    }
    return str.starts_with(prefix);
}

inline bool ends_with(const std::string_view str, const std::string_view suffix, bool case_sensitive)
{
    if (str.size() < suffix.size())
        return false;
    return starts_with(str.substr(str.size() - suffix.size()), suffix, case_sensitive);
}

inline bool ends_with(const std::wstring_view str, const std::wstring_view suffix, bool case_sensitive)
{
    if (str.size() < suffix.size())
        return false;
    return starts_with(str.substr(str.size() - suffix.size()), suffix, case_sensitive);
}

inline bool iequals(const std::wstring_view a, const std::wstring_view b)
{
    return a.size() == b.size() && starts_with(a, b, false);
}

inline bool iequals(const std::string_view a, const std::string_view b)
{
    return a.size() == b.size() && starts_with(a, b, false);
}

template<typename T>
std::wstring toLowerWStr(const T& arg)
{
    std::wstring source(arg.begin(), arg.end());
    std::wstring ret(source.size(), L'\0');
    if (!source.empty() && !LCMapStringEx(LOCALE_NAME_INVARIANT, LCMAP_LOWERCASE, source.data(), static_cast<int>(source.size()), ret.data(), static_cast<int>(ret.size()), nullptr, nullptr, 0))
        return source;
    return ret;
}

template<typename T>
std::string toLowerStr(const T& arg)
{
    std::string ret;
    ret.reserve(arg.size());
    for (auto c : arg)
        ret.push_back(static_cast<char>(::tolower(static_cast<unsigned char>(c))));
    return ret;
}

inline std::wstring toWString(std::string_view string)
{
    if (string.empty()) return std::wstring();
    int size_needed = MultiByteToWideChar(CP_UTF8, 0, string.data(), (int)string.size(), NULL, 0);
    std::wstring wstrTo(size_needed, 0);
    MultiByteToWideChar(CP_UTF8, 0, string.data(), (int)string.size(), &wstrTo[0], size_needed);
    return wstrTo;
}

inline std::string toString(std::wstring_view wstring)
{
    if (wstring.empty()) return std::string();
    int size_needed = WideCharToMultiByte(CP_UTF8, 0, wstring.data(), (int)wstring.size(), NULL, 0, NULL, NULL);
    std::string strTo(size_needed, 0);
    WideCharToMultiByte(CP_UTF8, 0, wstring.data(), (int)wstring.size(), &strTo[0], size_needed, NULL, NULL);
    return strTo;
}

// Converts a narrow string from the given code page (e.g. CP_ACP for argv) to UTF-16
inline std::wstring toWStringCP(std::string_view string, UINT codePage)
{
    if (string.empty()) return std::wstring();
    int size_needed = MultiByteToWideChar(codePage, 0, string.data(), (int)string.size(), NULL, 0);
    std::wstring wstrTo(size_needed, 0);
    MultiByteToWideChar(codePage, 0, string.data(), (int)string.size(), &wstrTo[0], size_needed);
    return wstrTo;
}

template<typename T>
void removeQuotesFromString(T& string)
{
    if (!string.empty() && (string.front() == '\"' || string.front() == '\''))
        string.erase(0, 1);
    if (!string.empty() && (string.back() == '\"' || string.back() == '\''))
        string.pop_back();
}

template<typename T>
T trimString(const T& string)
{
    auto isSpace = [](auto c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; };
    size_t begin = 0, end = string.size();
    while (begin < end && isSpace(string[begin])) ++begin;
    while (end > begin && isSpace(string[end - 1])) --end;
    return string.substr(begin, end - begin);
}

template<typename T>
size_t find_nth(const T& haystack, size_t pos, const T& needle, size_t nth)
{
    size_t found_pos = haystack.find(needle, pos);
    if (0 == nth || T::npos == found_pos)  return found_pos;
    return find_nth(haystack, found_pos + 1, needle, nth - 1);
}

template<typename T>
bool string_replace(T& str, const T& from, const T& to)
{
    size_t start_pos = str.find(from);
    if (start_pos == T::npos)
        return false;
    str.replace(start_pos, from.length(), to);
    return true;
}

template<typename T>
size_t string_replace_all(T& str, const T& from, const T& to)
{
    if (from.empty())
        return 0;
    size_t count = 0;
    for (size_t pos = str.find(from); pos != T::npos; pos = str.find(from, pos + to.length()))
    {
        str.replace(pos, from.length(), to);
        ++count;
    }
    return count;
}

inline std::string formatBytes(uint64_t bytes, int32_t precision = 2)
{
    if (bytes == 0)
        return std::string("0 Bytes");

    constexpr double k = 1000.0;
    const char* sizes[] = { "Bytes", "KB", "MB", "GB", "TB", "PB", "EB" };
    size_t i = static_cast<size_t>(std::floor(std::log(static_cast<double>(bytes)) / std::log(k)));
    i = std::min<size_t>(i, std::size(sizes) - 1);
    if (i == 0)
        return std::to_string(bytes) + " Bytes";
    std::ostringstream out;
    out << std::fixed << std::setprecision(precision) << (static_cast<double>(bytes) / std::pow(k, static_cast<double>(i)));
    return std::string(out.str() + ' ' + sizes[i]);
}

inline std::wstring formatBytesW(uint64_t bytes, int32_t precision = 2)
{
    return toWString(formatBytes(bytes, precision));
}

inline std::string getTimeAgo(int32_t hours)
{
    double deltaSeconds = hours * 3600.0;
    double deltaMinutes = deltaSeconds / 60.0;
    int32_t tmp;

    if (deltaSeconds < 5)
    {
        return "just now";
    }
    else if (deltaSeconds < 60)
    {
        return std::to_string(static_cast<int32_t>(deltaSeconds)) + " seconds ago";
    }
    else if (deltaSeconds < 120)
    {
        return "a minute ago";
    }
    else if (deltaMinutes < 60)
    {
        return std::to_string(static_cast<int32_t>(deltaMinutes)) + " minutes ago";
    }
    else if (deltaMinutes < 120)
    {
        return "an hour ago";
    }
    else if (deltaMinutes < (24 * 60))
    {
        tmp = (int)floor(deltaMinutes / 60);
        return std::to_string(tmp) + " hours ago";
    }
    else if (deltaMinutes < (24 * 60 * 2))
    {
        return "yesterday";
    }
    else if (deltaMinutes < (24 * 60 * 7))
    {
        tmp = (int)floor(deltaMinutes / (60 * 24));
        return std::to_string(tmp) + " days ago";
    }
    else if (deltaMinutes < (24 * 60 * 14))
    {
        return "last week";
    }
    else if (deltaMinutes < (24 * 60 * 31))
    {
        tmp = (int)floor(deltaMinutes / (60 * 24 * 7));
        return std::to_string(tmp) + " weeks ago";
    }
    else if (deltaMinutes < (24 * 60 * 61))
    {
        return "last month";
    }
    else if (deltaMinutes < (24 * 60 * 365.25))
    {
        tmp = (int)floor(deltaMinutes / (60 * 24 * 30));
        return std::to_string(tmp) + " months ago";
    }
    else if (deltaMinutes < (24 * 60 * 731))
    {
        return "last year";
    }

    tmp = (int)floor(deltaMinutes / (60 * 24 * 365));
    return std::to_string(tmp) + " years ago";
}

inline std::wstring getTimeAgoW(int32_t hours)
{
    return toWString(getTimeAgo(hours));
}
