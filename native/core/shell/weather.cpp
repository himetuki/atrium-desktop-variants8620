#include "shell/weather.h"

#include "localization.h"

#include "log.h"
#include "settings.h"

#include <winhttp.h>
#include <winrt/Windows.Data.Json.h>
#include <winrt/Windows.Devices.Geolocation.h>
#include <winrt/Windows.Foundation.Collections.h>

#include <algorithm>
#include <chrono>
#include <cmath>

namespace json = winrt::Windows::Data::Json;

namespace atrium {

namespace {

constexpr auto kEvery = std::chrono::minutes(30);
constexpr auto kRetry = std::chrono::minutes(3);

// A GET over WinHTTP; direct: not through the system's proxy. The body, empty on failure.
std::string Get(const std::wstring& url, bool direct)
{
    URL_COMPONENTS u{sizeof(u)};
    wchar_t host[256] = {}, path[2048] = {};
    u.lpszHostName = host;
    u.dwHostNameLength = 256;
    u.lpszUrlPath = path;
    u.dwUrlPathLength = 2048;
    wchar_t extra[2048] = {};
    u.lpszExtraInfo = extra;
    u.dwExtraInfoLength = 2048;
    if (!WinHttpCrackUrl(url.c_str(), 0, 0, &u))
        return {};
    std::string body;
    HINTERNET session = WinHttpOpen(L"Atrium", direct ? WINHTTP_ACCESS_TYPE_NO_PROXY : WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME,
                                    WINHTTP_NO_PROXY_BYPASS, 0);
    if (!session)
        return {};
    WinHttpSetTimeouts(session, 8000, 8000, 8000, 8000);
    HINTERNET connect = WinHttpConnect(session, host, u.nPort, 0);
    std::wstring target = std::wstring(path) + extra;
    HINTERNET request = connect ? WinHttpOpenRequest(connect, L"GET", target.c_str(), nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
                                                     u.nScheme == INTERNET_SCHEME_HTTPS ? WINHTTP_FLAG_SECURE : 0)
                                : nullptr;
    DWORD status = 0, size = sizeof(status);
    if (request && WinHttpSendRequest(request, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) &&
        WinHttpReceiveResponse(request, nullptr) &&
        WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &status, &size,
                            WINHTTP_NO_HEADER_INDEX) &&
        status == 200) {
        for (;;) {
            DWORD n = 0;
            if (!WinHttpQueryDataAvailable(request, &n) || n == 0)
                break;
            std::string chunk(n, '\0');
            DWORD read = 0;
            if (!WinHttpReadData(request, chunk.data(), n, &read) || read == 0)
                break;
            body.append(chunk.data(), read);
            if (body.size() > 4 * 1024 * 1024)
                break;
        }
    }
    if (request)
        WinHttpCloseHandle(request);
    if (connect)
        WinHttpCloseHandle(connect);
    WinHttpCloseHandle(session);
    return body;
}

// Directly first; then through the system's proxy (a network that only reaches out through it).
std::string GetAny(const std::wstring& url)
{
    std::string body = Get(url, true);
    return body.empty() ? Get(url, false) : body;
}

// A place's name for coordinates (BigDataCloud's free reverse geocoding, no account): the city,
// else the locality; "南京市" is shown as "南京", as the network's place is. Asked again only once the
// computer has moved a few kilometres (the worker thread alone calls it).
std::wstring PlaceName(double lat, double lon)
{
    static double lastLat = 1000, lastLon = 1000;
    static std::wstring lastName;
    static bool lastEnglish = false;
    const bool english = LanguageIsEnglish();
    if (!lastName.empty() && lastEnglish == english && std::fabs(lat - lastLat) < 0.03 && std::fabs(lon - lastLon) < 0.03)
        return lastName;
    wchar_t url[256];
    swprintf_s(url, L"https://api.bigdatacloud.net/data/reverse-geocode-client?latitude=%.4f&longitude=%.4f&localityLanguage=%s", lat, lon, english ? L"en" : L"zh");
    json::JsonObject o{nullptr};
    if (!json::JsonObject::TryParse(winrt::to_hstring(GetAny(url)), o))
        return {};
    std::wstring name(o.GetNamedString(L"city", L""));
    if (name.empty())
        name = std::wstring(o.GetNamedString(L"locality", L""));
    if (!LanguageIsEnglish() && name.size() > 2 && name.back() == L'市')
        name.pop_back();
    if (!name.empty()) {
        lastLat = lat;
        lastLon = lon;
        lastName = name;
        lastEnglish = english;
    }
    return name;
}

// Where the computer is: Windows' location service (when it lets desktop apps use it), else the
// network's place. The network's city also names a Windows position that has no name.
bool Locate(double& lat, double& lon, std::wstring& city)
{
    namespace geo = winrt::Windows::Devices::Geolocation;
    bool located = false;
    try {
        if (geo::Geolocator::RequestAccessAsync().get() == geo::GeolocationAccessStatus::Allowed) {
            geo::Geolocator locator;
            auto at = locator.GetGeopositionAsync(std::chrono::minutes(30), std::chrono::seconds(15)).get().Coordinate().Point().Position();
            lat = at.Latitude;
            lon = at.Longitude;
            located = true;
            city = PlaceName(lat, lon);
            Log(L"weather: located by Windows (%s)", city.c_str());
        } else {
            Log(L"weather: Windows' location is off for desktop apps; using the network's place");
        }
    } catch (const winrt::hresult_error& e) {
        Log(L"weather: Windows' location failed (0x%08X); using the network's place", static_cast<unsigned>(e.code()));
    }
    if (located && !city.empty())
        return true;
    json::JsonObject o{nullptr};
    if (!json::JsonObject::TryParse(winrt::to_hstring(Get(LanguageIsEnglish() ? L"http://ip-api.com/json/?lang=en&fields=status,city,lat,lon"
                                                                              : L"http://ip-api.com/json/?lang=zh-CN&fields=status,city,lat,lon",
                                                                    true)),
                                    o) ||
        o.GetNamedString(L"status", L"") != L"success")
        return located;
    city = std::wstring(o.GetNamedString(L"city", L""));
    if (!located) {
        lat = o.GetNamedNumber(L"lat", 0);
        lon = o.GetNamedNumber(L"lon", 0);
    }
    return true;
}

std::wstring Wide(const std::string& s)
{
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), int(s.size()), nullptr, 0);
    std::wstring w(size_t(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), int(s.size()), w.data(), n);
    return w;
}

