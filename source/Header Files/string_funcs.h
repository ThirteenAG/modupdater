#include <string>

template<typename T>
std::wstring toLowerWStr(T& arg)
{
    std::wstring ret;
    std::transform(arg.begin(), arg.end(), std::back_inserter(ret), ::tolower);
    return ret;
}

template<typename T>
std::string toLowerStr(T& arg)
{
    std::string ret;
    std::transform(arg.begin(), arg.end(), std::back_inserter(ret), ::tolower);
    return ret;
}

std::wstring toWString(std::string& string)
{
    std::wstring wstring; std::copy(string.begin(), string.end(), std::back_inserter(wstring));
    return wstring;
}

std::string toString(std::wstring& wstring)
{
    std::string string; std::copy(wstring.begin(), wstring.end(), std::back_inserter(string));
    return string;
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

std::string formatBytes(int32_t bytes, int32_t precision = 2)
{
    if (bytes == 0)
        return std::string("0 Bytes");

    auto k = 1000;
    char* sizes[] = { "Bytes", "KB", "MB", "GB", "TB", "PB", "EB", "ZB", "YB" };
    size_t i = (size_t)floor(log(bytes) / log(k));
    std::ostringstream out;
    out << std::fixed << std::setprecision(precision) << (bytes / pow(k, i));
    return std::string(out.str() + ' ' + sizes[i]);
};

std::wstring formatBytesW(int32_t bytes, int32_t precision = 2)
{
    return toWString(formatBytes(bytes, precision));
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

bool folderExists(const char* folderName) {
    if (_access(folderName, 0) == -1) {
        //File not found
        return false;
    }

    DWORD attr = GetFileAttributes((LPCSTR)folderName);
    if (!(attr & FILE_ATTRIBUTE_DIRECTORY)) {
        // File is not a directory
        return false;
    }

    return true;
}

bool folderExists(const wchar_t* folderName) {
    if (_waccess(folderName, 0) == -1) {
        //File not found
        return false;
    }

    DWORD attr = GetFileAttributesW((LPCWSTR)folderName);
    if (!(attr & FILE_ATTRIBUTE_DIRECTORY)) {
        // File is not a directory
        return false;
    }

    return true;
}

bool createFolder(std::string folderName) {
    std::list<std::string> folderLevels;
    char* c_str = (char*)folderName.c_str();

    // Point to end of the string
    char* strPtr = &c_str[strlen(c_str) - 1];

    // Create a list of the folders which do not currently exist
    do {
        if (folderExists(c_str)) {
            break;
        }
        // Break off the last folder name, store in folderLevels list
        do {
            strPtr--;
        } while ((*strPtr != '\\') && (*strPtr != '/') && (strPtr >= c_str));
        folderLevels.push_front(std::string(strPtr + 1));
        strPtr[1] = 0;
    } while (strPtr >= c_str);

    if (_chdir(c_str)) {
        return true;
    }

    // Create the folders iteratively
    for (std::list<std::string>::iterator it = folderLevels.begin(); it != folderLevels.end(); it++) {
        if (CreateDirectory(it->c_str(), NULL) == 0) {
            return true;
        }
        _chdir(it->c_str());
    }

    return false;
}

bool createFolder(std::wstring folderName) {
    std::list<std::wstring> folderLevels;
    wchar_t* c_str = (wchar_t*)folderName.c_str();

    // Point to end of the string
    wchar_t* strPtr = &c_str[wcslen(c_str) - 1];

    // Create a list of the folders which do not currently exist
    do {
        if (folderExists(c_str)) {
            break;
        }
        // Break off the last folder name, store in folderLevels list
        do {
            strPtr--;
        } while ((*strPtr != '\\') && (*strPtr != '/') && (strPtr >= c_str));
        folderLevels.push_front(std::wstring(strPtr + 1));
        strPtr[1] = 0;
    } while (strPtr >= c_str);

    if (_wchdir(c_str)) {
        return true;
    }

    // Create the folders iteratively
    for (std::list<std::wstring>::iterator it = folderLevels.begin(); it != folderLevels.end(); it++) {
        if (CreateDirectoryW(it->c_str(), NULL) == 0) {
            return true;
        }
        _wchdir(it->c_str());
    }

    return false;
}

std::string getTimeAgo(int32_t hours)
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

std::wstring getTimeAgoW(int32_t hours)
{
    return toWString(getTimeAgo(hours));
}