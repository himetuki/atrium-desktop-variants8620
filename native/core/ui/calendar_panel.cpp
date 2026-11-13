#include "ui/calendar_panel.h"

#include "localization.h"
#include "clock.h"
#include "log.h"
#include "motion.h"
#include "shell/lunar.h"

#include <shellapi.h>
#include <shellscalingapi.h>
#include <windowsx.h>

#include <algorithm>
#include <cmath>

namespace atrium {

namespace {

constexpr float kW = 336, kPad = 14, kGap = 10, kMonthH = 48, kWeekH = 26, kCellH = 46, kFooter = 40;
constexpr UINT_PTR kTickTimer = 1;
constexpr double kMonthMs = 260;

D2D1_COLOR_F Alpha(D2D1_COLOR_F c, float a) { return D2D1::ColorF(c.r, c.g, c.b, c.a * a); }
bool In(const D2D1_RECT_F& r, float x, float y) { return x >= r.left && x < r.right && y >= r.top && y < r.bottom; }
const wchar_t* const kWeekNames[] = {L"一", L"二", L"三", L"四", L"五", L"六", L"日"};
const wchar_t* const kWeekNamesEn[] = {L"M", L"T", L"W", L"T", L"F", L"S", L"S"};
const wchar_t* const kMonthsEn[] = {L"January", L"February", L"March", L"April", L"May", L"June",
                                    L"July", L"August", L"September", L"October", L"November", L"December"};
const wchar_t* const kWeekShortEn[] = {L"Mon", L"Tue", L"Wed", L"Thu", L"Fri", L"Sat", L"Sun"};
const wchar_t* WeekHeader(int wd) { return LanguageIsEnglish() ? kWeekNamesEn[wd] : kWeekNames[wd]; }
void OpenUri(const wchar_t* uri) { ShellExecuteW(nullptr, L"open", uri, nullptr, nullptr, SW_SHOWNORMAL); }

} // namespace

CalendarPanel::CalendarPanel(HINSTANCE instance) : surface_(instance, L"Atrium Calendar", true)
{
    surface_.paint = [this](Canvas& c) { Paint(c); };
    surface_.message = [this](UINT m, WPARAM w, LPARAM l, LRESULT& r) { return Message(m, w, l, r); };
    surface_.onDismiss = [this] { Close(); };
    DWORD first = 0;
    if (GetLocaleInfoEx(LOCALE_NAME_USER_DEFAULT, LOCALE_IFIRSTDAYOFWEEK | LOCALE_RETURN_NUMBER, reinterpret_cast<LPWSTR>(&first), 2))
        firstWeekday_ = int(first % 7);
}

CalendarPanel::~CalendarPanel() = default;

void CalendarPanel::ShowMonth(int y, int m, int direction)
{
    year_ = y;
    month_ = m;
    monthDir_ = direction;
    monthMs_ = ClockMs();
    // Six weeks from the week holding the 1st.
    int lead = (Weekday(y, m, 1) - firstWeekday_ + 7) % 7;
    int py = m == 1 ? y - 1 : y, pm = m == 1 ? 12 : m - 1;
    int cy = py, cm = pm, cd = DaysIn(py, pm) - lead + 1;
    if (lead == 0) {
        cy = y;
        cm = m;
        cd = 1;
    }
    days_.clear();
    for (int i = 0; i < 42; ++i) {
        Day day{cy, cm, cd};
        day.lunar = lunar_.DayLabel(cy, cm, cd, &day.festival);
        days_.push_back(std::move(day));
        if (++cd > DaysIn(cy, cm)) {
            cd = 1;
            cm = cm == 12 ? 1 : cm + 1;
            cy = cm == 1 ? cy + 1 : cy;
        }
    }
    surface_.Invalidate();
}

D2D1_SIZE_F CalendarPanel::Size() const { return D2D1::SizeF(kW, kMonthH + kWeekH + 6 * kCellH + 8 + kFooter + kGap + barH_); }

void CalendarPanel::Place()
{
    RECT r = PopupRect(bar_, Size(), PopupAlign::Right);
    if (surface_.Visible())
        surface_.Move(r); // pops out of its entry on the bar
    else
        surface_.Show(r, true, POINT{(anchor_.left + anchor_.right) / 2, r.bottom});
}

void CalendarPanel::Open(RECT anchor, RECT bar)
{
    anchor_ = anchor;
    bar_ = bar;
    hover_.clear();
    pressed_.clear();
    hoverAnim_.clear();
    GetLocalTime(&selected_);
    ShowMonth(selected_.wYear, selected_.wMonth, 0);
    Place();
    SetTimer(surface_.Hwnd(), kTickTimer, 1000, nullptr);
    Log(L"calendar: opened");
}

void CalendarPanel::Close()
{
    if (!surface_.Visible())
        return;
    KillTimer(surface_.Hwnd(), kTickTimer);
    surface_.Hide();
    lastClosed_ = GetTickCount64();
}

float CalendarPanel::Hover(Canvas& c, const std::wstring& id, bool on)
{
    auto& f = hoverAnim_[id];
    float v = f.Step(on, c.now);
    if (f.moving)
        c.Continue();
    return v;
}

void CalendarPanel::Paint(Canvas& c)
{
    const Theme& t = c.theme;
    hits_.clear();
    const D2D1_SIZE_F size = Size();
    D2D1_RECT_F card = D2D1::RectF(0, 0, kW, size.height - kGap - barH_);
    D2D1_RECT_F cap = D2D1::RectF(0, card.bottom + kGap, kW, size.height);
    std::vector<ShapeDip> shapes{{card, radius_}, {cap, barH_ / 2}};
    if (c.panel)
        for (const ShapeDip& sh : shapes)
            c.Fill(sh.rect, sh.radius, t.panel);
    surface_.SetShapes(shapes);
    PaintMonth(c, t, card);
    PaintCapsule(c, t, cap);
}

void CalendarPanel::PaintMonth(Canvas& c, const Theme& t, D2D1_RECT_F card)
{
    const float w = card.right - card.left;
    SYSTEMTIME now;
    GetLocalTime(&now);

    // The month and its buttons.
    std::wstring title = LanguageIsEnglish() ? std::wstring(kMonthsEn[std::clamp(month_, 1, 12) - 1]) + L" " + std::to_wstring(year_)
                                             : std::to_wstring(year_) + L"年" + std::to_wstring(month_) + L"月";
    c.Text(title, D2D1::RectF(kPad + 6, 0, w / 2 + 20, kMonthH), 16, t.text, DWRITE_FONT_WEIGHT_SEMI_BOLD);
    auto button = [&](const std::wstring& id, D2D1_RECT_F b, const std::wstring& glyph, const std::wstring& label) {
        float hv = Hover(c, id, hover_ == id);
        if (hv > 0.001f || pressed_ == id)
            c.Fill(b, (b.bottom - b.top) / 2, pressed_ == id ? t.press : Alpha(t.hover, hv));
        if (!glyph.empty())
            c.Glyph(glyph, b, 11, t.text);
        else
            c.Text(label, b, 12.5f, t.text, DWRITE_FONT_WEIGHT_NORMAL, 1);
        hits_.push_back({b, id});
    };
    float by = kMonthH / 2;
    D2D1_RECT_F next = D2D1::RectF(w - kPad - 34, by - 17, w - kPad, by + 17);
    D2D1_RECT_F prev = D2D1::RectF(next.left - 38, by - 17, next.left - 4, by + 17);
    button(L"prev", prev, L"\uE76B", L"");
    button(L"next", next, L"\uE76C", L"");
    bool away = year_ != now.wYear || month_ != now.wMonth || selected_.wYear != now.wYear || selected_.wMonth != now.wMonth ||
                selected_.wDay != now.wDay;
    if (away) {
        std::wstring label = Tr(L"Today");
        float bw = c.TextWidth(label, 12.5f) + 26;
        button(L"today", D2D1::RectF(prev.left - 8 - bw, by - 15, prev.left - 8, by + 15), L"", label);
    }

    // Weekday names, then six weeks; the month slides in from the side it came from.
    const float cellW = (w - 2 * kPad) / 7, top = kMonthH;
    for (int i = 0; i < 7; ++i) {
        int wd = (firstWeekday_ + i) % 7;
        c.Text(WeekHeader(wd), D2D1::RectF(kPad + float(i) * cellW, top, kPad + float(i + 1) * cellW, top + kWeekH), 12,
               wd >= 5 ? t.subtext : t.faint, DWRITE_FONT_WEIGHT_NORMAL, 1);
    }
    float e = motion::EaseOut(float((c.now - monthMs_) / kMonthMs));
    if (e < 1)
        c.Continue();
    const float gridTop = top + kWeekH;
    c.Clip(D2D1::RectF(0, gridTop - 2, w, gridTop + 6 * kCellH + 4));
    c.BeginGroup(monthDir_ ? e : 1, 1, w / 2, gridTop + 3 * kCellH, float(monthDir_) * 28 * (1 - e), 0);
    for (size_t i = 0; i < days_.size(); ++i) {
        const Day& d = days_[i];
        float x = kPad + float(i % 7) * cellW, y = gridTop + float(i / 7) * kCellH;
        D2D1_RECT_F cell = D2D1::RectF(x + 2, y + 2, x + cellW - 2, y + kCellH - 2);
        std::wstring id = L"day:" + std::to_wstring(i);
        bool inMonth = d.m == month_, today = d.y == now.wYear && d.m == now.wMonth && d.d == now.wDay;
        bool sel = d.y == selected_.wYear && d.m == selected_.wMonth && d.d == selected_.wDay;
        float hv = Hover(c, id, hover_ == id);
        const float r = 12;
        if (today)
            c.Fill(cell, r, pressed_ == id ? Alpha(t.accent, 0.8f) : t.accent);
        else if (pressed_ == id)
            c.Fill(cell, r, t.press);
        else if (hv > 0.001f)
            c.Fill(cell, r, Alpha(t.hover, hv));
        if (sel && !today)
            c.Stroke(cell, r, Alpha(t.accent, 0.95f), 1.5f);
        float fade = inMonth ? 1.f : 0.38f;
        D2D1_COLOR_F ink = today ? t.accentText : Alpha(t.text, fade);
        D2D1_COLOR_F under = today ? t.accentText : d.festival ? Alpha(t.accent, fade) : Alpha(t.subtext, fade);
        bool halo = c.halo;
        if (today)
            c.halo = false;
        bool lunar = !d.lunar.empty();
        c.Text(std::to_wstring(d.d), D2D1::RectF(cell.left, cell.top + (lunar ? 3 : 0), cell.right, lunar ? cell.top + 24 : cell.bottom), 14.5f, ink,
               today ? DWRITE_FONT_WEIGHT_SEMI_BOLD : DWRITE_FONT_WEIGHT_NORMAL, 1);
        if (lunar)
            c.Text(d.lunar, D2D1::RectF(cell.left - 2, cell.top + 23, cell.right + 2, cell.bottom - 2), 9.5f, under, DWRITE_FONT_WEIGHT_NORMAL, 1);
        c.halo = halo;
        hits_.push_back({cell, id});
    }
    c.EndGroup();
    c.Unclip();

    // The system's settings.
    float fy = gridTop + 6 * kCellH + 8;
    c.Line(kPad + 8, fy, w - kPad - 8, fy, t.divider);
    const std::wstring settingsLabel = Tr(L"Date & time settings");
    D2D1_RECT_F link = D2D1::RectF(kPad, fy + 5, kPad + c.TextWidth(settingsLabel, 12.5f) + 28, fy + kFooter - 5);
    float lh = Hover(c, L"settings", hover_ == L"settings");
    if (lh > 0.001f)
        c.Fill(link, (link.bottom - link.top) / 2, Alpha(t.hover, lh));
    c.Text(settingsLabel, link, 12.5f, t.subtext, DWRITE_FONT_WEIGHT_NORMAL, 1);
    hits_.push_back({link, L"settings"});
}

void CalendarPanel::PaintCapsule(Canvas& c, const Theme& t, D2D1_RECT_F cap)
{
    SYSTEMTIME now;
    GetLocalTime(&now);
    const float h = cap.bottom - cap.top, edge = std::max(16.f, h / 2 - 4);
    wchar_t time[16];
    swprintf_s(time, L"%02d:%02d:%02d", now.wHour, now.wMinute, now.wSecond);
    float tw = c.TextWidth(time, 20, DWRITE_FONT_WEIGHT_SEMI_BOLD);
    c.Text(time, D2D1::RectF(cap.left + edge, cap.top, cap.left + edge + tw + 4, cap.bottom), 20, t.text, DWRITE_FONT_WEIGHT_SEMI_BOLD);
    // The selected day (today unless another was picked).
    int wd = Weekday(selected_.wYear, selected_.wMonth, selected_.wDay);
    std::wstring date = LanguageIsEnglish()
                            ? std::wstring(kWeekShortEn[wd]) + L", " + kMonthsEn[std::clamp(int(selected_.wMonth), 1, 12) - 1] + L" " +
                                  std::to_wstring(selected_.wDay) +
                                  (selected_.wYear != now.wYear ? L", " + std::to_wstring(selected_.wYear) : std::wstring())
                            : std::to_wstring(selected_.wMonth) + L"月" + std::to_wstring(selected_.wDay) + L"日 星期" + kWeekNames[wd] +
                                  (selected_.wYear != now.wYear ? L"年" : L"");
    std::wstring lunar = lunar_.Line(selected_.wYear, selected_.wMonth, selected_.wDay);
    D2D1_RECT_F text = D2D1::RectF(cap.left + edge + tw + 16, cap.top, cap.right - edge, cap.bottom);
    if (lunar.empty()) {
        c.Text(date, text, 13, t.text, DWRITE_FONT_WEIGHT_NORMAL, 2);
    } else {
        float mid = (cap.top + cap.bottom) / 2;
        c.Text(date, D2D1::RectF(text.left, mid - 19, text.right, mid + 1), 13, t.text, DWRITE_FONT_WEIGHT_NORMAL, 2);
        c.Text(lunar, D2D1::RectF(text.left, mid, text.right, mid + 18), 11.5f, t.subtext, DWRITE_FONT_WEIGHT_NORMAL, 2);
    }
}

std::wstring CalendarPanel::HitAt(POINT pt) const
{
    float s = surface_.Scale(), x = float(pt.x) / s, y = float(pt.y) / s;
    for (const Hit& h : hits_)
        if (In(h.rect, x, y))
            return h.id;
    return {};
}

void CalendarPanel::Activate(const std::wstring& id)
{
    if (id == L"prev")
        ShowMonth(month_ == 1 ? year_ - 1 : year_, month_ == 1 ? 12 : month_ - 1, -1);
    else if (id == L"next")
        ShowMonth(month_ == 12 ? year_ + 1 : year_, month_ == 12 ? 1 : month_ + 1, 1);
    else if (id == L"today") {
        SYSTEMTIME now;
        GetLocalTime(&now);
        int dir = now.wYear * 12 + now.wMonth < year_ * 12 + month_ ? -1 : now.wYear * 12 + now.wMonth > year_ * 12 + month_ ? 1 : 0;
        selected_ = now;
        ShowMonth(now.wYear, now.wMonth, dir);
    } else if (id.rfind(L"day:", 0) == 0) {
        size_t i = size_t(std::stoul(id.substr(4)));
        if (i >= days_.size())
            return;
        const Day& d = days_[i];
        selected_.wYear = WORD(d.y);
        selected_.wMonth = WORD(d.m);
        selected_.wDay = WORD(d.d);
        if (d.m != month_) // a day of the next or previous month: go there
            ShowMonth(d.y, d.m, d.y * 12 + d.m < year_ * 12 + month_ ? -1 : 1);
        surface_.Invalidate();
    } else if (id == L"settings") {
        Close();
        OpenUri(L"ms-settings:dateandtime");
    }
}

bool CalendarPanel::Message(UINT m, WPARAM w, LPARAM l, LRESULT& result)
{
    POINT pt{GET_X_LPARAM(l), GET_Y_LPARAM(l)};
    switch (m) {
    case WM_ACTIVATE:
        if (LOWORD(w) == WA_INACTIVE && surface_.Visible())
            Close();
        return false;
    case WM_TIMER:
        if (w == kTickTimer) {
            surface_.Invalidate(); // the seconds
            return true;
        }
        return false;
    case WM_MOUSEMOVE: {
        if (!tracking_) {
            TRACKMOUSEEVENT tme{sizeof(tme), TME_LEAVE, surface_.Hwnd(), 0};
            tracking_ = TrackMouseEvent(&tme) != FALSE;
        }
        std::wstring h = HitAt(pt);
        if (h != hover_) {
            hover_ = h;
            surface_.Invalidate();
        }
        return true;
    }
    case WM_MOUSELEAVE:
        tracking_ = false;
        hover_.clear();
        pressed_.clear();
        surface_.Invalidate();
        return true;
    case WM_LBUTTONDOWN:
        pressed_ = HitAt(pt);
        if (!pressed_.empty()) {
            float s = surface_.Scale();
            surface_.Ripple(float(pt.x) / s, float(pt.y) / s);
        }
        surface_.Invalidate();
        return true;
    case WM_LBUTTONUP: {
        std::wstring h = HitAt(pt);
        bool same = !h.empty() && h == pressed_;
        pressed_.clear();
        surface_.Invalidate();
        if (same)
            Activate(h);
        return true;
    }
    case WM_MOUSEWHEEL: {
        int notches = GET_WHEEL_DELTA_WPARAM(w) / WHEEL_DELTA;
        if (notches)
            Activate(notches > 0 ? L"prev" : L"next");
        return true;
    }
    case WM_KEYDOWN:
        if (w == VK_ESCAPE) {
            Close();
            return true;
        }
        if (w == VK_LEFT || w == VK_PRIOR || w == VK_UP) {
            Activate(L"prev");
            return true;
        }
        if (w == VK_RIGHT || w == VK_NEXT || w == VK_DOWN) {
            Activate(L"next");
            return true;
        }
        if (w == VK_HOME) {
            Activate(L"today");
            return true;
        }
        return false;
    }
    (void)result;
    return false;
}

} // namespace atrium
