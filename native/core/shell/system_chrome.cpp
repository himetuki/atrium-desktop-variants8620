#include "shell/system_chrome.h"

#include "log.h"
#include "settings.h"

#include <dwmapi.h>
#include <shellapi.h>
#include <objbase.h>
#include <winreg.h>

#include <thread>
#include <vector>
#include <mutex>

namespace atrium {

namespace {

// The shell's Do Not Disturb service (undocumented; the same one the Windows
// notification centre's button uses). After IUnknown: the selected profile as
// a CoTaskMemAlloc'd string, and setting it.
constexpr CLSID kQuietHoursSettings = {0xF53321FA, 0x34F8, 0x4B7F, {0xB9, 0xA3, 0x36, 0x18, 0x77, 0xCB, 0x94, 0xCF}};
constexpr IID kIQuietHoursSettings = {0x6BFF4732, 0x81EC, 0x4FFB, {0xAE, 0x67, 0xB6, 0xC1, 0xBC, 0x29, 0x63, 0x1F}};
struct IQuietHoursSettings : IUnknown {
    virtual HRESULT STDMETHODCALLTYPE get_UserSelectedProfile(LPWSTR* profile) = 0;
    virtual HRESULT STDMETHODCALLTYPE put_UserSelectedProfile(LPCWSTR profile) = 0;
};

SystemChrome* g_chrome = nullptr;

std::wstring StatePath()
{
    std::wstring p = SettingsPath();
    return p.substr(0, p.find_last_of(L'\\')) + L"\\system.json";
}

std::vector<HWND> Taskbars()
{
    std::vector<HWND> out;
    if (HWND main = FindWindowW(L"Shell_TrayWnd", nullptr))
        out.push_back(main);
    for (HWND w = nullptr; (w = FindWindowExW(nullptr, w, L"Shell_SecondaryTrayWnd", nullptr)) != nullptr;)
        out.push_back(w);
    return out;
}

bool IsTaskbar(HWND w)
{
    wchar_t cls[32] = {};
    GetClassNameW(w, cls, 32);
    return wcscmp(cls, L"Shell_TrayWnd") == 0 || wcscmp(cls, L"Shell_SecondaryTrayWnd") == 0;
}

bool AutoHide()
{
    APPBARDATA abd{sizeof(abd)};
    return (SHAppBarMessage(ABM_GETSTATE, &abd) & ABS_AUTOHIDE) != 0;
}

void SetAutoHide(bool on)
{
    APPBARDATA abd{sizeof(abd)};
    abd.hWnd = FindWindowW(L"Shell_TrayWnd", nullptr);
    abd.lParam = on ? ABS_AUTOHIDE : ABS_ALWAYSONTOP;
    SHAppBarMessage(ABM_SETSTATE, &abd);
}

// Windows maximized while the taskbar still held its strip of the work area keep that gap when the
// area grows: not every app lays itself out again. Each one is stretched to its work area, its
// invisible borders outside it as Windows places a maximized window. A moment later: the shell
// moves the work area after the call returns.
void RefitMaximizedLater()
{
    std::thread([] {
        Sleep(500);
        EnumWindows([](HWND w, LPARAM) -> BOOL {
            MONITORINFO mi{sizeof(mi)};
            RECT r{}, f{};
            if (!IsWindowVisible(w) || !IsZoomed(w) || !GetMonitorInfoW(MonitorFromWindow(w, MONITOR_DEFAULTTONEAREST), &mi) ||
                !GetWindowRect(w, &r) || FAILED(DwmGetWindowAttribute(w, DWMWA_EXTENDED_FRAME_BOUNDS, &f, sizeof(f))))
                return TRUE;
            const RECT& a = mi.rcWork;
            if (f.left <= a.left + 1 && f.top <= a.top + 1 && f.right >= a.right - 1 && f.bottom >= a.bottom - 1)
                return TRUE;
            Log(L"chrome: a maximized window stopped short of the work area (%ld,%ld,%ld,%ld); fitted", f.left, f.top, f.right, f.bottom);
            SetWindowPos(w, nullptr, r.left + a.left - f.left, r.top + a.top - f.top, (r.right + a.right - f.right) - (r.left + a.left - f.left),
                         (r.bottom + a.bottom - f.bottom) - (r.top + a.top - f.top), SWP_NOZORDER | SWP_NOACTIVATE | SWP_ASYNCWINDOWPOS);
            return TRUE;
        }, 0);
    }).detach();
}

void ShowTaskbars()
{
    // Also used by the exception filter: enumerate without allocating.
    if (HWND w = FindWindowW(L"Shell_TrayWnd", nullptr))
        ShowWindow(w, SW_SHOWNA);
    for (HWND w = nullptr; (w = FindWindowExW(nullptr, w, L"Shell_SecondaryTrayWnd", nullptr)) != nullptr;)
        ShowWindow(w, SW_SHOWNA);
}

// AutoPlay's "a drive was inserted" toast is one of the system notifications
// Windows still shows in "alarms only" (that is why the USB banner survived Do
// Not Disturb). Its per-app "show banners" switch still applies: with the banner
// off the toast stays in the notification centre, where Atrium's listener reads
// it for a card, but Windows shows no banner.
constexpr wchar_t kAutoPlayKey[] =
    L"Software\\Microsoft\\Windows\\CurrentVersion\\Notifications\\Settings\\Windows.SystemToast.AutoPlay";
constexpr wchar_t kShowBanner[] = L"ShowBanner";

bool AutoPlayBannerShown()
{
    HKEY key = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kAutoPlayKey, 0, KEY_QUERY_VALUE, &key) != ERROR_SUCCESS)
        return true; // no per-app override: Windows shows the banner
    DWORD value = 1, size = sizeof(value), type = 0;
    const LSTATUS status = RegQueryValueExW(key, kShowBanner, nullptr, &type, reinterpret_cast<BYTE*>(&value), &size);
    RegCloseKey(key);
    return status != ERROR_SUCCESS || type != REG_DWORD || value != 0;
}

