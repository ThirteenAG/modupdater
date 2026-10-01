#include "stdafx.h"
#include "inimerge.h"

namespace mu
{
    namespace
    {
        constexpr std::string_view kBom = "\xEF\xBB\xBF";

        char AsciiLower(char c)
        {
            return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
        }

        bool EqualsNoCase(std::string_view a, std::string_view b)
        {
            return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin(), [](char x, char y) { return AsciiLower(x) == AsciiLower(y); });
        }

        bool IsBlank(char c)
        {
            return c == ' ' || c == '\t';
        }

        std::string_view Trim(std::string_view s)
        {
            while (!s.empty() && IsBlank(s.front()))
                s.remove_prefix(1);
            while (!s.empty() && IsBlank(s.back()))
                s.remove_suffix(1);
            return s;
        }

        enum class Kind
        {
            Other,      // blank line, comment, text without '='
            Section,
            Key,
        };

        struct Line
        {
            std::string text;           // without the line break
            Kind kind = Kind::Other;
            bool inSection = false;     // false before the first section
            std::string section;        // the section the line is in, the name of a section line
            std::string key;
            size_t equals = 0;          // position of '='
            size_t valueBegin = 0;      // the value without the blanks around it and without the comment
            size_t valueEnd = 0;

            std::string_view Value() const { return std::string_view(text).substr(valueBegin, valueEnd - valueBegin); }
        };

        // ';', '#' or "//" after a blank; "https://..." stays a value
        size_t FindComment(std::string_view text, size_t from)
        {
            for (size_t i = std::max<size_t>(from, 1); i < text.size(); i++)
            {
                bool marker = text[i] == ';' || text[i] == '#' || (text[i] == '/' && i + 1 < text.size() && text[i + 1] == '/');
                if (marker && IsBlank(text[i - 1]))
                    return i;
            }
            return text.size();
        }

        std::vector<Line> Parse(std::string_view text)
        {
            std::vector<Line> lines;
            std::string section;
            bool inSection = false;
            size_t pos = 0;
            while (pos < text.size())
            {
                size_t end = text.find('\n', pos);
                if (end == std::string_view::npos)
                    end = text.size();
                auto raw = text.substr(pos, end - pos);
                pos = end + 1;
                if (!raw.empty() && raw.back() == '\r')
                    raw.remove_suffix(1);

                Line line;
                line.text = raw;
                auto trimmed = Trim(raw);
                if (trimmed.starts_with('['))
                {
                    auto close = trimmed.find(']');
                    if (close != std::string_view::npos)
                    {
                        section = Trim(trimmed.substr(1, close - 1));
                        inSection = true;
                        line.kind = Kind::Section;
                    }
                }
                else if (!trimmed.empty() && trimmed.front() != ';' && trimmed.front() != '#' && !trimmed.starts_with("//"))
                {
                    auto equals = raw.find('=');
                    auto key = equals == std::string_view::npos ? std::string_view() : Trim(raw.substr(0, equals));
                    if (!key.empty())
                    {
                        line.kind = Kind::Key;
                        line.key = key;
                        line.equals = equals;
                        size_t valueEnd = FindComment(raw, equals + 1);
                        while (valueEnd > equals + 1 && IsBlank(raw[valueEnd - 1]))
                            valueEnd--;
                        size_t valueBegin = equals + 1;
                        while (valueBegin < valueEnd && IsBlank(raw[valueBegin]))
                            valueBegin++;
                        line.valueBegin = valueBegin;
                        line.valueEnd = valueEnd;
                    }
                }
                line.inSection = inSection;
                line.section = section;
                lines.push_back(std::move(line));
            }
            return lines;
        }

        bool SameKey(const Line& a, const Line& b)
        {
            return a.inSection == b.inSection && EqualsNoCase(a.section, b.section) && EqualsNoCase(a.key, b.key);
        }

