#include "settings.h"

#include "localization.h"
#include "log.h"
#include "shell/logon_task.h"

#include <windows.h>
#include <shlobj.h>
#include <shellapi.h>

#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Data.Json.h>
#include <winrt/Windows.Foundation.h>

#include <algorithm>
#include <cstdio>
#include <cwctype>
#include <fstream>
#include <sstream>
#include <vector>

namespace json = winrt::Windows::Data::Json;

namespace atrium {

void Settings::Clamp()
{
    auto c = [](double& v, double lo, double hi, double fallback) {
        v = v != v ? fallback : std::clamp(v, lo, hi); // NaN -> fallback
    };
    Settings d;
    c(transparency, 0, 1, d.transparency);
    c(cornerRadius, 0, 200, d.cornerRadius);
    c(blur, 0, 1, d.blur);
    c(refraction, 0, 60, d.refraction);
    c(edge, 1, 80, d.edge);
    c(dispersion, 0, 0.5, d.dispersion);
    c(highlight, 0, 1, d.highlight);
    c(shadow, 0, 1, d.shadow);
    c(width, 200, 4000, d.width);
    c(height, 32, 160, d.height);
    c(margin, 0, 400, d.margin);
    c(iconSize, 16, 64, d.iconSize);
    c(magnify, 1, 2, d.magnify);
    c(notifyTimeout, 3, 30, d.notifyTimeout);
    c(nearEnter, 2, 160, d.nearEnter);
    c(nearLeave, 2, 200, d.nearLeave);
    c(dockDisplayDelay, 100, 2000, d.dockDisplayDelay);
    tint &= 0xFFFFFF;
    bool languageValid = !language.empty() && language.size() <= 32;
    for (wchar_t ch : language)
        if (!iswalnum(ch) && ch != L'-')
            languageValid = false;
    if (!languageValid)
        language = L"auto";
    else if (language == L"zh")
        language = L"zh-Hans";
    if (textColor != L"light" && textColor != L"dark")
        textColor = L"auto";
    if (autoHide != L"never" && autoHide != L"always")
        autoHide = L"fullscreen";
    if (statusStyle != L"icons")
        statusStyle = L"ring";
    if (startSize != L"small" && startSize != L"medium" && startSize != L"large" && startSize != L"xlarge")
        startSize.clear();
    if (startIcon != L"small" && startIcon != L"medium" && startIcon != L"large" && startIcon != L"xlarge")
        startIcon.clear();
    if (dockDisplayMode != L"follow" && dockDisplayMode != L"fixed" && dockDisplayMode != L"all")
        dockDisplayMode = L"primary";
    if (taskbarApps != L"primaryAndWindow" && taskbarApps != L"window")
        taskbarApps = L"all";
    if (notifyPlacement != L"topRight")
        notifyPlacement = L"dock";
    if (pins && pins->size() > 64)
        pins->resize(64);
    if (preset != L"liquid" && preset != L"frosted")
        preset = L"custom";
}

std::wstring Settings::ToJson() const
{
    json::JsonObject o;
    auto num = [&](const wchar_t* k, double v) { o.SetNamedValue(k, json::JsonValue::CreateNumberValue(v)); };
    wchar_t tintText[8];
    swprintf_s(tintText, L"#%06X", tint);
    num(L"version", kVersion);
    o.SetNamedValue(L"preset", json::JsonValue::CreateStringValue(preset));
    num(L"transparency", transparency);
    num(L"cornerRadius", cornerRadius);
    num(L"blur", blur);
    num(L"refraction", refraction);
    num(L"edge", edge);
    num(L"dispersion", dispersion);
    num(L"highlight", highlight);
    num(L"shadow", shadow);
    o.SetNamedValue(L"tint", json::JsonValue::CreateStringValue(tintText));
    num(L"width", width);
    num(L"height", height);
    num(L"margin", margin);
    num(L"iconSize", iconSize);
    num(L"magnify", magnify);
    o.SetNamedValue(L"hoverRise", json::JsonValue::CreateBooleanValue(hoverRise));
    o.SetNamedValue(L"riseNear", json::JsonValue::CreateBooleanValue(riseNear));
    o.SetNamedValue(L"magnifyNear", json::JsonValue::CreateBooleanValue(magnifyNear));
    o.SetNamedValue(L"startTray", json::JsonValue::CreateBooleanValue(startTray));
    o.SetNamedValue(L"startStatus", json::JsonValue::CreateBooleanValue(startStatus));
    if (!startSize.empty())
        o.SetNamedValue(L"startSize", json::JsonValue::CreateStringValue(startSize));
    if (!startIcon.empty())
        o.SetNamedValue(L"startIcon", json::JsonValue::CreateStringValue(startIcon));
    o.SetNamedValue(L"showClock", json::JsonValue::CreateBooleanValue(showClock));
    o.SetNamedValue(L"showClockSeconds", json::JsonValue::CreateBooleanValue(showClockSeconds));
    o.SetNamedValue(L"showTray", json::JsonValue::CreateBooleanValue(showTray));
    o.SetNamedValue(L"showStatus", json::JsonValue::CreateBooleanValue(showStatus));
    o.SetNamedValue(L"showNotifications", json::JsonValue::CreateBooleanValue(showNotifications));
    o.SetNamedValue(L"autoWidth", json::JsonValue::CreateBooleanValue(autoWidth));
    o.SetNamedValue(L"showRunning", json::JsonValue::CreateBooleanValue(showRunning));
    o.SetNamedValue(L"language", json::JsonValue::CreateStringValue(language));
    o.SetNamedValue(L"textColor", json::JsonValue::CreateStringValue(textColor));
    o.SetNamedValue(L"autoHide", json::JsonValue::CreateStringValue(autoHide));
    o.SetNamedValue(L"statusStyle", json::JsonValue::CreateStringValue(statusStyle));
    o.SetNamedValue(L"dockDisplayMode", json::JsonValue::CreateStringValue(dockDisplayMode));
    num(L"dockDisplayDelay", dockDisplayDelay);
    o.SetNamedValue(L"dockDisplay", json::JsonValue::CreateStringValue(dockDisplay));
    o.SetNamedValue(L"desktopDisplay", json::JsonValue::CreateStringValue(desktopDisplay));
    o.SetNamedValue(L"taskbarApps", json::JsonValue::CreateStringValue(taskbarApps));
    o.SetNamedValue(L"taskbarRightPrimaryOnly", json::JsonValue::CreateBooleanValue(taskbarRightPrimaryOnly));
    o.SetNamedValue(L"windowsContextMenu", json::JsonValue::CreateBooleanValue(windowsContextMenu));
    num(L"notifyTimeout", notifyTimeout);
    num(L"nearEnter", nearEnter);
    num(L"nearLeave", nearLeave);
    o.SetNamedValue(L"notifyPlacement", json::JsonValue::CreateStringValue(notifyPlacement));
    o.SetNamedValue(L"notifyHideBody", json::JsonValue::CreateBooleanValue(notifyHideBody));
    o.SetNamedValue(L"notifyListener", json::JsonValue::CreateBooleanValue(notifyListener));
    o.SetNamedValue(L"notifyWeChat", json::JsonValue::CreateBooleanValue(notifyWeChat));
    o.SetNamedValue(L"notifyFeishu", json::JsonValue::CreateBooleanValue(notifyFeishu));
    o.SetNamedValue(L"bar", json::JsonValue::CreateBooleanValue(bar));
    o.SetNamedValue(L"widgets", json::JsonValue::CreateBooleanValue(widgets));
    o.SetNamedValue(L"widgetsOnRight", json::JsonValue::CreateBooleanValue(widgetsOnRight));
    o.SetNamedValue(L"widgetBackdrop", json::JsonValue::CreateStringValue(widgetBackdrop));
    o.SetNamedValue(L"widgetMaterial", json::JsonValue::CreateStringValue(widgetMaterial));
    o.SetNamedValue(L"stageManager", json::JsonValue::CreateBooleanValue(stageManager));
    o.SetNamedValue(L"stageOnRight", json::JsonValue::CreateBooleanValue(stageOnRight));
    o.SetNamedValue(L"stageAutoHide", json::JsonValue::CreateBooleanValue(stageAutoHide));
    o.SetNamedValue(L"stageMinimize", json::JsonValue::CreateBooleanValue(stageMinimize));
    {
        json::JsonArray a;
        for (const std::wstring& app : stageExcluded)
            a.Append(json::JsonValue::CreateStringValue(app));
        o.SetNamedValue(L"stageExcluded", a);
    }
    o.SetNamedValue(L"hideTaskbar", json::JsonValue::CreateBooleanValue(hideTaskbar));
    o.SetNamedValue(L"hideBanners", json::JsonValue::CreateBooleanValue(hideBanners));
    o.SetNamedValue(L"remoteVisible", json::JsonValue::CreateBooleanValue(remoteVisible));
    o.SetNamedValue(L"winKeyStart", json::JsonValue::CreateBooleanValue(winKeyStart));
    o.SetNamedValue(L"winNumbers", json::JsonValue::CreateBooleanValue(winNumbers));
    o.SetNamedValue(L"startOnLogin", json::JsonValue::CreateBooleanValue(startOnLogin));
    if (pins) {
        json::JsonArray a;
        for (const auto& p : *pins) {
            json::JsonObject e;
            e.SetNamedValue(L"name", json::JsonValue::CreateStringValue(p.name));
            e.SetNamedValue(L"path", json::JsonValue::CreateStringValue(p.path));
            e.SetNamedValue(L"args", json::JsonValue::CreateStringValue(p.args));
            e.SetNamedValue(L"aumid", json::JsonValue::CreateStringValue(p.aumid));
            if (p.multipleWindows)
                e.SetNamedValue(L"multipleWindows", json::JsonValue::CreateBooleanValue(*p.multipleWindows));
            a.Append(e);
        }
        o.SetNamedValue(L"pins", a);
    }
    return std::wstring(o.Stringify());
}

bool Settings::FromJson(const std::wstring& text, const Settings& base, Settings& out)
{
    json::JsonObject o{nullptr};
    if (!json::JsonObject::TryParse(text, o))
        return false;
    Settings s = base;
    auto num = [&](const wchar_t* k, double& v) {
        if (o.HasKey(k) && o.GetNamedValue(k).ValueType() == json::JsonValueType::Number)
            v = o.GetNamedNumber(k);
    };
    if (o.HasKey(L"preset") && o.GetNamedValue(L"preset").ValueType() == json::JsonValueType::String)
        s.preset = std::wstring(o.GetNamedString(L"preset"));
    num(L"transparency", s.transparency);
    num(L"cornerRadius", s.cornerRadius);
    num(L"blur", s.blur);
    num(L"refraction", s.refraction);
    num(L"edge", s.edge);
    num(L"dispersion", s.dispersion);
    num(L"highlight", s.highlight);
    num(L"shadow", s.shadow);
    num(L"width", s.width);
    num(L"height", s.height);
    num(L"margin", s.margin);
    if (o.HasKey(L"tint") && o.GetNamedValue(L"tint").ValueType() == json::JsonValueType::String) {
        std::wstring t(o.GetNamedString(L"tint"));
        if (t.size() == 7 && t[0] == L'#' && std::all_of(t.begin() + 1, t.end(), [](wchar_t c) { return iswxdigit(c) != 0; }))
            s.tint = static_cast<uint32_t>(wcstoul(t.c_str() + 1, nullptr, 16));
    }
    num(L"iconSize", s.iconSize);
    num(L"magnify", s.magnify);
    auto flag = [&](const wchar_t* k, bool& v) {
        if (o.HasKey(k) && o.GetNamedValue(k).ValueType() == json::JsonValueType::Boolean)
            v = o.GetNamedBoolean(k);
    };
    flag(L"hoverRise", s.hoverRise);
    flag(L"riseNear", s.riseNear);
    flag(L"magnifyNear", s.magnifyNear);
    flag(L"startTray", s.startTray);
    flag(L"startStatus", s.startStatus);
    if (o.HasKey(L"startSize") && o.GetNamedValue(L"startSize").ValueType() == json::JsonValueType::String)
        s.startSize = std::wstring(o.GetNamedString(L"startSize"));
    if (o.HasKey(L"startIcon") && o.GetNamedValue(L"startIcon").ValueType() == json::JsonValueType::String)
        s.startIcon = std::wstring(o.GetNamedString(L"startIcon"));
    flag(L"showClock", s.showClock);
    flag(L"showClockSeconds", s.showClockSeconds);
    flag(L"showTray", s.showTray);
    flag(L"showStatus", s.showStatus);
    flag(L"showNotifications", s.showNotifications);
    flag(L"autoWidth", s.autoWidth);
    flag(L"showRunning", s.showRunning);
    num(L"notifyTimeout", s.notifyTimeout);
    num(L"nearEnter", s.nearEnter);
    num(L"nearLeave", s.nearLeave);
    flag(L"notifyHideBody", s.notifyHideBody);
    // Version 1 saved the old default (off) along with everything else; from
    // version 2 the notifications are on unless turned off since.
    bool v1 = o.HasKey(L"version") && o.GetNamedValue(L"version").ValueType() == json::JsonValueType::Number && o.GetNamedNumber(L"version") < 2;
    if (!v1)
        flag(L"notifyListener", s.notifyListener);
    flag(L"notifyWeChat", s.notifyWeChat);
    flag(L"notifyFeishu", s.notifyFeishu);
    flag(L"bar", s.bar);
    flag(L"widgets", s.widgets);
    flag(L"widgetsOnRight", s.widgetsOnRight);
    flag(L"stageManager", s.stageManager);
    flag(L"stageOnRight", s.stageOnRight);
    flag(L"stageAutoHide", s.stageAutoHide);
    flag(L"stageMinimize", s.stageMinimize);
    flag(L"hideTaskbar", s.hideTaskbar);
    flag(L"hideBanners", s.hideBanners);
    if (o.HasKey(L"stageExcluded") && o.GetNamedValue(L"stageExcluded").ValueType() == json::JsonValueType::Array) {
        std::vector<std::wstring> apps;
        for (const auto& v : o.GetNamedArray(L"stageExcluded"))
            if (v.ValueType() == json::JsonValueType::String)
                apps.push_back(std::wstring(v.GetString()));
        s.stageExcluded = std::move(apps);
    }
    flag(L"remoteVisible", s.remoteVisible);
    flag(L"winKeyStart", s.winKeyStart);
    flag(L"winNumbers", s.winNumbers);
    flag(L"startOnLogin", s.startOnLogin);
    if (o.HasKey(L"widgetBackdrop") && o.GetNamedValue(L"widgetBackdrop").ValueType() == json::JsonValueType::String)
        s.widgetBackdrop = std::wstring(o.GetNamedString(L"widgetBackdrop")) == L"wallpaper" ? L"wallpaper" : L"desktop";
    if (o.HasKey(L"widgetMaterial") && o.GetNamedValue(L"widgetMaterial").ValueType() == json::JsonValueType::String) {
        const std::wstring m(o.GetNamedString(L"widgetMaterial"));
        s.widgetMaterial = m == L"dusk" || m == L"color" ? m : L"glass";
    }
    if (o.HasKey(L"language") && o.GetNamedValue(L"language").ValueType() == json::JsonValueType::String)
        s.language = std::wstring(o.GetNamedString(L"language"));
    if (o.HasKey(L"textColor") && o.GetNamedValue(L"textColor").ValueType() == json::JsonValueType::String)
        s.textColor = std::wstring(o.GetNamedString(L"textColor"));
    if (o.HasKey(L"autoHide") && o.GetNamedValue(L"autoHide").ValueType() == json::JsonValueType::String)
        s.autoHide = std::wstring(o.GetNamedString(L"autoHide"));
    if (o.HasKey(L"statusStyle") && o.GetNamedValue(L"statusStyle").ValueType() == json::JsonValueType::String)
        s.statusStyle = std::wstring(o.GetNamedString(L"statusStyle"));
    auto display = [&](const wchar_t* key, std::wstring& value) {
        if (o.HasKey(key) && o.GetNamedValue(key).ValueType() == json::JsonValueType::String)
            value = std::wstring(o.GetNamedString(key));
    };
    display(L"dockDisplayMode", s.dockDisplayMode);
    num(L"dockDisplayDelay", s.dockDisplayDelay);
    display(L"dockDisplay", s.dockDisplay);
    display(L"desktopDisplay", s.desktopDisplay);
    display(L"taskbarApps", s.taskbarApps);
    flag(L"taskbarRightPrimaryOnly", s.taskbarRightPrimaryOnly);
    flag(L"windowsContextMenu", s.windowsContextMenu);
    display(L"notifyPlacement", s.notifyPlacement);
    if (o.HasKey(L"pins") && o.GetNamedValue(L"pins").ValueType() == json::JsonValueType::Array) {
        std::vector<PinnedApp> pins;
        for (const auto& v : o.GetNamedArray(L"pins")) {
            if (v.ValueType() != json::JsonValueType::Object)
                continue;
            auto e = v.GetObject();
            auto str = [&](const wchar_t* k) {
                return e.HasKey(k) && e.GetNamedValue(k).ValueType() == json::JsonValueType::String ? std::wstring(e.GetNamedString(k))
                                                                                                    : std::wstring();
            };
            PinnedApp p{str(L"name"), str(L"path"), str(L"args"), str(L"aumid")};
            if (e.HasKey(L"multipleWindows") && e.GetNamedValue(L"multipleWindows").ValueType() == json::JsonValueType::Boolean)
                p.multipleWindows = e.GetNamedBoolean(L"multipleWindows");
            if (!p.path.empty() || !p.aumid.empty())
                pins.push_back(std::move(p));
        }
        s.pins = std::move(pins);
    }
    s.Clamp();
    out = s;
    return true;
}

std::wstring SettingsPath()
{
    PWSTR base = nullptr;
    std::wstring dir;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &base)))
        dir = std::wstring(base) + L"\\Atrium";
    CoTaskMemFree(base);
    CreateDirectoryW(dir.c_str(), nullptr);
    return dir + L"\\settings.json";
}

