// What the foreground window types with (issue #56): the keyboard layout of
// its thread and, for an IME, the Chinese/English conversion state. Read on a
// UI-thread timer. GetKeyboardLayout answers at once; the conversion state
// comes from the window's default IME window, which belongs to the foreground
// thread and may be hung, so its two control messages carry a deadline.
#pragma once

#include <windows.h>
#include <imm.h>

#include <cwctype>
#include <string>

namespace atrium {

// The WM_IME_CONTROL sub-messages the public imm.h leaves out (immdev.h's
// set): asking the foreground thread's default IME window for its state.
#ifndef IMC_GETOPENSTATUS
#define IMC_GETOPENSTATUS 0x0005
#endif
#ifndef IMC_GETCONVERSIONMODE
#define IMC_GETCONVERSIONMODE 0x0001
#endif

struct ImeStatus {
    DWORD layout = 0;    // the foreground thread's keyboard layout (its KLID)
    bool ime = false;    // the layout is an input method editor
    bool open = false;   // conversion is on (composition, not direct input)
    bool native = false; // native script mode: Chinese letters, not latin
    bool fullShape = false; // full-width letters (the moon is not shown yet)
    bool upper = false;  // Shift held or CapsLock on: latin letters are capital
    bool caps = false;   // CapsLock's toggle, read on the UI thread (it latches: always news)
    bool reliable = true; // both control messages came back; false: this poll read nothing
    bool empty() const { return layout == 0; }

    bool Latin() const { return !ime || !open || !native; }

    // What the bubble announces: only a change of these is worth showing. A
    // switch between the branches is already news; the case is news only when
    // CapsLock latches. A Shift held for a capital — and the temporary A/a
    // modes a long press opens, which every IME ends its own way — is a typing
    // modifier, not a state: it never announces, or every capital letter would
    // pop a bubble. The glyph still reads the case if another change announces
    // while Shift happens to be held.
    bool SameAs(const ImeStatus& other) const
    {
        if (layout != other.layout || open != other.open || native != other.native || fullShape != other.fullShape)
            return false;
        return caps == other.caps;
    }

    // A real switch (not just the case of the letters).
    bool SwitchedFrom(const ImeStatus& other) const
    {
        return layout != other.layout || open != other.open || native != other.native || fullShape != other.fullShape;
    }
};

// A poll that could not read the IME window, while the layout still is the one
// the last complete read saw: the conversion state is still that read's (the
// keyboard cannot have changed it unnoticed — every switch is a layout change
// or a key the IME acts on). Carrying it forward keeps a stalled poll from
// reading as an English direct input; `reliable` stays false, so nothing
// announces; the island's input.rs rechecks both after its read.
inline void ImeCarryForward(ImeStatus& now, const ImeStatus& last)
{
    if (now.reliable || now.empty() || last.empty() || now.layout != last.layout || !now.ime)
        return;
    now.open = last.open;
    now.native = last.native;
    now.fullShape = last.fullShape;
}

// A language's own mark for the bubble, for scripts beyond the conventions
// below: the first character of the language's name in its own script
// ("Deutsch" -> D, "ไทย" -> ไ, "Tiếng Việt" -> T). Nothing here is written
// for one device: the OS answers for whatever language this is.
inline std::wstring LanguageGlyph(LANGID langid)
{
    wchar_t locale[LOCALE_NAME_MAX_LENGTH]{};
    if (!LCIDToLocaleName(MAKELCID(langid, SORT_DEFAULT), locale, LOCALE_NAME_MAX_LENGTH, 0))
        return L"A";
    wchar_t name[160]{};
    const DWORD kinds[] = {LOCALE_SNATIVEDISPLAYNAME, LOCALE_SLOCALIZEDDISPLAYNAME};
    for (DWORD kind : kinds)
        if (GetLocaleInfoEx(locale, kind, name, DWORD(sizeof(name) / sizeof(name[0]))) && name[0])
            return std::wstring(1, name[0]);
    // Even the names are missing: the ISO code's first letter, upper-cased.
    if (GetLocaleInfoEx(locale, LOCALE_SISO639LANGNAME, name, DWORD(sizeof(name) / sizeof(name[0]))) && name[0])
        return std::wstring(1, wchar_t(towupper(name[0])));
    return L"A";
}

// The bubble's big letter: an IME composing its script writes it — 中/あ/가
// for the languages that have a sign of their own, the language's native
// mark (LanguageGlyph) for every other script, and latin "a"/"A" whenever the
// keyboard types latin directly. CapsLock over a native method types uppercase
// Latin for every IME, and Shift held under it gives the script back — the
// letter says what a key would produce. Shift alone, however, never starts an
// announcement (a long press only opens a temporary mode); it shows here only
// if some other change announces while it is held.
inline std::wstring ImeGlyph(const ImeStatus& s)
{
    if (s.Latin())
        return s.upper ? L"A" : L"a";
    if (s.caps && s.upper)
        return L"A";
    switch (PRIMARYLANGID(LOWORD(s.layout))) {
    case LANG_CHINESE: return L"中";
    case LANG_JAPANESE: return L"あ";
    case LANG_KOREAN: return L"가";
    default: return LanguageGlyph(LOWORD(s.layout));
    }
}

inline ImeStatus QueryImeStatus(HWND foreground, bool askIme = true)
{
    ImeStatus s;
    if (!foreground)
        return s;
    const DWORD thread = GetWindowThreadProcessId(foreground, nullptr);
    if (!thread)
        return s;
    const HKL hkl = GetKeyboardLayout(thread);
    s.layout = DWORD(reinterpret_cast<uintptr_t>(hkl) & 0xffffffff);
    if (!s.layout)
        return s;
    s.ime = ImmIsIME(hkl) != FALSE;
    if (!askIme) {
        // The identity alone: the two control messages below block the window's
        // thread up to 120 ms each, and a probe on a 60 ms cadence may not pay
        // that every tick while an IME window is known not to answer.
        s.reliable = false;
        return s;
    }
    if (s.ime) {
        if (HWND ime = ImmGetDefaultIMEWnd(foreground)) {
            DWORD_PTR answer = 0;
            if (SendMessageTimeoutW(ime, WM_IME_CONTROL, IMC_GETOPENSTATUS, 0, SMTO_ABORTIFHUNG | SMTO_BLOCK, 120, &answer))
                s.open = answer != 0;
            else
                s.reliable = false; // a stalled IME window answers nothing: not a state
            if (SendMessageTimeoutW(ime, WM_IME_CONTROL, IMC_GETCONVERSIONMODE, 0, SMTO_ABORTIFHUNG | SMTO_BLOCK, 120, &answer)) {
                s.native = (answer & IME_CMODE_NATIVE) != 0;
                s.fullShape = (answer & IME_CMODE_FULLSHAPE) != 0;
            } else {
                s.reliable = false;
            }
        } else {
            s.reliable = false; // no window to ask: not a state, and not a false English either
        }
    }
    // The case is read on the caller's (UI) thread: CapsLock's toggle lives in
    // a thread's key state, and only a thread that pumps messages sees it — this
    // probe thread has no message loop, and GetAsyncKeyState's low bit is merely
    // "pressed since the last poll". See OnImeState.
    return s;
}

// What the announcements react to. A polled state alone is not news: a busy
// IME window may stall one poll, and focusing another window brings that
// window's own layout along without the user switching anything. So a change
// opens a window of its own dwell time (the user's setting, 250 ms by default
// — a case-only change has its own) and the newest state rides it: when the
// window closes, whatever is current announces. A run of quick switches
// therefore announces at its deadline with the latest state instead of being
// swallowed by the next one, and a transient that comes and goes inside the
// window never shows. Shift is not polled for news at all: only CapsLock's
// latch is a case change.
class ImeWatch {
public:
    static constexpr unsigned kDefaultStateMs = 0, kDefaultCaseMs = 250; // a switch is immediate; a CapsLock flip still waits

