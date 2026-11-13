#include "shell/update_checker.h"

#include "localization.h"
#include "log.h"
#include "settings.h"
#include "ui/notifications.h"

#include <winrt/Windows.Data.Json.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Web.Http.h>
#include <winrt/Windows.Web.Http.Headers.h>
#include <winrt/Windows.Security.Cryptography.h>
#include <winrt/Windows.Security.Cryptography.Core.h>

#include <array>
#include <chrono>
#include <memory>
#include <optional>
#include <string_view>
#include <vector>
#include <cwctype>

namespace atrium {
namespace {
namespace json = winrt::Windows::Data::Json;
constexpr int64_t kIntervalSeconds = 6 * 60 * 60;
constexpr wchar_t kLatest[] = L"https://api.github.com/repos/wynter-yaxley/atrium-desktop/releases/latest";
using Version = std::array<unsigned, 4>;

std::optional<Version> ParseVersion(std::wstring_view tag)
{
    if (!tag.empty() && (tag.front() == L'v' || tag.front() == L'V')) tag.remove_prefix(1);
    tag = tag.substr(0, tag.find(L'+'));
    Version version{};
    size_t part = 0;
    bool digit = false;
    for (wchar_t c : tag) {
        if (c == L'.' && digit && part < 3) {
            ++part;
            digit = false;
        } else if (c >= L'0' && c <= L'9') {
            version[part] = version[part] * 10 + (c - L'0');
            if (version[part] > 65535) return {};
            digit = true;
        } else return {};
    }
    return digit && part >= 1 ? std::optional<Version>(version) : std::nullopt;
}

Version CurrentVersion()
{
    wchar_t exe[MAX_PATH]{};
    GetModuleFileNameW(nullptr, exe, MAX_PATH);
    DWORD unused = 0;
    std::vector<BYTE> data(GetFileVersionInfoSizeW(exe, &unused));
    VS_FIXEDFILEINFO* info = nullptr;
    UINT size = 0;
    if (data.empty() || !GetFileVersionInfoW(exe, 0, DWORD(data.size()), data.data()) ||
        !VerQueryValueW(data.data(), L"\\", reinterpret_cast<void**>(&info), &size))
        throw winrt::hresult_error(E_FAIL, L"Missing application version");
    return {HIWORD(info->dwFileVersionMS), LOWORD(info->dwFileVersionMS), HIWORD(info->dwFileVersionLS), LOWORD(info->dwFileVersionLS)};
}

int64_t Now()
{
    return std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
}

DWORD Delay(int64_t last, int64_t now)
{
    // A clock correction must not postpone checks indefinitely.
    return last > 0 && last <= now && now - last < kIntervalSeconds ? DWORD((kIntervalSeconds - (now - last)) * 1000) : 0;
}

std::wstring ExeDirectory()
{
    wchar_t path[MAX_PATH]{};
    GetModuleFileNameW(nullptr, path, MAX_PATH);
    std::wstring directory(path);
    return directory.substr(0, directory.find_last_of(L'\\'));
}

std::wstring UpdateCache()
{
    using namespace winrt::Windows::Security::Cryptography;
    std::wstring directory = ExeDirectory();
    int count = LCMapStringEx(LOCALE_NAME_INVARIANT, LCMAP_UPPERCASE, directory.data(), int(directory.size()), nullptr, 0, nullptr, nullptr, 0);
    std::wstring upper(size_t(count), L'\0');
    LCMapStringEx(LOCALE_NAME_INVARIANT, LCMAP_UPPERCASE, directory.data(), int(directory.size()), upper.data(), count, nullptr, nullptr, 0);
    auto algorithm = Core::HashAlgorithmProvider::OpenAlgorithm(Core::HashAlgorithmNames::Sha256());
    std::wstring key = CryptographicBuffer::EncodeToHexString(algorithm.HashData(
        CryptographicBuffer::ConvertStringToBinary(upper, BinaryStringEncoding::Utf8))).c_str();
    key.resize(16);
    for (wchar_t& ch : key) ch = wchar_t(std::towupper(ch));
    std::wstring root = SettingsPath();
    root = root.substr(0, root.find_last_of(L'\\')) + L"\\Updates";
    CreateDirectoryW(root.c_str(), nullptr);
    root += L"\\" + key;
    CreateDirectoryW(root.c_str(), nullptr);
    return root;
}

void StartWorker(const std::wstring& arguments)
{
    std::wstring exe = ExeDirectory() + L"\\Settings\\Atrium.Settings.exe";
    if (GetFileAttributesW(exe.c_str()) == INVALID_FILE_ATTRIBUTES) return; // native-only development build
    std::wstring command = L"\"" + exe + L"\" " + arguments;
    STARTUPINFOW startup{sizeof(startup)};
    startup.dwFlags = STARTF_USESHOWWINDOW;
    startup.wShowWindow = SW_HIDE;
    PROCESS_INFORMATION process{};
    if (CreateProcessW(exe.c_str(), command.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &startup, &process)) {
        CloseHandle(process.hThread);
        CloseHandle(process.hProcess);
    } else Log(L"update: could not start background queue (%lu)", GetLastError());
}

void DownloadRelease(const json::JsonObject& release, const Version& current)
{
    std::wstring tag = release.GetNamedString(L"tag_name").c_str();
    auto latest = ParseVersion(tag);
    if (!latest) throw winrt::hresult_invalid_argument(L"Invalid release version");
    if (*latest <= current || release.GetNamedBoolean(L"draft", false) || release.GetNamedBoolean(L"prerelease", false)) return;
    std::wstring number = tag.substr(tag[0] == L'v' || tag[0] == L'V' ? 1 : 0);
    number = number.substr(0, number.find(L'+'));
    std::wstring assetName = L"Atrium-" + number + L"-win-x64.zip";
    for (const auto& value : release.GetNamedArray(L"assets")) {
        auto asset = value.GetObject();
        if (asset.GetNamedString(L"name") != assetName) continue;
        json::JsonObject payload;
        std::wstring version;
        for (unsigned part : *latest) version += (version.empty() ? L"" : L".") + std::to_wstring(part);
        payload.SetNamedValue(L"Version", json::JsonValue::CreateStringValue(version));
        payload.SetNamedValue(L"Tag", json::JsonValue::CreateStringValue(tag));
        payload.SetNamedValue(L"Page", release.GetNamedValue(L"html_url"));
        payload.SetNamedValue(L"Download", asset.GetNamedValue(L"browser_download_url"));
        payload.SetNamedValue(L"Digest", asset.HasKey(L"digest") ? asset.GetNamedValue(L"digest") : json::JsonValue::CreateNullValue());
        using namespace winrt::Windows::Security::Cryptography;
        StartWorker(L"--queue-update " + std::wstring(CryptographicBuffer::EncodeToBase64String(
            CryptographicBuffer::ConvertStringToBinary(payload.Stringify(), BinaryStringEncoding::Utf8)).c_str()));
        Log(L"update: queued background download for %s", tag.c_str());
        return;
    }
}
} // namespace

UpdateChecker::UpdateChecker(HWND notify)
    : notify_(notify), stop_(CreateEventW(nullptr, TRUE, FALSE, nullptr)), cache_(UpdateCache())
{
    if (!stop_) winrt::throw_last_error();
    if (GetFileAttributesW((cache_ + L"\\download.json").c_str()) != INVALID_FILE_ATTRIBUTES)
        StartWorker(L"--resume-update");
    PostMessageW(notify_, WM_APP + 11, 0, 0);
    worker_ = std::thread([this] { Run(); });
}

UpdateChecker::~UpdateChecker()
{
    SetEvent(stop_);
    worker_.join();
    CloseHandle(stop_);
}

void UpdateChecker::Run()
{
    winrt::init_apartment(winrt::apartment_type::multi_threaded);
    try {
        const Version current = CurrentVersion();
        const std::wstring path = cache_ + L"\\update-state.json";
        json::JsonObject state;
        if (!json::JsonObject::TryParse(ReadTextFile(path), state)) state = json::JsonObject();
        int64_t checked = int64_t(state.GetNamedNumber(L"checked", 0));
        while (WaitForSingleObject(stop_, Delay(checked, Now())) == WAIT_TIMEOUT) {
            checked = Now();
            try {
                winrt::Windows::Web::Http::HttpClient http;
                http.DefaultRequestHeaders().UserAgent().TryParseAdd(L"Atrium-Desktop/1.0");
                http.DefaultRequestHeaders().Accept().TryParseAdd(L"application/vnd.github+json");
                auto request = http.GetStringAsync(winrt::Windows::Foundation::Uri(kLatest));
                const auto start = GetTickCount64();
                while (request.Status() == winrt::Windows::Foundation::AsyncStatus::Started) {
                    if (WaitForSingleObject(stop_, 100) == WAIT_OBJECT_0 || GetTickCount64() - start >= 15000) {
                        request.Cancel();
                        break;
                    }
                }
                if (WaitForSingleObject(stop_, 0) == WAIT_OBJECT_0) break;
                auto release = json::JsonObject::Parse(request.GetResults());
                std::wstring tag = release.GetNamedString(L"tag_name").c_str();
                DownloadRelease(release, current);
                Log(L"update: checked latest release %s", tag.c_str());
            } catch (const winrt::hresult_error& error) {
                Log(L"update: check failed (0x%08X); retry in 6 hours", unsigned(error.code().value));
            }
            state.SetNamedValue(L"checked", json::JsonValue::CreateNumberValue(double(checked)));
            if (!WriteTextFile(path, state.Stringify().c_str())) Log(L"update: could not save check state");
        }
    } catch (const winrt::hresult_error& error) {
        Log(L"update: checker stopped (0x%08X)", unsigned(error.code().value));
    }
    winrt::uninit_apartment();
}

void UpdateChecker::NotifyPrepared(Notifications& notifications)
{
    try {
        const auto text = ReadTextFile(cache_ + L"\\ready.json");
        if (text.empty()) { notifications.ClearPreparedUpdate(); return; }
        auto ready = json::JsonObject::Parse(text);
        auto version = ParseVersion(ready.GetNamedString(L"Version").c_str());
        const std::wstring tag = ready.GetNamedString(L"Tag").c_str();
        if (!version || *version <= CurrentVersion()) { notifications.ClearPreparedUpdate(); return; }
        const bool silent = ReadTextFile(cache_ + L"\\ready-notified.txt") == tag;
        Notice notice;
        notice.source = L"atrium-update";
        notice.externalId = 1; // one current update card, independent of ordinary notices
        notice.app = L"Atrium";
        notice.title = Tr(L"Update ready");
        notice.body = TrFormat(L"%s has been downloaded. Restart from About to install it.", tag.c_str());
        notice.action = L"settings:updates";
        notifications.Post(std::move(notice), silent);
        // This runs synchronously on the UI thread: the card is now accepted,
        // rather than merely having a message queued for a possibly dying UI.
        if (!silent && !WriteTextFile(cache_ + L"\\ready-notified.txt", tag)) Log(L"update: could not save the banner acknowledgement");
    } catch (const winrt::hresult_error& error) {
        Log(L"update: could not read prepared update (0x%08X)", unsigned(error.code().value));
    }
}
} // namespace atrium