std::wstring ReadTextFile(const std::wstring& path)
{
    std::ifstream in(path, std::ios::binary);
    if (!in)
        return {};
    std::ostringstream ss;
    ss << in.rdbuf();
    std::string bytes = ss.str();
    if (bytes.size() >= 3 && bytes.compare(0, 3, "\xEF\xBB\xBF") == 0)
        bytes.erase(0, 3); // UTF-8 BOM, e.g. saved by Notepad or Windows PowerShell
    if (bytes.empty())
        return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, bytes.data(), int(bytes.size()), nullptr, 0);
    std::wstring w(size_t(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, bytes.data(), int(bytes.size()), w.data(), n);
    return w;
}

bool WriteTextFile(const std::wstring& path, const std::wstring& text)
{
    std::wstring tmp = path + L".tmp";
    int n = WideCharToMultiByte(CP_UTF8, 0, text.data(), int(text.size()), nullptr, 0, nullptr, nullptr);
    std::string bytes(size_t(n), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.data(), int(text.size()), bytes.data(), n, nullptr, nullptr);
    // Win32 rather than a stream: a failure leaves its reason in GetLastError.
    HANDLE file = CreateFileW(tmp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE)
        return false;
    DWORD written = 0;
    const bool ok = WriteFile(file, bytes.data(), DWORD(bytes.size()), &written, nullptr) && written == bytes.size() && FlushFileBuffers(file);
    const DWORD error = ok ? ERROR_SUCCESS : GetLastError();
    CloseHandle(file);
    if (!ok) {
        DeleteFileW(tmp.c_str());
        SetLastError(error != ERROR_SUCCESS ? error : ERROR_WRITE_FAULT);
        return false;
    }
    return MoveFileExW(tmp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
}

Settings LoadSettings()
{
    Settings s;
    std::wstring text = ReadTextFile(SettingsPath());
    if (text.empty())
        return s;
    if (!Settings::FromJson(text, Settings{}, s))
        Log(L"settings: %s is not valid JSON, using defaults", SettingsPath().c_str());
    return s;
}

bool SetStartOnLogin(bool enabled)
{
    if (IsDevelopmentBuild())
        return true;
    constexpr auto key = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
    wchar_t exe[32768]{};
    DWORD length = GetModuleFileNameW(nullptr, exe, DWORD(std::size(exe)));
    if (!length || length == std::size(exe))
        return false;
    std::wstring command = L"\"" + std::wstring(exe) + L"\"";
    wchar_t current[32771]{};
    DWORD size = sizeof(current);
    LSTATUS read = RegGetValueW(HKEY_CURRENT_USER, key, L"Atrium", RRF_RT_REG_SZ, nullptr, current, &size);
    const bool runEntry = read == ERROR_SUCCESS;
    const std::wstring task = LogonTaskProgram();
    if (!enabled) {
        // Another distribution's entries are not ours to remove.
        if ((read != ERROR_SUCCESS && read != ERROR_FILE_NOT_FOUND && read != ERROR_PATH_NOT_FOUND) ||
            (runEntry && _wcsicmp(command.c_str(), current) != 0) || (!task.empty() && _wcsicmp(task.c_str(), exe) != 0))
            return false;
        if (!task.empty() && !RemoveLogonTask())
            return false;
        if (!runEntry)
            return true;
        LSTATUS result = RegDeleteKeyValueW(HKEY_CURRENT_USER, key, L"Atrium");
        return result == ERROR_SUCCESS || result == ERROR_FILE_NOT_FOUND;
    }
    // The logon task, and no Run entry left to start Atrium a second time (also from
    // earlier versions, which started from the Run key).
    if ((_wcsicmp(task.c_str(), exe) == 0 || RegisterLogonTask(exe)) && (!runEntry || RegDeleteKeyValueW(HKEY_CURRENT_USER, key, L"Atrium") == ERROR_SUCCESS))
        return true;
    if (read == ERROR_SUCCESS && command == current)
        return true;
    return RegSetKeyValueW(HKEY_CURRENT_USER, key, L"Atrium", REG_SZ, command.c_str(), DWORD((command.size() + 1) * sizeof(wchar_t))) == ERROR_SUCCESS;
}

bool StartupTurnedOff()
{
    // The first byte is odd when the switch is off (2 on, 3 off).
    BYTE state[12]{};
    DWORD size = sizeof(state);
    return RegGetValueW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\StartupApproved\\Run", L"Atrium",
                        RRF_RT_REG_BINARY, nullptr, state, &size) == ERROR_SUCCESS &&
           size > 0 && (state[0] & 1);
}