bool SetAutoPlayBanner(bool shown)
{
    HKEY key = nullptr;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, kAutoPlayKey, 0, nullptr, 0, KEY_SET_VALUE, nullptr, &key, nullptr) != ERROR_SUCCESS)
        return false;
    LSTATUS status = ERROR_SUCCESS;
    if (shown) {
        // Absent is Windows' default; removing the override restores the banner.
        status = RegDeleteValueW(key, kShowBanner);
        if (status == ERROR_FILE_NOT_FOUND)
            status = ERROR_SUCCESS;
    } else {
        const DWORD value = 0;
        status = RegSetValueExW(key, kShowBanner, 0, REG_DWORD, reinterpret_cast<const BYTE*>(&value), sizeof(value));
    }
    RegCloseKey(key);
    return status == ERROR_SUCCESS;
}

} // namespace

std::wstring QuietProfile()
{
    IQuietHoursSettings* qh = nullptr;
    if (FAILED(CoCreateInstance(kQuietHoursSettings, nullptr, CLSCTX_ALL, kIQuietHoursSettings, reinterpret_cast<void**>(&qh))) || !qh)
        return {};
    LPWSTR p = nullptr;
    std::wstring out;
    if (SUCCEEDED(qh->get_UserSelectedProfile(&p)) && p)
        out = p;
    CoTaskMemFree(p);
    qh->Release();
    return out;
}

bool SetQuietProfile(const wchar_t* profile)
{
    IQuietHoursSettings* qh = nullptr;
    if (FAILED(CoCreateInstance(kQuietHoursSettings, nullptr, CLSCTX_ALL, kIQuietHoursSettings, reinterpret_cast<void**>(&qh))) || !qh)
        return false;
    bool ok = SUCCEEDED(qh->put_UserSelectedProfile(profile));
    qh->Release();
    return ok && QuietProfile() == profile;
}

SystemChrome::SystemChrome()
{
    g_chrome = this;
    // Left over from a run that did not end cleanly: put it back first (the
    // settings then hide again what they ask for).
    UndoLeftover();
}

bool SystemChrome::UndoLeftover()
{
    std::wstring left = ReadTextFile(StatePath());
    if (left.empty())
        return false;
    Log(L"chrome: undoing the last run's changes: %s", left.c_str());
    if (left.find(L"\"autoHide\":true") != std::wstring::npos)
        SetAutoHide(false);
    ShowTaskbars();
    if (left.find(L"\"quiet\":true") != std::wstring::npos && QuietProfile() == kQuietProfile) {
        // The profile Atrium changed it from. Files from before this field have
        // no record and are restored to unrestricted, as they were then.
        std::wstring restore = kQuietProfileOff;
        const std::wstring key = L"\"quietRestore\":\"";
        const size_t at = left.find(key);
        if (at != std::wstring::npos) {
            const size_t begin = at + key.size();
            const size_t end = left.find(L'"', begin);
            if (end != std::wstring::npos && end > begin)
                restore.assign(left, begin, end - begin);
        }
        if (restore.empty() || restore == kQuietProfile)
            restore = kQuietProfileOff;
        SetQuietProfile(restore.c_str());
        Log(L"chrome: Do Not Disturb restored (%s)", restore.c_str());
    }
    if (left.find(L"\"autoPlayBanner\":true") != std::wstring::npos) {
        if (SetAutoPlayBanner(true))
            Log(L"chrome: AutoPlay banners restored");
        else
            Log(L"chrome: could not restore AutoPlay banners");
    }
    DeleteFileW(StatePath().c_str());
    return true;
}

void SystemChrome::ShowTaskbarsNow() { ShowTaskbars(); }

