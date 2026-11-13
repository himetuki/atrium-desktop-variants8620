#include "ui/window_preview.h"

#include "localization.h"

#include "clock.h"
#include "log.h"

#include <windowsx.h>

#include <algorithm>
#include <cmath>

namespace atrium {

namespace {

// DIP. A card: its title row, then the window's picture; cards side by side, inset from the glass.
constexpr float kRadius = 18, kPad = 8, kGap = 6, kCardPad = 8, kTitleH = 28, kImageH = 124, kMinImageW = 150, kMaxImageW = 228;
constexpr float kCardR = kRadius - kPad; // concentric with the glass
constexpr UINT_PTR kLeaveTimer = 0x5B01, kFadeTimer = 0x5B02;
constexpr UINT kLeaveMs = 320, kFadeMs = 140;

D2D1_COLOR_F Alpha(D2D1_COLOR_F c, float a) { return D2D1::ColorF(c.r, c.g, c.b, c.a * a); }

std::wstring TitleOf(HWND h)
{
    // Never GetWindowText: it sends a message to the window's thread, which may be hung.
    wchar_t buf[256] = {};
    int n = InternalGetWindowText(h, buf, 256);
    return std::wstring(buf, size_t(std::max(n, 0)));
}

} // namespace

WindowPreview::WindowPreview(HINSTANCE instance) : surface_(instance, L"Atrium Window Previews", false)
{
    surface_.paint = [this](Canvas& c) { Paint(c); };
    surface_.message = [this](UINT m, WPARAM w, LPARAM l, LRESULT& r) { return Message(m, w, l, r); };
    surface_.onDismiss = [this] { Close(); };
    surface_.onReady = [this] {
        // The glass has popped out: the pictures fade in over it.
        fadeStart_ = ClockMs();
        SetTimer(surface_.Hwnd(), kFadeTimer, 15, nullptr);
    };
    surface_.SetRadius(kRadius);
}

WindowPreview::~WindowPreview() { DropThumbs(); }

void WindowPreview::DropThumbs()
{
    for (Card& c : cards_)
        if (c.thumb) {
            DwmUnregisterThumbnail(c.thumb);
            c.thumb = nullptr;
        }
}

void WindowPreview::SetWindows(const std::vector<HWND>& windows)
{
    const HWND hovered = hover_ >= 0 && size_t(hover_) < cards_.size() ? cards_[size_t(hover_)].window : nullptr;
    const HWND pressed = pressed_ >= 0 && size_t(pressed_) < cards_.size() ? cards_[size_t(pressed_)].window : nullptr;
    // Keep the cards (and their thumbnails) of windows still there, in the group's order.
    std::vector<Card> next;
    for (HWND h : windows) {
        if (!IsWindow(h) || !IsWindowVisible(h))
            continue;
        auto old = std::find_if(cards_.begin(), cards_.end(), [&](const Card& c) { return c.window == h; });
        Card c;
        if (old != cards_.end()) {
            c = std::move(*old);
            old->thumb = nullptr;
        }
        c.window = h;
        c.title = TitleOf(h);
        next.push_back(std::move(c));
    }
    DropThumbs();
    cards_ = std::move(next);
    auto indexOf = [&](HWND window) {
        auto item = std::find_if(cards_.begin(), cards_.end(), [&](const Card& c) { return c.window == window; });
        return item != cards_.end() ? int(item - cards_.begin()) : -1;
    };
    hover_ = indexOf(hovered);
    pressed_ = indexOf(pressed);
    if (hover_ < 0)
        overClose_ = false;
    if (cards_.empty()) {
        Close();
        return;
    }
    for (Card& c : cards_) {
        if (!c.thumb && FAILED(DwmRegisterThumbnail(surface_.Hwnd(), c.window, &c.thumb)))
            c.thumb = nullptr;
        c.source = SIZE{1, 1};
        if (c.thumb)
            DwmQueryThumbnailSourceSize(c.thumb, &c.source);
        // The window as it looks: without the invisible resize borders around it.
        RECT win{}, frame{};
        GetWindowRect(c.window, &win);
        c.sourceRect = RECT{0, 0, c.source.cx, c.source.cy};
        if (!IsIconic(c.window) && SUCCEEDED(DwmGetWindowAttribute(c.window, DWMWA_EXTENDED_FRAME_BOUNDS, &frame, sizeof(frame))) &&
            win.right > win.left) {
            // The source size is in the window's own (physical) px, as the rectangles are.
            c.sourceRect = RECT{frame.left - win.left, frame.top - win.top, frame.right - win.left, frame.bottom - win.top};
            c.sourceRect.right = std::min<LONG>(c.sourceRect.right, c.source.cx);
            c.sourceRect.bottom = std::min<LONG>(c.sourceRect.bottom, c.source.cy);
        }
    }
}

void WindowPreview::Show(const std::wstring& key, const std::vector<HWND>& windows, POINT anchor, const IconImage& icon)
{
    KillTimer(surface_.Hwnd(), kLeaveTimer);
    const bool visible = surface_.Visible();
    const bool same = visible && key == key_;
    key_ = key;
    anchor_ = anchor;
    icon_ = icon;
    SetWindows(windows);
    if (cards_.empty())
        return;
    if (!same) {
        hover_ = pressed_ = -1;
        // Reusing the open surface does not send onReady again. Keep its fade
        // progress when switching apps instead of leaving the new picture invisible.
        if (!visible)
            thumbAlpha_ = 0;
    }
    Place();
    if (!same)
        Log(L"preview: %zu window(s) of %s", cards_.size(), key_.c_str());
}

void WindowPreview::Refresh(const std::vector<TaskItem>& items)
{
    if (!surface_.Visible())
        return;
    auto item = std::find_if(items.begin(), items.end(), [&](const TaskItem& task) { return task.key == key_; });
    if (item == items.end()) {
        Close();
        return;
    }
    SetWindows(item->windows);
    if (!cards_.empty())
        Place();
}

void WindowPreview::Layout(float scale, float maxWidthDip)
{
    (void)scale;
    // Each picture keeps its window's proportions at one height; a long row shrinks to fit the screen.
    float imageH = kImageH;
    auto widthOf = [&](const Card& c, float h) {
        float w = float(std::max<LONG>(1, c.sourceRect.right - c.sourceRect.left)), hh = float(std::max<LONG>(1, c.sourceRect.bottom - c.sourceRect.top));
        return std::clamp(h * w / hh, kMinImageW * h / kImageH, kMaxImageW * h / kImageH);
    };
    auto total = [&](float h) {
        float sum = 2 * kPad + float(cards_.size() - 1) * kGap;
        for (const Card& c : cards_)
            sum += widthOf(c, h) + 2 * kCardPad;
        return sum;
    };
    while (imageH > 48 && total(imageH) > maxWidthDip)
        imageH -= 4;
    float x = kPad;
    for (Card& c : cards_) {
        float w = widthOf(c, imageH) + 2 * kCardPad;
        c.rect = D2D1::RectF(x, kPad, x + w, kPad + kCardPad + kTitleH + imageH + kCardPad);
        c.image = D2D1::RectF(c.rect.left + kCardPad, c.rect.top + kCardPad + kTitleH, c.rect.right - kCardPad, c.rect.bottom - kCardPad);
        c.close = D2D1::RectF(c.rect.right - kCardPad - 24, c.rect.top + kCardPad + 1, c.rect.right - kCardPad, c.rect.top + kCardPad + 25);
        x += w + kGap;
    }
    size_ = D2D1::SizeF(x - kGap + kPad, kPad + kCardPad + kTitleH + imageH + kCardPad + kPad);
}

void WindowPreview::Place()
{
    RECT probe{anchor_.x, anchor_.y, anchor_.x + 1, anchor_.y + 1};
    const float s = PopupScale(probe);
    MONITORINFO mi{sizeof(mi)};
    GetMonitorInfoW(MonitorFromPoint(anchor_, MONITOR_DEFAULTTONEAREST), &mi);
    const RECT& work = mi.rcWork;
    Layout(s, float(work.right - work.left) / s - 24);
    LONG w = LONG(std::lround(size_.width * s)), h = LONG(std::lround(size_.height * s));
    LONG gap = LONG(std::lround(kPopupGapDip * s));
    LONG x = std::clamp<LONG>(anchor_.x - w / 2, work.left + gap, std::max<LONG>(work.left + gap, work.right - gap - w));
    LONG y = std::max<LONG>(work.top, anchor_.y - gap - h);
    RECT r{x, y, x + w, y + h};
    if (surface_.Visible())
        surface_.Move(r);
    else
        surface_.Show(r, false, POINT{anchor_.x, anchor_.y});
    surface_.Invalidate();
}

void WindowPreview::Leave()
{
    if (surface_.Visible())
        SetTimer(surface_.Hwnd(), kLeaveTimer, kLeaveMs, nullptr);
}

void WindowPreview::Close()
{
    KillTimer(surface_.Hwnd(), kLeaveTimer);
    KillTimer(surface_.Hwnd(), kFadeTimer);
    DropThumbs();
    cards_.clear();
    key_.clear();
    hover_ = pressed_ = -1;
    overClose_ = false;
    inside_ = false;
    if (surface_.Visible())
        surface_.Hide();
}

void WindowPreview::UpdateThumbs()
{
    const float s = surface_.Scale();
    for (Card& c : cards_) {
        if (!c.thumb)
            continue;
        // Fitted into the picture's place, centred, in the window's client px.
        float sw = float(std::max<LONG>(1, c.sourceRect.right - c.sourceRect.left)), sh = float(std::max<LONG>(1, c.sourceRect.bottom - c.sourceRect.top));
        float bw = c.image.right - c.image.left, bh = c.image.bottom - c.image.top;
        float k = std::min(bw / sw, bh / sh), dw = sw * k, dh = sh * k;
        float l = c.image.left + (bw - dw) / 2, t = c.image.top + (bh - dh) / 2;
        POINT tl = surface_.ContentToScreen(POINT{LONG(std::lround(l * s)), LONG(std::lround(t * s))});
        ScreenToClient(surface_.Hwnd(), &tl);
        DWM_THUMBNAIL_PROPERTIES p{};
        p.dwFlags = DWM_TNP_RECTDESTINATION | DWM_TNP_RECTSOURCE | DWM_TNP_VISIBLE | DWM_TNP_OPACITY | DWM_TNP_SOURCECLIENTAREAONLY;
        p.rcDestination = RECT{tl.x, tl.y, tl.x + LONG(std::lround(dw * s)), tl.y + LONG(std::lround(dh * s))};
        p.rcSource = c.sourceRect;
        p.fVisible = thumbAlpha_ > 0.01f; // DWM can also show the last frame of a minimized window
        p.opacity = BYTE(std::lround(255 * thumbAlpha_));
        p.fSourceClientAreaOnly = FALSE;
        DwmUpdateThumbnailProperties(c.thumb, &p);
    }
}

void WindowPreview::Paint(Canvas& c)
{
    const Theme& t = c.theme;
    RECT rr = surface_.Rect();
    const float w = float(rr.right - rr.left) / c.scale, h = float(rr.bottom - rr.top) / c.scale;
    if (c.panel)
        c.Fill(D2D1::RectF(0, 0, w, h), kRadius, t.panel);
    for (size_t i = 0; i < cards_.size(); ++i) {
        Card& card = cards_[i];
        float hv = card.hover.Step(int(i) == hover_, c.now);
        if (card.hover.moving)
            c.Continue();
        if (hv > 0.001f)
            c.Fill(card.rect, kCardR, Alpha(t.hover, hv));
        if (int(i) == pressed_)
            c.Fill(card.rect, kCardR, t.press);
        // The title row: the app's icon, the window's title, the close button with the pointer on the card.
        float ty = card.rect.top + kCardPad, cy = ty + kTitleH / 2 - 2;
        float textLeft = card.rect.left + kCardPad + 2;
        if (!icon_.empty()) {
            c.Image(L"preview|" + key_, icon_, D2D1::RectF(textLeft, cy - 8, textLeft + 16, cy + 8));
            textLeft += 22;
        }
        float textRight = card.rect.right - kCardPad - (hv > 0.01f ? 28 : 2);
        c.Text(card.title.empty() ? Tr(L"Windows") : card.title, D2D1::RectF(textLeft, ty - 2, textRight, ty + kTitleH - 2), 12.5f, t.text,
               DWRITE_FONT_WEIGHT_MEDIUM);
        if (hv > 0.01f) {
            D2D1_COLOR_F plate = int(i) == hover_ && overClose_ ? D2D1::ColorF(0.91f, 0.27f, 0.23f, 0.95f * hv) : Alpha(t.hover, hv);
            c.Fill(card.close, 12, plate);
            c.Glyph(L"", card.close, 10, int(i) == hover_ && overClose_ ? D2D1::ColorF(1, 1, 1, hv) : Alpha(t.text, hv));
        }
        // Use the app's icon only when DWM could not provide a thumbnail.
        if (!card.thumb) {
            c.Fill(card.image, kCardR - 4, t.card);
            float ic = std::min(48.f, (card.image.bottom - card.image.top) * 0.5f), mx = (card.image.left + card.image.right) / 2,
                  my = (card.image.top + card.image.bottom) / 2;
            if (!icon_.empty())
                c.Image(L"preview|" + key_, icon_, D2D1::RectF(mx - ic / 2, my - ic / 2, mx + ic / 2, my + ic / 2), 0.9f);
        }
    }
    UpdateThumbs();
}

int WindowPreview::CardAt(POINT pt) const
{
    float s = surface_.Scale(), x = float(pt.x) / s, y = float(pt.y) / s;
    for (size_t i = 0; i < cards_.size(); ++i) {
        const D2D1_RECT_F& r = cards_[i].rect;
        if (x >= r.left && x < r.right && y >= r.top && y < r.bottom)
            return int(i);
    }
    return -1;
}

bool WindowPreview::Message(UINT m, WPARAM w, LPARAM l, LRESULT& result)
{
    POINT pt{GET_X_LPARAM(l), GET_Y_LPARAM(l)};
    auto inClose = [&](int i) {
        if (i < 0)
            return false;
        float s = surface_.Scale(), x = float(pt.x) / s, y = float(pt.y) / s;
        const D2D1_RECT_F& r = cards_[size_t(i)].close;
        return x >= r.left - 2 && x < r.right + 2 && y >= r.top - 2 && y < r.bottom + 2;
    };
    switch (m) {
    case WM_TIMER:
        if (w == kLeaveTimer) {
            KillTimer(surface_.Hwnd(), kLeaveTimer);
            if (!inside_)
                Close();
            return true;
        }
        if (w == kFadeTimer) {
            thumbAlpha_ = float(std::clamp((ClockMs() - fadeStart_) / kFadeMs, 0.0, 1.0));
            thumbAlpha_ = 1 - (1 - thumbAlpha_) * (1 - thumbAlpha_) * (1 - thumbAlpha_); // ease out
            UpdateThumbs();
            if (thumbAlpha_ >= 0.999f)
                KillTimer(surface_.Hwnd(), kFadeTimer);
            return true;
        }
        return false;
    case WM_MOUSEMOVE: {
        if (!tracking_) {
            TRACKMOUSEEVENT tme{sizeof(tme), TME_LEAVE, surface_.Hwnd(), 0};
            tracking_ = TrackMouseEvent(&tme) != FALSE;
        }
        inside_ = true;
        KillTimer(surface_.Hwnd(), kLeaveTimer);
        int i = CardAt(pt);
        bool c = inClose(i);
        if (i != hover_ || c != overClose_) {
            hover_ = i;
            overClose_ = c;
            surface_.Invalidate();
        }
        return true;
    }
    case WM_MOUSELEAVE:
        tracking_ = false;
        inside_ = false;
        hover_ = pressed_ = -1;
        overClose_ = false;
        surface_.Invalidate();
        Leave();
        return true;
    case WM_LBUTTONDOWN:
    case WM_MBUTTONDOWN:
        pressed_ = CardAt(pt);
        surface_.Invalidate();
        return true;
    case WM_LBUTTONUP:
    case WM_MBUTTONUP: {
        int i = CardAt(pt);
        bool same = i >= 0 && i == pressed_;
        pressed_ = -1;
        surface_.Invalidate();
        if (!same)
            return true;
        HWND target = cards_[size_t(i)].window;
        if (m == WM_MBUTTONUP || inClose(i)) {
            // Queue the same command as a native close button. The task model
            // retires the card only after the window actually closes or hides.
            if (TaskModel::CloseWindow(target))
                Log(L"preview: requested window close for %s (%p)", key_.c_str(), target);
            return true;
        }
        Close();
        TaskModel::ActivateWindow(target);
        if (onActivate)
            onActivate(target);
        return true;
    }
    }
    (void)result;
    return false;
}

} // namespace atrium