bool Parse(const std::string& text, json::JsonObject& out)
{
    return !text.empty() && json::JsonObject::TryParse(Wide(text), out);
}

// "2026-11-09T14:00" -> minutes since 2000 (only compared with each other), hour and minute.
int64_t Minutes(const std::wstring& t, int* hour = nullptr, int* minute = nullptr, int* weekday = nullptr)
{
    int y = 0, mo = 0, d = 0, h = 0, mi = 0;
    if (swscanf_s(t.c_str(), L"%d-%d-%dT%d:%d", &y, &mo, &d, &h, &mi) < 3)
        return 0;
    if (hour)
        *hour = h;
    if (minute)
        *minute = mi;
    SYSTEMTIME st{WORD(y), WORD(mo), 0, WORD(d), WORD(h), WORD(mi), 0, 0};
    FILETIME f{};
    SystemTimeToFileTime(&st, &f);
    int64_t m = int64_t((ULARGE_INTEGER{f.dwLowDateTime, f.dwHighDateTime}.QuadPart) / 600000000ULL);
    if (weekday) {
        FileTimeToSystemTime(&f, &st);
        *weekday = st.wDayOfWeek; // 0 Sunday
    }
    return m;
}

const wchar_t* const kWeek[] = {L"周日", L"周一", L"周二", L"周三", L"周四", L"周五", L"周六"};
const wchar_t* const kWeekEn[] = {L"Sun", L"Mon", L"Tue", L"Wed", L"Thu", L"Fri", L"Sat"};
const wchar_t* WeekName(int wd) { return LanguageIsEnglish() ? kWeekEn[wd] : kWeek[wd]; }

