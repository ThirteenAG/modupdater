#pragma once
#include <string>
#include <string_view>

namespace mu
{
    // Merges the settings of an ini file into a newer version of it: the result has the layout, comments
    // and keys of 'newText' with the values of 'oldText'.
    // - Section and key names are compared like Windows does, ignoring the case. Of duplicates the last one
    //   wins: files that the updater of FusionFix 5.1.0/5.1.1 broke (the new file, then the old settings in
    //   lowercase) get the settings of the user back.
    // - Keys that only the old file has stay at the end of their section, sections that only the old file
    //   has at the end of the file.
    // - Comments start with ';', '#' or "//" after a space or tab.
    // - The line breaks and the byte order mark of the new file are kept, text is handled as bytes (UTF-8).
    // Self-contained on purpose: mINI is a header-only library that the modules linking this library use with
    // other settings (IniReader.h defines MINI_CASE_SENSITIVE), the linker would mix both variants.
    std::string MergeIniText(std::string_view oldText, std::string_view newText);
}
