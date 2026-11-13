// Checks the source-language fallback, a locale lookup, context fallback and
// the legacy Chinese data bridge. Run from the repository's test build so
// LocaleDir() can find locales/ by walking up.
#include "localization.h"
#include "localization_format.h"

#include <winrt/Windows.Foundation.h>

#include <iostream>
#include <string>
#include <atomic>
#include <thread>

namespace {
int failures = 0;
void Check(bool condition, const char* message)
{
    if (!condition) { std::cerr << "FAIL: " << message << '\n'; ++failures; }
}
}

int wmain()
{
    winrt::init_apartment();
    using namespace atrium;

    SetLanguage(L"en");
    if (std::wstring(Tr(L"Open")) != L"Open")
        return 1; // the English key is the fallback
    if (std::wstring(Tr(L"设置")) != L"Settings") {
        std::wcerr << L"legacy result: " << Tr(L"设置") << L"\n";
        return 2; // legacy OS/data names still bridge to English
    }

    SetLanguage(L"zh-Hans");
    if (std::wstring(Tr(L"Open")) != L"打开")
        return 3;
    if (std::wstring(Tr(L"Open", L"menu")) != L"打开")
        return 4; // an absent context falls back to the plain key
    if (std::wstring(Tr(L"设置")) != L"设置")
        return 5;
    if (TrPlural(L"{0} test items", 2) != L"2 项测试")
        return 6;

    SetLanguage(L"xx-missing");
    if (std::wstring(Tr(L"Open")) != L"Open")
        return 7; // an unknown locale falls back to English

    SetLanguage(L"zh-Hans");
    const wchar_t* saved = Tr(L"Could not write %s: %s (error %lu)");
    const std::wstring expected = saved;
    for (int i = 0; i < 50; ++i) {
        Check(!SetLanguage(L"zh-Hans"), "same-language application is a no-op");
        Check(Tr(L"Could not write %s: %s (error %lu)") == saved, "no-op settings reuse the same immutable strings");
        SetLanguage(L"en");
        Check(std::wstring(saved) == expected, "pointers remain valid after a language switch");
        SetLanguage(L"zh-Hans");
    }
    SetLanguage(L"x-test");
    Check(std::wstring(Tr(L"Open")) == L"Aç", "non-Chinese resources load");
    Check(std::wstring(Tr(L"Open", L"menu")) == L"Menüyü aç" && std::wstring(Tr(L"打开", L"menu")) == L"Menüyü aç", "contexts survive legacy aliases");
    Check(std::wstring(Tr(L"Open", L"absent")) == L"Aç", "contexts fall back to base keys");
    Check(std::wstring(Tr(L"Empty")) == L"Empty", "empty translations fall back");
    Check(std::wstring(Tr(L"Nul")) == L"Nul", "embedded NUL translations fall back");
    Check(TrFormat(L"Read %s: %lu", L"file", 7ul) == L"Read file: 7", "wrong translated printf order falls back to English");
    Check(TrFormat(L"Count %d", 5) == L"Count 5", "%n never reaches the CRT");
    Check(TrFormat(L"Amount %.0f", 5.0) == L"Amount 5", "a translation cannot introduce huge precision");
    Check(TrFormat(L"Stars %*.*f", 6, 2, 1.25).find(L"1.25") != std::wstring::npos, "width/precision arguments survive fallback");
    Check(TrPlural(L"{0} files", 1) == L"ONE 1" && TrPlural(L"{0} files", 2) == L"OTHER 2", "plural branches work");
    Check(TrPlural(L"{0} files", 2, L"menu") == L"MENU OTHER 2", "plural contexts work");
    Check(TrPlural(L"{0} and {0}", 2) == L"2 / 2", "all count occurrences are replaced");
    Check(TrPlural(L"{{{0}}} files", 2) == L"{2} test", "escaped braces stay literal");
    Check(TrPlural(L"{0} bad", 2) == L"2 bad" && TrPlural(L"{0} incomplete", 1) == L"1 incomplete", "invalid plurals fall back");
    SetLanguage(L"x-malformed");
    Check(LanguageTag() == L"en" && std::wstring(Tr(L"Open")) == L"Open", "malformed resources do not retain a partial locale");
    SetLanguage(L"x-bom");
    Check(std::wstring(Tr(L"Open")) == L"BOM loaded", "UTF-8 BOM resources load correctly");
    SetLanguage(L"../../x-test");
    Check(LanguageTag() == L"en", "language tags cannot traverse directories");
    SetLanguage(L"ZH-hans");
    Check(LanguageTag() == L"zh-Hans", "language tags and zh aliases are normalized");
    Check(localization::PrintfMatches(L"%*.*f %%", L"%*.*f %%"), "printf stars and literal percent work");
    Check(!localization::PrintfMatches(L"%s %lu", L"%lu %s") && !localization::PrintfTokens(L"%2$s"), "printf type order and unsupported positional arguments are rejected");

    std::atomic_bool good = true, ready = false;
    std::thread reader([&] {
        ready = true;
        for (int i = 0; i < 3000; ++i) {
            const std::wstring text = Tr(L"Open");
            if (text != L"Open" && text != L"打开" && text != L"Aç") good = false;
            if (TrFormat(L"Count %d", 5) != L"Count 5") good = false;
        }
    });
    while (!ready) std::this_thread::yield();
    for (int i = 0; i < 1500; ++i) SetLanguage(i % 3 == 0 ? L"en" : i % 3 == 1 ? L"zh-Hans" : L"x-test");
    reader.join();
    Check(good, "concurrent formatting and settings changes keep valid text");
    std::cout << "localization checks completed\n";
    return failures ? 1 : 0;
}