// A real mouse click on a tray icon (its menu, its toggle) needs the taskbar
// visible for the moment of the click. The shell's show-hook must stand still
// in between; the bars go back only when Atrium had hidden them. The tray's
// worker thread calls these while the main thread may be in the hook or in
// OnShow, so every access to the shared state goes through one mutex.
std::atomic_bool SystemChrome::suppressHide_ = false;
static std::mutex clickMutex_;

void SystemChrome::RevealForClick()
{
    std::lock_guard<std::mutex> guard(clickMutex_);
    suppressHide_ = true;
    ShowTaskbars();
}

void SystemChrome::ConcealAfterClick()
{
    std::lock_guard<std::mutex> guard(clickMutex_);
    if (g_chrome && g_chrome->taskbarHidden_)
        for (HWND w : Taskbars())
            ShowWindow(w, SW_HIDE);
    suppressHide_ = false;
}

SystemChrome::~SystemChrome()
{
    RestoreForExit();
    g_chrome = nullptr;
}

void SystemChrome::RestoreForExit()
{
    if (exiting_) return;
    exiting_ = true;
    SetTaskbarHidden(false);
    SetBannersHidden(false);
}

void SystemChrome::SetTaskbarHidden(bool hidden)
{
    if (hidden && exiting_) return;
    if (hidden == taskbarHidden_)
        return;
    taskbarHidden_ = hidden;
    if (hidden) {
        // Auto-hide first: a hidden taskbar that is not auto-hide would still
        // keep its strip of the work area.
        if (!AutoHide()) {
            SetAutoHide(true);
            autoHideOurs_ = true;
            RefitMaximizedLater();
        }
        Save();
        HideTaskbars();
        Watch();
    } else {
        if (hook_)
            UnhookWinEvent(hook_);
        hook_ = nullptr;
        hookedPid_ = 0;
        ShowTaskbars();
        if (autoHideOurs_)
            SetAutoHide(false);
        autoHideOurs_ = false;
        Save();
    }
    Log(L"chrome: system taskbar %s", hidden ? L"hidden" : L"shown");
}

void SystemChrome::SetBannersHidden(bool hidden)
{
    if (hidden && exiting_) return;
    // The setting is idempotent. A failed attempt (the service was unavailable)
    // or a setting the user changed since is retried on the next apply; turning
    // it off retries a restore that failed.
    if (hidden == bannersHidden_) {
        if (hidden ? (quietReady_ && autoPlayBannerReady_) : (!quietOurs_ && !autoPlayBannerOurs_))
            return;
    }
    bannersHidden_ = hidden;
    const std::wstring now = QuietProfile();
    const QuietPlan plan = PlanQuiet(now, hidden, quietOurs_, quietBefore_);
    if (plan.action == QuietAction::hide) {
        if (SetQuietProfile(plan.profile.c_str())) {
            quietBefore_ = now;
            quietOurs_ = true;
            quietReady_ = true;
            // "Priority only" and "unrestricted" both let banners through; only
            // alarms-only hides the system's own toasts as well as apps'.
            Log(L"chrome: Do Not Disturb on (alarms only; was %s)", now.c_str());
        } else {
            quietReady_ = false;
            Log(L"chrome: Do Not Disturb could not be turned on (was %s)", now.c_str());
        }
    } else if (plan.action == QuietAction::restore) {
        quietReady_ = false;
        if (now == kQuietProfile) {
            if (SetQuietProfile(plan.profile.c_str()))
                Log(L"chrome: Do Not Disturb restored (%s)", plan.profile.c_str());
            else
                Log(L"chrome: Do Not Disturb could not be restored (%s)", plan.profile.c_str());
        } else if (!now.empty()) {
            // The user chose another profile while it was Atrium's: leave theirs.
            Log(L"chrome: Do Not Disturb changed by the user (%s); left as it is", now.c_str());
        }
        quietOurs_ = false;
        quietBefore_.clear();
    } else if (hidden) {
        if (now.empty()) {
            quietReady_ = false;
            Log(L"chrome: Do Not Disturb is not available; banners stay");
        } else {
            quietReady_ = true;
            Log(L"chrome: Do Not Disturb already hides banners (alarms only); left as it is");
        }
    } else {
        quietReady_ = false;
        Log(L"chrome: Do Not Disturb off (banners shown)");
    }

    // AutoPlay's toast goes through "alarms only", so its own banner switch is
    // what keeps the USB notification quiet.
    if (hidden) {
        if (!autoPlayBannerReady_) {
            if (!AutoPlayBannerShown()) {
                autoPlayBannerReady_ = true;
                Log(L"chrome: AutoPlay banners already off; left as it is");
            } else if (SetAutoPlayBanner(false)) {
                autoPlayBannerOurs_ = true;
                autoPlayBannerReady_ = true;
                Log(L"chrome: AutoPlay banners off (the drive toast bypasses Do Not Disturb)");
            } else {
                Log(L"chrome: could not turn off AutoPlay banners");
            }
        }
    } else {
        autoPlayBannerReady_ = false;
        if (autoPlayBannerOurs_) {
            if (SetAutoPlayBanner(true)) {
                autoPlayBannerOurs_ = false;
                Log(L"chrome: AutoPlay banners restored");
            } else {
                // Keep the ownership so system.json (and the guard) retries it.
                Log(L"chrome: could not restore AutoPlay banners");
            }
        }
    }
    Save();
}

