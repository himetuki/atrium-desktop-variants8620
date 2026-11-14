// User-adjustable appearance and layout. One flat JSON object, shared with
// Atrium.Settings (settings/Atrium.Settings/BarSettings.cs) and the pipe
// protocol (docs/SETTINGS-PROTOCOL.md). Lengths are in DIP.
#pragma once

#include "shell/task_model.h"
#include "ui/stage_excluded.h"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace atrium {

struct Settings {
    static constexpr int kVersion = 2; // 2: Windows notifications on by default (v1 files are migrated)

    // Material (presets change only these). Liquid follows macOS Liquid Glass:
    // nearly clear centre, strong lensing over a wide rim, visible dispersion.
    std::wstring preset = L"liquid"; // liquid | frosted | custom (informational)
    double transparency = 0.95;      // 0 = fully tinted, 1 = clear glass
    double blur = 0.50;              // 0 = sharp backdrop, 1 = fully frosted
    double refraction = 30;          // max sample displacement at the rim
    double edge = 24;                // width of the refracting rim
    double dispersion = 0.30;        // relative channel spread of the refraction
    double highlight = 0.80;
    double shadow = 0.30;
    uint32_t tint = 0xFFFFFF;        // 0xRRGGBB

    // Layout and content.
    double cornerRadius = 30;        // clamped to half the bar height when drawn
    double width = 1200;             // maximum width when autoWidth
    double height = 60;
    double margin = 10;              // gap above the bottom of the work area
    double iconSize = 30;
    double magnify = 1.7;            // dock magnification under the pointer: icon scale, 1 = off
    bool startTray = true;           // the tray's button in the start menu's capsule
    bool startStatus = true;         // the network, volume and battery buttons there
    // The start menu's: small | medium | large | xlarge; empty: as start.json has it
    // (older files). `startIcon`: how large its icons and folders are drawn, four
    // steps of its own.
    std::wstring startSize;
    std::wstring startIcon;
    bool hoverRise = false;           // glass rises with the pointer's magnification wave
    // A pointer resting just above the bar is answered ahead of time: the glass rises
    // to it (with hoverRise) and the icons magnify — each on its own switch.
    bool riseNear = true;
    bool magnifyNear = false; // off unless chosen: the icons would grow under a pointer working just above the bar
    double nearEnter = 40, nearLeave = 48; // DIP above the glass where that starts, and where it lets go
    bool autoWidth = true;           // shrink to the content
    bool showClock = true;
    bool showClockSeconds = false;    // seconds after the minutes, in the system's long time format
    bool showTray = true;
    bool showStatus = true;
    bool showNotifications = true;
    bool showRunning = true;
    // The interface language of the bar, its popups, the widgets and the settings app.
    // auto follows Windows' UI language (Chinese -> zh, anything else -> en).
    std::wstring language = L"auto"; // auto | zh | en
    std::wstring textColor = L"auto"; // auto | light | dark
    // When the bar steps out of the way: never | fullscreen (a maximized or full-screen app is in
    // front) | always (it waits below the screen's bottom edge). The pointer there brings it up.
    std::wstring autoHide = L"fullscreen";
    std::wstring statusStyle = L"ring"; // the status entry: ring (a ring round the Wi-Fi fan) | icons (network, volume, battery)
    // One Dock, summoned at another display's bottom edge, as on macOS.
    std::wstring dockDisplayMode = L"primary"; // primary (as before several displays were known) | follow | fixed | all
    double dockDisplayDelay = 450;   // milliseconds at another display's bottom edge (100..2000)
    std::wstring dockDisplay;        // fixed display's device-interface ID
    std::wstring desktopDisplay;     // widgets and Stage Manager; empty follows the system primary
    std::wstring taskbarApps = L"all"; // all | primaryAndWindow | window
    bool taskbarRightPrimaryOnly = false; // hide the right-side group on non-primary displays
    // An icon's right-click menu lists the app's own jump list entries (recent
    // places, tasks) and the system's ("run as administrator", "properties").
    bool windowsContextMenu = true;
    // Notifications (Atrium's own cards above the bell).
    double notifyTimeout = 6;        // seconds a popup stays (3..30)
    std::wstring notifyPlacement = L"dock"; // dock (default) | topRight (system primary)
    bool notifyHideBody = false;     // privacy: no message text; cards excluded from screen capture
    bool notifyListener = true;      // Windows notifications as Atrium cards (UserNotificationListener)
    bool notifyFeishu = true;        // Feishu messages as Atrium cards (its own popup, read through UI Automation)
    bool notifyWeChat = true;        // WeChat messages as Atrium cards (its chat list, read through UI Automation)
    // Three independent modules: the bar (with its popups and Atrium's notification cards), the
    // desktop widgets and Stage Manager. Any of them may run alone.
    bool bar = true;                 // the glass bar; off: Windows' own taskbar and banners stay in charge
    bool widgets = true;             // desktop widgets (their layout is widgets.json)
    bool widgetsOnRight = false;     // mirror the logical grid without rewriting widgets.json
    std::wstring widgetBackdrop = L"desktop"; // the widgets' glass: desktop (captured live, D38) | wallpaper (the files, least work)
    // Stage Manager (ui/stage_manager.h): the app in front on the stage, the others in a strip at the side.
    bool stageManager = false;
    bool stageOnRight = false;       // the strip's side; left by default (the widgets take the other side)
    bool stageAutoHide = false;      // the strip only comes out while the pointer rests on its edge of the screen
    bool stageMinimize = false;      // a window minimized on the stage goes to the strip
    // Apps Stage Manager leaves alone: their windows are never put away. Defaults cover the
    // screenshot tools whose own overlay/control bar must stay on screen while recording.
    std::vector<std::wstring> stageExcluded = DefaultStageExcluded();
    // The widgets' material: glass (the bar's, its ink adapting) | dusk (the glass dimmed, white ink;
    // macOS's dimmed desktop widgets) | color (their own opaque faces, as macOS's full-colour widgets).
    std::wstring widgetMaterial = L"glass";
    // Windows' own shell (shell/system_chrome.h): put back when turned off or on exit.
    bool hideTaskbar = false;        // the system taskbar is hidden while Atrium runs
    bool hideBanners = false;        // Do Not Disturb: no system banners, the notifications still become cards
    bool winKeyStart = false;        // the Windows key alone opens Atrium's start menu instead of Windows'
    bool winNumbers = false;         // Win + 1..9, 0 open the bar's apps
    bool remoteVisible = false;      // remote-control apps see Atrium; the glass shows the wallpaper
    bool startOnLogin = true;        // independent of starting/stopping the current session
    // Pinned apps; nullopt = never saved yet (first run imports the Windows pins).
    std::optional<std::vector<PinnedApp>> pins;

