#include "localization.h"

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
std::vector<std::shared_ptr<const Locale>> g_retired;
std::unordered_map<std::wstring, std::wstring> g_legacy;
bool g_legacyLoaded = false;


std::wstring Utf8ToWide(const std::string& text)
{
    if (text.empty())
        return {};
    const int size = MultiByteToWideChar(CP_UTF8, 0, text.data(), int(text.size()), nullptr, 0);
    if (size <= 0)
        return {};
    std::wstring out(size_t(size), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.data(), int(text.size()), out.data(), size);
    return out;
}

std::string ReadFile(const std::filesystem::path& path)
{
    std::ifstream file(path, std::ios::binary);
    if (!file)
        return {};
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
        if (std::filesystem::exists(candidate / L"manifest.json"))
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
            locale.strings[key] = HString(value.GetString());
        } else if (value.ValueType() == json::JsonValueType::Object) {
            const json::JsonObject forms = value.GetObject();
            auto& plural = locale.plurals[key];
            for (const auto& form : forms) {
                if (form.Value().ValueType() == json::JsonValueType::String)
                    plural[HString(form.Key())] = HString(form.Value().GetString());
            }
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
        return false;
    }
}

void LoadLegacy(Locale& locale)
{
    if (!g_legacyLoaded) {
        Locale legacy;
        if (LoadLocale(LocaleDir() / L"legacy.json", legacy))
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
    if (context && *context) {
        std::wstring qualified(key);
        qualified += L'#';
        qualified += context;
        if (const std::wstring* text = find(qualified))
            return text;
    }
    if (const std::wstring* text = find(key))
        return text;

    // OS data (jump list names, app names, cities, lunar festivals) arrives in
    // Chinese. The legacy table turns it into the English key, then the normal
    // locale lookup translates it. Static UI strings use English keys directly.
    const auto legacy = locale.legacy.find(key);
    if (legacy != locale.legacy.end()) {
        if (const std::wstring* text = find(legacy->second))
            return text;
        return &legacy->second;
    }
    return nullptr;
}

bool SystemPrefersChinese()
{
    return PRIMARYLANGID(GetUserDefaultUILanguage()) == LANG_CHINESE;
}

std::wstring NormalizeTag(std::wstring tag)
{
    if (tag.empty() || tag == L"auto")
        return SystemPrefersChinese() ? L"zh-Hans" : L"en";
    if (tag == L"zh")
        return L"zh-Hans";
    return tag;
}

} // namespace

bool SetLanguage(const std::wstring& setting)
{
    const std::wstring tag = NormalizeTag(setting);
    auto locale = std::make_shared<Locale>();
    locale->tag = tag;
    LoadLegacy(*locale);
    if (tag != L"en" && !LoadLocale(LocaleDir() / (tag + L".json"), *locale)) {
        locale->tag = L"en";
        locale->strings.clear();
        locale->plurals.clear();
    }

    std::lock_guard lock(g_switch);
    const auto before = g_active.load();
    const bool changed = before->tag != locale->tag;
    if (before->tag != locale->tag || before->strings != locale->strings)
        g_retired.push_back(before); // Tr() pointers may still be in use
    g_active.store(std::move(locale));
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
    const wchar_t* format = Tr(key);
    va_list args;
    va_start(args, key);
    const int length = _vscwprintf(format, args);
    va_end(args);
    if (length <= 0)
        return std::wstring(format);
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
    std::wstring qualified(key);
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
        forms = findPlural(key);
    std::wstring text;
    if (forms) {
        const auto one = forms->find(L"one");
        const auto other = forms->find(L"other");
        if (count == 1 && one != forms->end())
            text = one->second;
        else if (other != forms->end())
            text = other->second;
        else if (one != forms->end())
            text = one->second;
    }
    if (text.empty())
        text = Tr(key, context);
    const std::wstring number = std::to_wstring(count);
    if (const size_t at = text.find(L"{0}"); at != std::wstring::npos)
        text.replace(at, 3, number);
    return text;
}

} // namespace atrium