bool IsDevelopmentBuild()
{
#ifdef ATRIUM_DEVELOPMENT
    return true;
#else
    static const bool development = [] {
        int count = 0;
        auto args = CommandLineToArgvW(GetCommandLineW(), &count);
        bool explicitDevelopment = false;
        for (int i = 1; args && i < count; ++i)
            explicitDevelopment |= wcscmp(args[i], L"--development") == 0;
        LocalFree(args);
        wchar_t exe[32768]{};
        GetModuleFileNameW(nullptr, exe, DWORD(std::size(exe)));
        std::wstring path(exe);
        path = path.substr(0, path.find_last_of(L'\\')) + L"\\development.txt";
        return explicitDevelopment || GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES;
    }();
    return development;
#endif
}

bool LowIntegrity()
{
    static const bool low = [] {
        HANDLE token = nullptr;
        if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token))
            return false;
        DWORD size = 0;
        GetTokenInformation(token, TokenIntegrityLevel, nullptr, 0, &size);
        std::vector<BYTE> buffer(size);
        bool below = false;
        if (size && GetTokenInformation(token, TokenIntegrityLevel, buffer.data(), size, &size)) {
            const auto* label = reinterpret_cast<const TOKEN_MANDATORY_LABEL*>(buffer.data());
            const DWORD rid = *GetSidSubAuthority(label->Label.Sid, *GetSidSubAuthorityCount(label->Label.Sid) - 1);
            below = rid < SECURITY_MANDATORY_MEDIUM_RID;
        }
        CloseHandle(token);
        return below;
    }();
    return low;
}

