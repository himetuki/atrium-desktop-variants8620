#include "localization.h"
#include "localization_format.h"

#include <windows.h>

#include <winrt/Windows.Data.Json.h>
#include <winrt/Windows.Foundation.Collections.h>

#include <atomic>
#include <cstdarg>
#include <cstdio>
#include <cwchar>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <mutex>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace atrium {

namespace {

namespace json = winrt::Windows::Data::Json;

struct Locale {
    std::wstring tag = L"en";
    std::unordered_map<std::wstring, std::wstring> strings;
    std::unordered_map<std::wstring, std::unordered_map<std::wstring, std::wstring>> plurals;
    std::unordered_map<std::wstring, std::wstring> legacy;
};

std::atomic<std::shared_ptr<const Locale>> g_active{
    std::make_shared<Locale>()};
std::mutex g_switch;
// Tr pointers can outlive a call or a language change. Cache each immutable
// locale once, avoiding both dangling strings and a retired copy per edit.
std::unordered_map<std::wstring, std::shared_ptr<const Locale>> g_loaded;
std::unordered_map<std::wstring, std::wstring> g_legacy;
bool g_legacyLoaded = false;


std::wstring Utf8ToWide(const std::string& text)
{
    if (text.empty())
        return {};
    const int size = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), int(text.size()), nullptr, 0);
    if (size <= 0)
        return {};
    std::wstring out(size_t(size), L'\0');
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), int(text.size()), out.data(), size);
    if (!out.empty() && out.front() == 0xFEFF) out.erase(0, 1);
    return out;
}

std::string ReadFile(const std::filesystem::path& path)
{
    std::ifstream file(path, std::ios::binary);
    if (!file)
        return {};
    std::error_code error;
    const auto size = std::filesystem::file_size(path, error);
    if (error || size > 2 * 1024 * 1024) return {};
    return std::string(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
}

std::filesystem::path LocaleDir()
{
    wchar_t exe[MAX_PATH] = {};
    if (!GetModuleFileNameW(nullptr, exe, MAX_PATH))
        return {};
    std::filesystem::path dir(exe);
    dir.remove_filename();
    // The portable build has <exe>/locales; a development build from
    // native/build/... finds the repository's locales by walking up.
    for (int i = 0; i < 8 && !dir.empty(); ++i) {
        const std::filesystem::path candidate = dir / L"locales";
        std::error_code error;
        if (std::filesystem::exists(candidate / L"manifest.json", error))
            return candidate;
        dir = dir.parent_path();
    }
    return {};
}

std::wstring HString(const winrt::hstring& text) { return std::wstring(text.c_str()); }

void LoadStrings(const json::JsonObject& root, Locale& locale)
{
    if (!root.HasKey(L"strings") || root.GetNamedValue(L"strings").ValueType() != json::JsonValueType::Object)
        return;
    const json::JsonObject strings = root.GetNamedObject(L"strings");
    for (const auto& pair : strings) {
        const std::wstring key = HString(pair.Key());
        const json::IJsonValue value = pair.Value();
        if (value.ValueType() == json::JsonValueType::String) {
            const auto text = HString(value.GetString());
            if (!text.empty() && text.find(L'\0') == std::wstring::npos) locale.strings[key] = text;
        } else if (value.ValueType() == json::JsonValueType::Object) {
            const json::JsonObject forms = value.GetObject();
            std::unordered_map<std::wstring, std::wstring> plural;
            for (const auto& form : forms) {
                if (form.Value().ValueType() == json::JsonValueType::String)
                    if (auto text = HString(form.Value().GetString()); !text.empty() && text.find(L'\0') == std::wstring::npos)
                        plural[HString(form.Key())] = std::move(text);
            }
            if (plural.contains(L"other")) locale.plurals[key] = std::move(plural);
        }
    }
}

bool LoadLocale(const std::filesystem::path& path, Locale& locale)
{
    const std::string bytes = ReadFile(path);
    if (bytes.empty())
        return false;
    try {
        const json::JsonObject root = json::JsonObject::Parse(Utf8ToWide(bytes));
        LoadStrings(root, locale);
        return !locale.strings.empty() || !locale.plurals.empty();
    } catch (...) {
        locale.strings.clear();
        locale.plurals.clear();
        return false;
    }
}

void LoadLegacy(Locale& locale)
{
    if (!g_legacyLoaded) {
        Locale legacy;
        const auto directory = LocaleDir();
        if (!directory.empty() && LoadLocale(directory / L"legacy.json", legacy))
            g_legacy = std::move(legacy.strings);
        g_legacyLoaded = true;
    }
    locale.legacy = g_legacy;
}

const std::wstring* Find(const Locale& locale, const wchar_t* key, const wchar_t* context)
{
    if (!key || !*key)
        return nullptr;
    auto find = [&](const std::wstring& candidate) -> const std::wstring* {
        const auto it = locale.strings.find(candidate);
        return it == locale.strings.end() ? nullptr : &it->second;
    };
    const auto alias = locale.legacy.find(key);
    const wchar_t* source = alias == locale.legacy.end() ? key : alias->second.c_str();
    if (context && *context) {
        std::wstring qualified(source);
        qualified += L'#';
        qualified += context;
        if (const std::wstring* text = find(qualified))
            return text;
    }
    if (const std::wstring* text = find(source))
        return text;

    // OS data (jump list names, app names, cities, lunar festivals) arrives in
    // Chinese. The legacy table turns it into the English key, then the normal
    // locale lookup translates it. Static UI strings use English keys directly.
    if (alias != locale.legacy.end()) return &alias->second;
    return nullptr;
}

bool SystemPrefersChinese()
{
    return PRIMARYLANGID(GetUserDefaultUILanguage()) == LANG_CHINESE;
}

std::wstring NormalizeTag(std::wstring tag)
{
    for (auto& ch : tag) if (ch >= L'A' && ch <= L'Z') ch += L'a' - L'A';
    if (tag.empty() || tag == L"auto")
        return SystemPrefersChinese() ? L"zh-Hans" : L"en";
    if (!localization::ValidTag(tag)) return L"en";
    if (tag == L"zh" || tag == L"zh-hans")
        return L"zh-Hans";
    return tag;
}

} // namespace

