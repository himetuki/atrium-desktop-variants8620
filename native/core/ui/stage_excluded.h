#pragma once

#include <algorithm>
#include <cwctype>
#include <string>
#include <vector>

namespace atrium {

// Entries are executable names (wechat.exe), package prefixes (microsoft.screensketch_) or
// complete app keys. An app is excluded when its key matches any of them; comparison is
// case-insensitive. Excluded apps are left out of Stage Manager entirely.
inline std::wstring LowerAppKey(std::wstring value)
{
    for (wchar_t& c : value)
        c = wchar_t(towlower(c));
    return value;
}

inline bool StageAppExcluded(const std::vector<std::wstring>& excluded, const std::wstring& key)
{
    // A window key appends '|' and the window handle; match against the app part only, so
    // older saved stage entries and per-window keys are recognized too.
    std::wstring appKey = key;
    if (const size_t bar = appKey.rfind(L'|'); bar != std::wstring::npos)
        appKey.resize(bar);
    const std::wstring app = LowerAppKey(appKey);
    const size_t slash = app.find_last_of(L"\\/");
    const std::wstring file = slash == std::wstring::npos ? app : app.substr(slash + 1);
    for (const std::wstring& raw : excluded) {
        const std::wstring entry = LowerAppKey(raw);
        if (entry.empty())
            continue;
        if (app == entry || file == entry || app.rfind(entry, 0) == 0)
            return true;
    }
    return false;
}

// The Windows screenshot tools that must not be swallowed by the stage while they record or
// capture: their own overlays/control bars are separate windows, and putting them away hides
// pause/stop from the user. Other screenshot apps can be added in Settings.
inline std::vector<std::wstring> DefaultStageExcluded()
{
    return {L"snippingtool.exe", L"screenclippinghost.exe", L"microsoft.screensketch_"};
}

} // namespace atrium