        // The line with another value. The spaces in front of a comment absorb the different length,
        // the comments of the following lines stay aligned.
        std::string WithValue(const Line& line, std::string_view value)
        {
            std::string_view text = line.text;
            std::string result(text.substr(0, line.valueBegin));
            // "Key =" without a value gets "Key = value", "Key=value" stays like that
            if (line.valueBegin == line.valueEnd && line.valueBegin == line.equals + 1 && line.equals > 0 && IsBlank(text[line.equals - 1]) && !value.empty())
                result += ' ';
            result += value;

            auto rest = text.substr(line.valueEnd);
            auto spaces = rest.find_first_not_of(' ');
            if (spaces != std::string_view::npos && spaces > 0)
            {
                auto shift = static_cast<ptrdiff_t>(result.size()) - static_cast<ptrdiff_t>(line.valueEnd);
                auto keep = std::max<ptrdiff_t>(1, static_cast<ptrdiff_t>(spaces) - shift);
                result.append(static_cast<size_t>(keep), ' ');
                rest.remove_prefix(spaces);
            }
            result += rest;
            return result;
        }
    }

    std::string MergeIniText(std::string_view oldText, std::string_view newText)
    {
        const bool bom = newText.starts_with(kBom);
        if (bom)
            newText.remove_prefix(kBom.size());
        if (oldText.starts_with(kBom))
            oldText.remove_prefix(kBom.size());

        const std::string_view eol = newText.find("\r\n") != std::string_view::npos ? "\r\n" : "\n";
        const auto oldLines = Parse(oldText);
        const auto newLines = Parse(newText);

        std::vector<std::string> out;
        out.reserve(newLines.size());

        // where each section of the new file ends (after its last key), keys that only the old file has go there
        std::vector<std::pair<std::string, size_t>> sectionEnds;
        auto sectionEnd = [&](std::string_view section) -> size_t*
        {
            for (auto& [name, end] : sectionEnds)
            {
                if (EqualsNoCase(name, section))
                    return &end;
            }
            return nullptr;
        };

        for (auto& line : newLines)
        {
            if (line.kind == Kind::Key)
            {
                const Line* source = nullptr;
                for (auto& old : oldLines)
                {
                    if (old.kind == Kind::Key && SameKey(old, line))
                        source = &old;
                }
                out.push_back(source && source->Value() != line.Value() ? WithValue(line, source->Value()) : line.text);

                if (auto end = line.inSection ? sectionEnd(line.section) : nullptr)
                    *end = out.size();
            }
            else
            {
                out.push_back(line.text);
                if (line.kind == Kind::Section && !sectionEnd(line.section))
                    sectionEnds.emplace_back(line.section, out.size());
            }
        }

        // keys that only the old file has: added by the user or removed from the new version
        struct Extra
        {
            std::string section;
            std::string header;                 // the section line of the old file
            std::vector<const Line*> keys;      // the last one of duplicates, at the place of the first one
        };
        std::vector<Extra> extras;
        for (auto& old : oldLines)
        {
            if (old.kind != Kind::Key || !old.inSection)
                continue;
            if (std::any_of(newLines.begin(), newLines.end(), [&](const Line& line) { return line.kind == Kind::Key && SameKey(line, old); }))
                continue;

            auto extra = std::find_if(extras.begin(), extras.end(), [&](const Extra& e) { return EqualsNoCase(e.section, old.section); });
            if (extra == extras.end())
            {
                auto header = std::find_if(oldLines.begin(), oldLines.end(), [&](const Line& line) { return line.kind == Kind::Section && EqualsNoCase(line.section, old.section); });
                extras.push_back({ old.section, header != oldLines.end() ? header->text : "[" + old.section + "]", {} });
                extra = std::prev(extras.end());
            }

            auto duplicate = std::find_if(extra->keys.begin(), extra->keys.end(), [&](const Line* key) { return EqualsNoCase(key->key, old.key); });
            if (duplicate != extra->keys.end())
                *duplicate = &old;
            else
                extra->keys.push_back(&old);
        }

        // into the sections of the new file, from the bottom up so that the positions stay valid
        std::vector<std::pair<size_t, const Extra*>> inserts;
        for (auto& extra : extras)
        {
            if (auto end = sectionEnd(extra.section))
                inserts.emplace_back(*end, &extra);
        }
        std::sort(inserts.begin(), inserts.end(), [](auto& a, auto& b) { return a.first > b.first; });
        for (auto& [position, extra] : inserts)
        {
            std::vector<std::string> texts;
            for (auto key : extra->keys)
                texts.push_back(key->text);
            out.insert(out.begin() + position, texts.begin(), texts.end());
        }

        // sections that the new file does not have
        for (auto& extra : extras)
        {
            if (sectionEnd(extra.section))
                continue;
            if (!out.empty() && !Trim(out.back()).empty())
                out.emplace_back();
            out.push_back(extra.header);
            for (auto key : extra.keys)
                out.push_back(key->text);
        }

        std::string result;
        if (bom)
            result += kBom;
        const bool finalBreak = newText.empty() || newText.back() == '\n';
        for (size_t i = 0; i < out.size(); i++)
        {
            result += out[i];
            if (i + 1 < out.size() || finalBreak)
                result += eol;
        }
        return result;
    }
}