void SystemChrome::Enforce()
{
    if (exiting_) return;
    if (taskbarHidden_) {
        // Hidden but not auto-hide, the taskbar keeps its strip of the work area and maximized
        // windows stop its height short of the bottom. Auto-hide can be lost after it was set:
        // set before Explorer's taskbar was ready at sign-in, Explorer restarted, another program.
        if (FindWindowW(L"Shell_TrayWnd", nullptr) && !AutoHide()) {
            SetAutoHide(true);
            autoHideOurs_ = true;
            Save();
            Log(L"chrome: the taskbar was no longer auto-hide; set again");
            RefitMaximizedLater();
        }
        HideTaskbars();
        Watch(); // Explorer may have been restarted
    }
    if (quietReady_) {
        const std::wstring now = QuietProfile();
        if (!now.empty() && now != kQuietProfile) {
            // The user turned it off (or chose another mode): theirs from now on.
            Log(L"chrome: Do Not Disturb changed by the user (%s); no longer hidden by Atrium", now.c_str());
            quietReady_ = false;
            quietOurs_ = false;
            quietBefore_.clear();
            Save();
        }
    }
    if (autoPlayBannerOurs_ && AutoPlayBannerShown()) {
        // The user turned AutoPlay banners back on: theirs from now on.
        Log(L"chrome: AutoPlay banners changed by the user; no longer hidden by Atrium");
        autoPlayBannerOurs_ = false;
        autoPlayBannerReady_ = false;
        Save();
    }
}

void SystemChrome::EmergencyRestore()
{
    if (!g_chrome || !g_chrome->taskbarHidden_.exchange(false))
        return;
    // Stop OnShow/Enforce hiding Explorer again before making it visible.
    // The exception can be on a worker, so these flags are atomic. Do not
    // log or save here; system.json still restores Do Not Disturb next start.
    if (g_chrome->autoHideOurs_.exchange(false))
        SetAutoHide(false);
    ShowTaskbars();
}

void SystemChrome::HideTaskbars()
{
    for (HWND w : Taskbars())
        if (IsWindowVisible(w))
            ShowWindow(w, SW_HIDE);
}

void SystemChrome::Watch()
{
    // Explorer shows its taskbar again by itself now and then: hide it the
    // moment it does.
    DWORD pid = 0;
    GetWindowThreadProcessId(FindWindowW(L"Shell_TrayWnd", nullptr), &pid);
    if (!pid || pid == hookedPid_)
        return;
    if (hook_)
        UnhookWinEvent(hook_);
    hook_ = SetWinEventHook(EVENT_OBJECT_SHOW, EVENT_OBJECT_SHOW, nullptr, OnShow, pid, 0, WINEVENT_OUTOFCONTEXT);
    hookedPid_ = hook_ ? pid : 0;
}

void CALLBACK SystemChrome::OnShow(HWINEVENTHOOK, DWORD, HWND w, LONG obj, LONG, DWORD, DWORD)
{
    if (obj == OBJID_WINDOW && g_chrome && g_chrome->taskbarHidden_ && !suppressHide_ && w && IsTaskbar(w)) {
        ShowWindow(w, SW_HIDE);
        Log(L"chrome: Explorer showed its taskbar; hidden again");
    }
}

void SystemChrome::Save() const
{
    if (!taskbarHidden_ && !quietOurs_ && !autoPlayBannerOurs_) {
        DeleteFileW(StatePath().c_str());
        return;
    }
    std::wstring json = L"{\"taskbarHidden\":";
    json += taskbarHidden_ ? L"true" : L"false";
    json += L",\"autoHide\":";
    json += autoHideOurs_ ? L"true" : L"false";
    json += L",\"quiet\":";
    json += quietOurs_ ? L"true" : L"false";
    if (quietOurs_ && !quietBefore_.empty()) {
        // The profile to put back after a crash or a killed process. Profile names
        // are plain ASCII identifiers, so they need no JSON escaping.
        json += L",\"quietRestore\":\"";
        json += quietBefore_;
        json += L"\"";
    }
    if (autoPlayBannerOurs_)
        json += L",\"autoPlayBanner\":true";
    json += L"}";
    WriteTextFile(StatePath(), json);
}

} // namespace atrium