bool SaveSettings(const Settings& s, std::wstring* warning, std::wstring* error)
{
    if (warning) warning->clear();
    if (error) error->clear();
    const bool startupChanged = LoadSettings().startOnLogin != s.startOnLogin;
    const std::wstring path = SettingsPath();
    if (!WriteTextFile(path, s.ToJson())) {
        const DWORD code = GetLastError();
        wchar_t* text = nullptr;
        FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, nullptr, code, 0,
                       reinterpret_cast<wchar_t*>(&text), 0, nullptr);
        std::wstring reason = text ? text : L"";
        LocalFree(text);
        while (!reason.empty() && (reason.back() == L'\n' || reason.back() == L'\r' || reason.back() == L' '))
            reason.pop_back();
        Log(L"settings: could not write %s (%lu)%s", path.c_str(), code, LowIntegrity() ? L", running at low integrity" : L"");
        if (error) {
            *error = TrFormat(L"Could not write %s: %s (error %lu)", path.c_str(), reason.c_str(), code);
            // Below medium integrity (an executable carrying a Low label) nothing in the profile is writable.
            if (LowIntegrity())
                *error += Tr(L"\nAtrium is running at low integrity and cannot write to your profile. Unzip it to an ordinary folder (such as D:\\\\Atrium), make sure Atrium.exe has no Low integrity label, then start it again.");
        }
        return false;
    }
    if (startupChanged && !SetStartOnLogin(s.startOnLogin)) {
        Log(L"settings: settings saved, but Windows startup could not be changed");
        if (warning) *warning = Tr(L"Settings were saved, but Windows startup could not be changed. Toggle Start with Windows again, then save.");
    }
    return true;
}

} // namespace atrium