json::JsonObject ToJson(const Weather& w)
{
    json::JsonObject o;
    o.SetNamedValue(L"city", json::JsonValue::CreateStringValue(w.city));
    o.SetNamedValue(L"temp", json::JsonValue::CreateNumberValue(w.temp));
    o.SetNamedValue(L"hi", json::JsonValue::CreateNumberValue(w.hi));
    o.SetNamedValue(L"lo", json::JsonValue::CreateNumberValue(w.lo));
    o.SetNamedValue(L"code", json::JsonValue::CreateNumberValue(w.code));
    o.SetNamedValue(L"day", json::JsonValue::CreateBooleanValue(w.day));
    json::JsonArray hours, days;
    for (const WeatherHour& h : w.hours) {
        json::JsonObject x;
        x.SetNamedValue(L"label", json::JsonValue::CreateStringValue(h.label));
        x.SetNamedValue(L"code", json::JsonValue::CreateNumberValue(h.code));
        x.SetNamedValue(L"day", json::JsonValue::CreateBooleanValue(h.day));
        x.SetNamedValue(L"temp", json::JsonValue::CreateNumberValue(h.temp));
        x.SetNamedValue(L"event", json::JsonValue::CreateNumberValue(h.event));
        hours.Append(x);
    }
    for (const WeatherDay& d : w.days) {
        json::JsonObject x;
        x.SetNamedValue(L"label", json::JsonValue::CreateStringValue(d.label));
        x.SetNamedValue(L"code", json::JsonValue::CreateNumberValue(d.code));
        x.SetNamedValue(L"lo", json::JsonValue::CreateNumberValue(d.lo));
        x.SetNamedValue(L"hi", json::JsonValue::CreateNumberValue(d.hi));
        days.Append(x);
    }
    o.SetNamedValue(L"hours", hours);
    o.SetNamedValue(L"days", days);
    return o;
}

bool FromJson(const json::JsonObject& o, Weather& w)
{
    try {
        w.city = std::wstring(o.GetNamedString(L"city", L""));
        w.temp = float(o.GetNamedNumber(L"temp", 0));
        w.hi = float(o.GetNamedNumber(L"hi", 0));
        w.lo = float(o.GetNamedNumber(L"lo", 0));
        w.code = int(o.GetNamedNumber(L"code", 0));
        w.day = o.GetNamedBoolean(L"day", true);
        for (const auto& v : o.GetNamedArray(L"hours", json::JsonArray())) {
            auto x = v.GetObject();
            w.hours.push_back({std::wstring(x.GetNamedString(L"label", L"")), int(x.GetNamedNumber(L"code", 0)), x.GetNamedBoolean(L"day", true),
                               float(x.GetNamedNumber(L"temp", 0)), int(x.GetNamedNumber(L"event", 0))});
        }
        for (const auto& v : o.GetNamedArray(L"days", json::JsonArray())) {
            auto x = v.GetObject();
            w.days.push_back({std::wstring(x.GetNamedString(L"label", L"")), int(x.GetNamedNumber(L"code", 0)), float(x.GetNamedNumber(L"lo", 0)),
                              float(x.GetNamedNumber(L"hi", 0))});
        }
        w.ok = !w.hours.empty() && !w.days.empty();
    } catch (...) {
        w = Weather{};
    }
    return w.ok;
}

} // namespace