    // The user's debounces (settings): the dwell of a real switch, and of a
    // case-only change. 0 announces at the first sample.
    void SetSettle(unsigned stateMs, unsigned caseMs)
    {
        stateSettle_ = std::min(stateMs, 2000u);
        caseSettle_ = std::min(caseMs, 2000u);
    }

    // One poll of the window in front and its state, at ClockMs. True when a
    // change should announce now (the caller shows state()).
    bool Sample(HWND window, const ImeStatus& status, double now)
    {
        if (!window || status.empty() || !status.reliable)
            return false; // nothing read: not a state change
        if (!window_ || window != window_) {
            // Another window, with whatever layout it remembers: a new
            // baseline, not an announcement.
            window_ = window;
            shown_ = status;
            pending_ = false;
            return false;
        }
        if (status.SameAs(shown_)) {
            // Back to what was announced before the window closed: a capital's
            // Shift that came and went, or a switch that returned (A -> B -> A),
            // is not news. A run of differing states still announces its newest
            // one at the deadline.
            pending_ = false;
            return false;
        } else {
            if (!pending_) {
                pending_ = true;
                pendingCaseOnly_ = !status.SwitchedFrom(shown_);
                since_ = now;
            } else if (status.SwitchedFrom(shown_)) {
                pendingCaseOnly_ = false; // a real switch rides along: the state dwell
            }
            pendingStatus_ = status; // the newest state rides the window
        }
        if (!pending_)
            return false;
        if (now - since_ < double(pendingCaseOnly_ ? caseSettle_ : stateSettle_))
            return false;
        shown_ = pendingStatus_;
        pending_ = false;
        return true;
    }

    const ImeStatus& state() const { return shown_; }
    void reset() { *this = ImeWatch{}; }

private:
    HWND window_ = nullptr;
    ImeStatus shown_;       // what the last announcement (or window) established
    ImeStatus pendingStatus_; // the newest state riding the window
    bool pending_ = false;
    bool pendingCaseOnly_ = false;
    double since_ = 0;
    unsigned stateSettle_ = kDefaultStateMs, caseSettle_ = kDefaultCaseMs;
};

} // namespace atrium
