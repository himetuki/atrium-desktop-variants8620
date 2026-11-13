#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace atrium::localization {

inline bool ValidTag(std::wstring_view tag)
{
    if (tag.empty() || tag.size() > 32 || tag.front() == L'-' || tag.back() == L'-') return false;
    bool separator = false;
    for (wchar_t ch : tag) {
        if (!((ch >= L'a' && ch <= L'z') || (ch >= L'A' && ch <= L'Z') ||
              (ch >= L'0' && ch <= L'9') || ch == L'-') || (ch == L'-' && separator)) return false;
        separator = ch == L'-';
    }
    return true;
}

// Preserve complete conversions in order, including argument-consuming stars
// and size modifiers. %n and positional parameters are unsupported.
inline std::optional<std::vector<std::wstring>> PrintfTokens(std::wstring_view text)
{
    std::vector<std::wstring> tokens;
    auto digit = [](wchar_t ch) { return ch >= L'0' && ch <= L'9'; };
    for (size_t i = 0; i < text.size(); ++i) {
        if (text[i] != L'%') continue;
        const size_t start = i++;
        if (i == text.size()) return std::nullopt;
        if (text[i] == L'%') continue;
        while (i < text.size() && std::wstring_view(L"-+ #0").find(text[i]) != std::wstring_view::npos) ++i;
        if (i < text.size() && text[i] == L'*') ++i;
        else while (i < text.size() && digit(text[i])) ++i;
        if (i < text.size() && text[i] == L'.') {
            ++i;
            if (i < text.size() && text[i] == L'*') ++i;
            else while (i < text.size() && digit(text[i])) ++i;
        }
        for (auto size : {L"I32", L"I64", L"hh", L"ll", L"h", L"l", L"j", L"z", L"t", L"L", L"I", L"w"}) {
            const std::wstring_view modifier(size);
            if (text.substr(i).starts_with(modifier)) { i += modifier.size(); break; }
        }
        if (i == text.size() || std::wstring_view(L"diouxXfFeEgGaAcCsSp").find(text[i]) == std::wstring_view::npos)
            return std::nullopt;
        tokens.emplace_back(text.substr(start, i - start + 1));
    }
    return tokens;
}

inline bool PrintfMatches(std::wstring_view source, std::wstring_view translated)
{
    const auto expected = PrintfTokens(source), actual = PrintfTokens(translated);
    return expected && actual && *expected == *actual;
}

inline int CountTokens(std::wstring_view text)
{
    int count = 0;
    for (size_t i = 0; i < text.size(); ++i) {
        if ((text[i] == L'{' || text[i] == L'}') && i + 1 < text.size() && text[i + 1] == text[i]) { ++i; continue; }
        if (text[i] == L'{') {
            if (text.substr(i, 3) != L"{0}") return -1;
            ++count; i += 2;
        } else if (text[i] == L'}') return -1;
    }
    return count;
}

inline bool CountMatches(std::wstring_view source, std::wstring_view translated)
{
    const int expected = CountTokens(source);
    return expected >= 0 && expected == CountTokens(translated);
}

inline std::wstring SubstituteCount(std::wstring_view text, int count)
{
    const std::wstring number = std::to_wstring(count);
    std::wstring out;
    for (size_t i = 0; i < text.size(); ++i) {
        if ((text[i] == L'{' || text[i] == L'}') && i + 1 < text.size() && text[i + 1] == text[i]) { out += text[i++]; }
        else if (text.substr(i, 3) == L"{0}") { out += number; i += 2; }
        else out += text[i];
    }
    return out;
}

} // namespace atrium::localization