bool SetLanguage(const std::wstring& setting)
{
    const std::wstring tag = NormalizeTag(setting);
    std::lock_guard lock(g_switch);
    if (g_loaded.empty()) {
        auto source = std::make_shared<Locale>();
        LoadLegacy(*source);
        g_loaded.emplace(L"en", std::move(source));
    }
    auto found = g_loaded.find(tag);
    if (found == g_loaded.end()) {
        auto locale = std::make_shared<Locale>();
        locale->tag = tag;
        LoadLegacy(*locale);
        const auto directory = LocaleDir();
        if (!directory.empty() && LoadLocale(directory / (tag + L".json"), *locale))
            found = g_loaded.emplace(tag, std::move(locale)).first;
        else
            found = g_loaded.find(L"en");
    }
    const auto before = g_active.load();
    const bool changed = before->tag != found->second->tag;
    g_active.store(found->second);
    return changed;
}

bool LanguageIsEnglish() { return g_active.load()->tag.rfind(L"zh", 0) != 0; }

std::wstring LanguageTag() { return g_active.load()->tag; }

const wchar_t* Tr(const wchar_t* key, const wchar_t* context)
{
    if (!key)
        return L"";
    const auto locale = g_active.load();
    if (const std::wstring* text = Find(*locale, key, context))
        return text->c_str();
    return key;
}

std::wstring TrFormat(const wchar_t* key, ...)
{
    if (!key) return {};
    const auto locale = g_active.load(); // own the entire formatting operation
    const auto alias = locale->legacy.find(key);
    const wchar_t* source = alias != locale->legacy.end() && localization::PrintfMatches(key, alias->second) ? alias->second.c_str() : key;
    const std::wstring* translated = Find(*locale, key, nullptr);
    const wchar_t* format = translated && localization::PrintfMatches(source, *translated) ? translated->c_str() : source;
    if (!localization::PrintfTokens(format)) return key;
    va_list args;
    va_start(args, key);
    const int length = _vscwprintf(format, args);
    va_end(args);
    if (length < 0 || length > 1024 * 1024) return key;
    if (length == 0) return {};
    std::wstring text(size_t(length), L'\0');
    va_start(args, key);
    vswprintf_s(text.data(), text.size() + 1, format, args);
    va_end(args);
    return text;
}

std::wstring TrPlural(const wchar_t* key, int count, const wchar_t* context)
{
    if (!key)
        return {};
    const auto locale = g_active.load();
    const auto alias = locale->legacy.find(key);
    const wchar_t* source = alias != locale->legacy.end() && localization::CountMatches(key, alias->second) ? alias->second.c_str() : key;
    std::wstring qualified(source);
    if (context && *context) {
        qualified += L'#';
        qualified += context;
    }
    auto findPlural = [&](const std::wstring& candidate) -> const std::unordered_map<std::wstring, std::wstring>* {
        const auto it = locale->plurals.find(candidate);
        if (it != locale->plurals.end())
            return &it->second;
        const auto legacy = locale->legacy.find(candidate);
        if (legacy != locale->legacy.end()) {
            const auto translated = locale->plurals.find(legacy->second);
            if (translated != locale->plurals.end())
                return &translated->second;
        }
        return nullptr;
    };
    const auto* forms = findPlural(qualified);
    if (!forms)
        forms = findPlural(source);
    std::wstring text;
    if (forms) {
        const auto one = forms->find(L"one");
        const auto other = forms->find(L"other");
        if (count == 1 && one != forms->end())
            text = one->second;
        else if (other != forms->end())
            text = other->second;
    }
    if (text.empty()) {
        const auto* fallback = Find(*locale, key, context);
        text = fallback ? *fallback : key;
    }
    if (!localization::CountMatches(source, text)) text = source;
    return localization::SubstituteCount(text, count);
}

} // namespace atrium
