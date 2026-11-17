// Whether the input method bubble stays quiet: over a fullscreen game the
// bottom bubble would sit on top of the play area, so it is muted by default
// while the bar's status icon still announces. The no-bubble list starts as a
// copy of Stage Manager's excluded applications (upstream 2.7.0's list of games
// the stage leaves alone) and later only ever grows with new stage entries;
// entries the user edits in Settings are pinned so a sync never re-adds them.
#pragma once

#include "ui/stage_excluded.h"

#include <cwctype>
#include <string>
#include <vector>

namespace atrium {

// A list entry as a game key is kept: surrounding blanks gone, the first
// spelling kept, an empty entry dropped.
inline std::wstring TrimAppEntry(const std::wstring& value)
{
    size_t first = 0, last = value.size();
    while (first < last && std::iswspace(value[first]))
        ++first;
    while (last > first && std::iswspace(value[last - 1]))
        --last;
    return value.substr(first, last - first);
}

// The no-bubble list after a sync: `games` is trimmed and deduplicated
// case-insensitively (the first spelling is kept), then every stage entry not
// pinned by the user and not already listed is appended. Additions only — an
// entry that leaves the stage list stays here.
inline std::vector<std::wstring> SyncNoBubbleGames(std::vector<std::wstring> games,
                                                   const std::vector<std::wstring>& pinned,
                                                   const std::vector<std::wstring>& stageExcluded)
{
    std::vector<std::wstring> out;
    out.reserve(games.size() + stageExcluded.size());
    auto listed = [&](const std::wstring& lower) {
        for (const std::wstring& entry : out)
            if (LowerAppKey(entry) == lower)
                return true;
        return false;
    };
    for (std::wstring& raw : games) {
        std::wstring entry = TrimAppEntry(raw);
        if (entry.empty() || listed(LowerAppKey(entry)))
            continue;
        out.push_back(std::move(entry));
    }
    for (const std::wstring& raw : stageExcluded) {
        const std::wstring entry = TrimAppEntry(raw);
        if (entry.empty())
            continue;
        const std::wstring lower = LowerAppKey(entry);
        bool userPinned = false;
        for (const std::wstring& pin : pinned)
            if (LowerAppKey(TrimAppEntry(pin)) == lower) {
                userPinned = true;
                break;
            }
        if (!userPinned && !listed(lower))
            out.push_back(entry);
    }
    return out;
}

// The bubble is muted only while the pause is on and the foreground app is on
// the no-bubble list; an empty list never mutes (the switch alone does nothing).
inline bool ImeBubbleMuted(bool pause, const std::vector<std::wstring>& games, const std::wstring& appKey)
{
    return pause && !games.empty() && StageAppExcluded(games, appKey);
}

} // namespace atrium