std::wstring WeatherCondition(int code)
{
    switch (code) {
    case 0: return Tr(L"Clear");
    case 1: return Tr(L"Mostly clear");
    case 2: return Tr(L"Partly cloudy");
    case 3: return Tr(L"Overcast");
    case 45:
    case 48: return Tr(L"Fog");
    case 51:
    case 53:
    case 55: return Tr(L"Drizzle");
    case 56:
    case 57:
    case 66:
    case 67: return Tr(L"Freezing rain");
    case 61: return Tr(L"Light rain");
    case 63: return Tr(L"Moderate rain");
    case 65: return Tr(L"Heavy rain");
    case 71: return Tr(L"Light snow");
    case 73: return Tr(L"Moderate snow");
    case 75: return Tr(L"Heavy snow");
    case 77: return Tr(L"Snow grains");
    case 80:
    case 81: return Tr(L"Rain showers");
    case 82: return Tr(L"Violent rain showers");
    case 85:
    case 86: return Tr(L"Snow showers");
    case 95: return Tr(L"Thunderstorm");
    case 96:
    case 99: return Tr(L"Thunderstorm with hail");
    default: return Tr(L"Cloudy");
    }
}

std::wstring WeatherCachePath()
{
    std::wstring path = SettingsPath();
    return path.substr(0, path.find_last_of(L'\\')) + L"\\weather.json";
}

WeatherSource::WeatherSource(HWND notify, UINT message) : notify_(notify), message_(message)
{
    // The last forecast at once (stale is better than nothing while the first fetch runs).
    json::JsonObject o{nullptr};
    std::wstring text = ReadTextFile(WeatherCachePath());
    if (!text.empty() && json::JsonObject::TryParse(text, o))
        FromJson(o, latest_);
    thread_ = std::thread([this] { Run(); });
}

WeatherSource::~WeatherSource()
{
    {
        std::lock_guard g(lock_);
        quit_ = true;
    }
    wake_.notify_all();
    if (thread_.joinable())
        thread_.join();
}

void WeatherSource::SetActive(bool on)
{
    {
        std::lock_guard g(lock_);
        if (on == active_)
            return;
        active_ = on;
    }
    wake_.notify_all();
}

void WeatherSource::Refresh()
{
    {
        std::lock_guard g(lock_);
        dirty_ = true;
    }
    wake_.notify_all();
}

Weather WeatherSource::Latest()
{
    std::lock_guard g(lock_);
    return latest_;
}

void WeatherSource::Run()
{
    winrt::init_apartment(winrt::apartment_type::multi_threaded);
    auto next = std::chrono::steady_clock::now();
    for (;;) {
        {
            std::unique_lock g(lock_);
            wake_.wait(g, [&] { return quit_ || active_; }); // nothing to do while no weather widget shows
            if (quit_)
                return;
            wake_.wait_until(g, next, [&] { return quit_ || !active_ || dirty_; });
            if (quit_)
                return;
            if (!active_ || (!dirty_ && std::chrono::steady_clock::now() < next))
                continue;
            dirty_ = false;
        }
        bool ok = false;
        try {
            ok = Fetch();
        } catch (...) {
            Log(L"weather: fetch failed");
        }
        next = std::chrono::steady_clock::now() + (ok ? kEvery : kRetry);
        PostMessageW(notify_, message_, 0, 0);
    }
}

