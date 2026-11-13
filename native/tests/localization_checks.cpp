// Checks the source-language fallback, a locale lookup, context fallback and
// the legacy Chinese data bridge. Run from the repository's test build so
// LocaleDir() can find locales/ by walking up.
#include "localization.h"

#include <winrt/Windows.Foundation.h>

#include <iostream>
#include <string>

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

    std::wcout << L"localization passed\n";
    return 0;
}
