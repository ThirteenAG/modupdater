#include <string>

inline bool starts_with(const std::string_view str, const std::string_view prefix, bool case_sensitive)
{
    if (!case_sensitive)
    {
        std::string str1(str); std::string str2(prefix);
        std::transform(str1.begin(), str1.end(), str1.begin(), ::tolower);
        std::transform(str2.begin(), str2.end(), str2.begin(), ::tolower);
        return str1.starts_with(str2);
    }
    return str.starts_with(prefix);
}

inline bool starts_with(const std::wstring_view str, const std::wstring_view prefix, bool case_sensitive)
{
    if (!case_sensitive)
    {
        std::wstring str1(str); std::wstring str2(prefix);
        std::transform(str1.begin(), str1.end(), str1.begin(), ::towlower);
        std::transform(str2.begin(), str2.end(), str2.begin(), ::towlower);
        return str1.starts_with(str2);
    }
    return str.starts_with(prefix);
}

inline bool ends_with(const std::string_view str, const std::string_view prefix, bool case_sensitive)
{
    if (!case_sensitive)
    {
        std::string str1(str); std::string str2(prefix);
        std::transform(str1.begin(), str1.end(), str1.begin(), ::tolower);
        std::transform(str2.begin(), str2.end(), str2.begin(), ::tolower);
        return str1.ends_with(str2);
    }
    return str.ends_with(prefix);
}

inline bool ends_with(const std::wstring_view str, const std::wstring_view prefix, bool case_sensitive)
{
    if (!case_sensitive)
    {
        std::wstring str1(str); std::wstring str2(prefix);
        std::transform(str1.begin(), str1.end(), str1.begin(), ::towlower);
        std::transform(str2.begin(), str2.end(), str2.begin(), ::towlower);
        return str1.ends_with(str2);
    }
    return str.ends_with(prefix);
}

template<typename T>
std::wstring toLowerWStr(T arg)
{
    std::wstring ret;
    std::transform(arg.begin(), arg.end(), std::back_inserter(ret), ::tolower);
    return ret;
}

template<typename T>
std::string toLowerStr(T arg)
{
    std::string ret;
    std::transform(arg.begin(), arg.end(), std::back_inserter(ret), ::tolower);
    return ret;
}

inline std::wstring toWString(const std::string& string)
{
    if (string.empty()) return std::wstring();
    int size_needed = MultiByteToWideChar(CP_UTF8, 0, &string[0], (int)string.size(), NULL, 0);
    std::wstring wstrTo(size_needed, 0);
    MultiByteToWideChar(CP_UTF8, 0, &string[0], (int)string.size(), &wstrTo[0], size_needed);
    return wstrTo;
}

inline std::string toString(const std::wstring& wstring)
{
    if (wstring.empty()) return std::string();
    int size_needed = WideCharToMultiByte(CP_UTF8, 0, &wstring[0], (int)wstring.size(), NULL, 0, NULL, NULL);
    std::string strTo(size_needed, 0);
    WideCharToMultiByte(CP_UTF8, 0, &wstring[0], (int)wstring.size(), &strTo[0], size_needed, NULL, NULL);
    return strTo;
}

template<typename T>
void removeQuotesFromString(T& string)
{
    if (string.at(0) == '\"' || string.at(0) == '\'')
        string.erase(0, 1);
    if (string.at(string.size() - 1) == '\"' || string.at(string.size() - 1) == '\'')
        string.erase(string.size() - 1);
}

template<typename T>
size_t find_nth(const T& haystack, size_t pos, const T& needle, size_t nth)
{
    size_t found_pos = haystack.find(needle, pos);
    if (0 == nth || T::npos == found_pos)  return found_pos;
    return find_nth(haystack, found_pos + 1, needle, nth - 1);
}

template<typename T>
bool string_replace(T& str, const T& from, const T& to) {
    size_t start_pos = str.find(from);
    if (start_pos == T::npos)
        return false;
    str.replace(start_pos, from.length(), to);
    return true;
}

inline std::string formatBytes(int32_t bytes, int32_t precision = 2)
{
    if (bytes == 0)
        return std::string("0 Bytes");

    auto k = 1000;
    const char* sizes[] = { "Bytes", "KB", "MB", "GB", "TB", "PB", "EB", "ZB", "YB" };
    size_t i = (size_t)floor(log(bytes) / log(k));
    std::ostringstream out;
    out << std::fixed << std::setprecision(precision) << (bytes / pow(k, i));
    return std::string(out.str() + ' ' + sizes[i]);
};

inline std::wstring formatBytesW(int32_t bytes, int32_t precision = 2)
{
    auto s = formatBytes(bytes, precision);
    return toWString(s);
};

template<typename T>
T GetLongestCommonSubstring(const T & first, const T & second)
{
    auto findSubstrings = [](const T& word, std::set<T>& substrings)->void
    {
        int l = word.length();
        for (int start = 0; start < l; start++) {
            for (int length = 1; length < l - start + 1; length++) {
                substrings.insert(word.substr(start, length));
            }
        }
    };

    std::set<T> firstSubstrings, secondSubstrings;
    findSubstrings(first, firstSubstrings);
    findSubstrings(second, secondSubstrings);
    std::set<T> common;
    std::set_intersection(firstSubstrings.begin(), firstSubstrings.end(), secondSubstrings.begin(), secondSubstrings.end(), std::inserter(common, common.begin()));
    std::vector<T> commonSubs(common.begin(), common.end());
    std::sort(commonSubs.begin(), commonSubs.end(), [](const T &s1, const T &s2) { return s1.length() > s2.length(); });
    if (!commonSubs.empty())
        return *(commonSubs.begin());
    else
        return L"";
}

inline std::string getTimeAgo(int32_t hours)
{
    double deltaSeconds = hours * 3600;
    double deltaMinutes = deltaSeconds / 60.0f;
    int32_t tmp;

    if (deltaSeconds < 5)
    {
        return "just now";
    }
    else if (deltaSeconds < 60)
    {
        return std::to_string(floor(deltaSeconds)) + " seconds ago";
    }
    else if (deltaSeconds < 120)
    {
        return "a minute ago";
    }
    else if (deltaMinutes < 60)
    {
        return std::to_string(floor(deltaMinutes)) + " minutes ago";
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
    auto s = getTimeAgo(hours);
    return toWString(s);
}