    // Clamps every value into its valid range (input from disk or the pipe).
    void Clamp();

    std::wstring ToJson() const;
    // Missing keys keep the values of `base`; malformed JSON returns false.
    static bool FromJson(const std::wstring& json, const Settings& base, Settings& out);
};

// %LOCALAPPDATA%\Atrium\settings.json
std::wstring SettingsPath();
// UTF-8 text files (a BOM is skipped); the write replaces the file at once.
std::wstring ReadTextFile(const std::wstring& path);
bool WriteTextFile(const std::wstring& path, const std::wstring& text);
Settings LoadSettings();
// True means the file was saved. A failed startup change is reported separately;
// callers must accept the saved snapshot even when warning is nonempty. A failed
// save says why in `error` (the path, the system's reason).
bool SaveSettings(const Settings& s, std::wstring* warning = nullptr, std::wstring* error = nullptr);
// The process runs below medium integrity (an executable with a Low label):
// the profile and HKCU are closed to it, and its keys cannot reach other windows.
bool LowIntegrity();
bool IsDevelopmentBuild();
// The current user's Windows logon entry, pointing at this core executable.
bool SetStartOnLogin(bool enabled);
// The Run entry turned off in Task Manager's startup apps.
bool StartupTurnedOff();

} // namespace atrium