bool WeatherSource::Fetch()
{
    double lat = 0, lon = 0;
    std::wstring city;
    if (!Locate(lat, lon, city)) {
        std::lock_guard g(lock_);
        latest_.ok = latest_.ok && !latest_.hours.empty();
        latest_.error = Tr(L"Location unavailable");
        Log(L"weather: no location");
        return false;
    }
    json::JsonObject o{nullptr};
    wchar_t url[512];
    swprintf_s(url,
               L"https://api.open-meteo.com/v1/forecast?latitude=%.4f&longitude=%.4f&timezone=auto&forecast_days=7"
               L"&current=temperature_2m,weather_code,is_day&hourly=temperature_2m,weather_code,is_day"
               L"&daily=weather_code,temperature_2m_max,temperature_2m_min,sunrise,sunset",
               lat, lon);
    if (!Parse(GetAny(url), o) || !o.HasKey(L"current") || !o.HasKey(L"hourly") || !o.HasKey(L"daily")) {
        std::lock_guard g(lock_);
        latest_.error = Tr(L"Weather unavailable");
        Log(L"weather: no forecast for %s", city.c_str());
        return false;
    }
    Weather w;
    w.city = city;
    auto current = o.GetNamedObject(L"current");
    w.temp = float(current.GetNamedNumber(L"temperature_2m", 0));
    w.code = int(current.GetNamedNumber(L"weather_code", 0));
    w.day = current.GetNamedNumber(L"is_day", 1) != 0;
    const int64_t now = Minutes(std::wstring(current.GetNamedString(L"time", L"")));

    // The next hours: this one ("现在") and five more, sunrises and sunsets among them.
    struct Item {
        int64_t at;
        WeatherHour hour;
    };
    std::vector<Item> items;
    auto hourly = o.GetNamedObject(L"hourly");
    auto times = hourly.GetNamedArray(L"time"), temps = hourly.GetNamedArray(L"temperature_2m"), codes = hourly.GetNamedArray(L"weather_code"),
         days = hourly.GetNamedArray(L"is_day");
    for (uint32_t i = 0; i < times.Size() && items.size() < 6; ++i) {
        int h = 0;
        int64_t at = Minutes(std::wstring(times.GetStringAt(i)), &h);
        if (at + 60 <= now)
            continue;
        WeatherHour x;
        x.label = items.empty() ? std::wstring(Tr(L"Now")) : LanguageIsEnglish() ? std::to_wstring(h) + L":00" : std::to_wstring(h) + L"时";
        x.temp = float(temps.GetNumberAt(i));
        x.code = int(codes.GetNumberAt(i));
        x.day = days.GetNumberAt(i) != 0;
        items.push_back({at, x});
    }
    auto daily = o.GetNamedObject(L"daily");
    if (!items.empty()) {
        const int64_t last = items.back().at;
        for (const wchar_t* key : {L"sunrise", L"sunset"}) {
            auto list = daily.GetNamedArray(key);
            for (uint32_t i = 0; i < list.Size(); ++i) {
                int h = 0, m = 0;
                int64_t at = Minutes(std::wstring(list.GetStringAt(i)), &h, &m);
                if (at <= now || at >= last)
                    continue;
                WeatherHour x;
                wchar_t label[16];
                swprintf_s(label, L"%d:%02d", h, m);
                x.label = label;
                x.event = key[4] == L'r' ? 1 : 2;
                items.push_back({at, x});
            }
        }
        std::stable_sort(items.begin(), items.end(), [](const Item& a, const Item& b) { return a.at < b.at; });
        if (items.size() > 6)
            items.resize(6);
    }
    for (const Item& i : items)
        w.hours.push_back(i.hour);

    auto dates = daily.GetNamedArray(L"time"), dcodes = daily.GetNamedArray(L"weather_code"), his = daily.GetNamedArray(L"temperature_2m_max"),
         los = daily.GetNamedArray(L"temperature_2m_min");
    for (uint32_t i = 0; i < dates.Size(); ++i) {
        int wd = 0;
        Minutes(std::wstring(dates.GetStringAt(i)) + L"T00:00", nullptr, nullptr, &wd);
        WeatherDay d;
        d.label = i == 0 ? Tr(L"Today") : WeekName(wd);
        d.code = int(dcodes.GetNumberAt(i));
        d.hi = float(his.GetNumberAt(i));
        d.lo = float(los.GetNumberAt(i));
        w.days.push_back(d);
    }
    if (w.hours.empty() || w.days.empty())
        return false;
    w.hi = w.days[0].hi;
    w.lo = w.days[0].lo;
    w.ok = true;
    WriteTextFile(WeatherCachePath(), std::wstring(ToJson(w).Stringify()));
    Log(L"weather: %s %.0f° %s", w.city.c_str(), w.temp, WeatherCondition(w.code).c_str());
    std::lock_guard g(lock_);
    latest_ = std::move(w);
    return true;
}

} // namespace atrium
