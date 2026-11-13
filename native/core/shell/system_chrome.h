// The parts of Windows' own shell that Atrium can stand in for, hidden on the
// user's request (both settings are off by default): the system taskbar
// (Shell_TrayWnd and the secondary ones are hidden while Atrium runs, and set
// to auto-hide so they leave no gap) and the system's notification banners
// (Do Not Disturb through the shell's QuietHoursSettings service: the
// notifications still reach the notification centre, where the listener reads
// them for Atrium's cards). Everything is put back when the setting is turned
// off or Atrium exits; what was changed is kept in system.json, so a run that
// did not end cleanly is undone by the next start, or at once by the guard
// (`Atrium.exe --guard`, main.cpp) that watches a run hiding them.
#pragma once

#include <windows.h>

#include <atomic>
#include <string>

namespace atrium {

// The shell's Do Not Disturb profiles Atrium uses.
inline constexpr wchar_t kQuietProfile[] = L"Microsoft.QuietHoursProfile.AlarmsOnly";
inline constexpr wchar_t kQuietProfileOff[] = L"Microsoft.QuietHoursProfile.Unrestricted";

// What SetBannersHidden does to the shell's Do Not Disturb profile. Anything
// short of alarms-only lets priority notifications through: a "priority only"
// profile still shows e.g. the AutoPlay toast for a newly inserted drive, which
// reads as the setting not hiding system banners at all.
enum class QuietAction { none, hide, restore };

struct QuietPlan {
    QuietAction action = QuietAction::none;
    std::wstring profile; // hide: alarms-only; restore: the profile to put back
    bool owns = false;    // the change is Atrium's, to undo when the setting goes off
};

inline QuietPlan PlanQuiet(const std::wstring& current, bool hide, bool ours, const std::wstring& before)
{
    QuietPlan plan;
    if (hide) {
        // Nothing to do when the service is gone or the strict profile is
        // already selected (by Atrium or by the user).
        if (current.empty() || current == kQuietProfile)
            return plan;
        plan.action = QuietAction::hide;
        plan.profile = kQuietProfile;
        plan.owns = true;
        return plan;
    }
    if (ours) {
        plan.action = QuietAction::restore;
        plan.profile = before.empty() ? kQuietProfileOff : before;
    }
    return plan;
}

class SystemChrome {
public:
    SystemChrome();
    ~SystemChrome(); // puts everything back

    void SetTaskbarHidden(bool hidden);
    void SetBannersHidden(bool hidden);
    // Hides the taskbars again if Explorer has shown them (a restart of
    // Explorer, a display change); follows the user turning Do Not Disturb off.
    void Enforce();
    // Irreversible for this instance: disable every hide entry point, then
    // restore Windows before joining workers or waiting for the renderer.
    void RestoreForExit();
    // Do Not Disturb is in the alarms-only state the setting asks for (whether
    // Atrium switched it or the user already had it): it must not hide Atrium's
    // own cards then.
    bool QuietIsOurs() const { return quietReady_; }
    // From a crash handler: stops hiding, restores our auto-hide change and
    // shows the taskbars (no allocation, no COM).
    static void EmergencyRestore();
    // What a run that did not end cleanly left changed (system.json), put back; false if nothing
    // was. COM must be initialised (Do Not Disturb).
    static bool UndoLeftover();
    // The taskbars shown, nothing else changed (Atrium is not responding).
    static void ShowTaskbarsNow();
    // Around one real mouse click on a tray icon the taskbars must be visible;
    // the shell's own show-hook must not hide them again in between. Reveal
    // shows them (no-op flags otherwise), Conceal hides them back only when
    // Atrium had hidden them.
    static void RevealForClick();
    static void ConcealAfterClick();

private:
    void HideTaskbars();
    void Watch();
    void Save() const;
    static void CALLBACK OnShow(HWINEVENTHOOK, DWORD, HWND, LONG, LONG, DWORD, DWORD);

    std::atomic_bool taskbarHidden_ = false; // also cleared by a crashing worker
    std::atomic_bool autoHideOurs_ = false; // Atrium switched the taskbar to auto-hide
    bool bannersHidden_ = false; // the setting asks for no system banners
    bool quietReady_ = false;    // Do Not Disturb is alarms-only (ours or already)
    bool quietOurs_ = false;     // Atrium changed the profile; restore quietBefore_
    std::wstring quietBefore_;   // the profile Atrium changed it from
    bool autoPlayBannerOurs_ = false; // Atrium turned the AutoPlay banners off; restore them
    bool autoPlayBannerReady_ = false; // AutoPlay banners are off (ours or already)
    bool exiting_ = false;
    static std::atomic_bool suppressHide_; // a tray-icon click has the taskbars shown for a moment
    HWINEVENTHOOK hook_ = nullptr;
    DWORD hookedPid_ = 0;
};

// Do Not Disturb: the user's selected profile, e.g.
// "Microsoft.QuietHoursProfile.Unrestricted"; empty if the service is unavailable.
std::wstring QuietProfile();
bool SetQuietProfile(const wchar_t* profile);

} // namespace atrium
