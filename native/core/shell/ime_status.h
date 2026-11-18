// What the foreground window types with (issue #56): the keyboard layout of
// its thread and, for an IME, the conversion state. Read after an event that
// may have changed it — never on a timer: the two control messages cross into
// the foreground thread and may block, so they are only paid when there is a
// reason to ask. ImeWatcher names the events: the foreground changing, the IME
// window's own show/hide/change events, and the keys that switch or alter the
// input method.
#pragma once

#include <windows.h>

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
    DWORD layout = 0;       // the foreground thread's keyboard layout (its KLID)
    bool ime = false;       // the layout is an input method editor
    bool open = false;      // conversion is on (composition, not direct input)
    bool native = false;    // native script mode: Chinese letters, not latin
    bool fullShape = false; // full-width letters
    bool upper = false;     // Shift held or CapsLock on: latin letters are capital
    bool caps = false;      // CapsLock's toggle, read on the UI thread (it latches: always news)
    bool reliable = true;   // both control messages came back; false: this read got nothing
    bool empty() const { return layout == 0; }

    bool Latin() const { return !ime || !open || !native; }

    // What an announcement reacts to: a change of these is worth showing. The
    // case is news only when CapsLock latches; a Shift held for a capital is a
    // typing modifier, not a state, and never announces. The glyph still reads
    // the case if another change announces while Shift happens to be held.
    bool SameAs(const ImeStatus& other) const
    {
        if (layout != other.layout || open != other.open || native != other.native || fullShape != other.fullShape)
            return false;
        return caps == other.caps;
    }
};

// A read that could not ask the IME window, while the layout is the one the
// last complete read saw: the conversion state is still that read's (the
// keyboard cannot have changed it unnoticed — every switch is a layout change
// or a key the IME acts on). Carrying it forward keeps a stalled read from
// reading as an English direct input; `reliable` stays false, so it never
// announces.
inline void ImeCarryForward(ImeStatus& now, const ImeStatus& last)
{
    if (now.reliable || now.empty() || last.empty() || now.layout != last.layout || !now.ime)
        return;
    now.open = last.open;
    now.native = last.native;
    now.fullShape = last.fullShape;
}

// A language's own mark for scripts beyond the conventions below: the first
// character of the language's name in its own script ("Deutsch" -> D,
// "ไทย" -> ไ, "Tiếng Việt" -> T). Nothing here is written for one device: the
// OS answers for whatever language this is.
std::wstring LanguageGlyph(LANGID langid);

// The status icon's big letter: an IME composing its script writes it — 中/あ/가
// for the languages that have a sign of their own, the language's native mark
// (LanguageGlyph) for every other script, and latin "a"/"A" whenever the
// keyboard types latin directly. CapsLock over a native method types uppercase
// Latin for every IME, and Shift held under it gives the script back — the
// letter says what a key would produce.
std::wstring ImeGlyph(const ImeStatus& status);

// One read of the foreground window's state. GetKeyboardLayout answers at once;
// the conversion state comes from the window's default IME window, which
// belongs to the foreground thread and may be hung, so its two control
// messages carry a deadline. `askIme` false reads the identity alone.
ImeStatus QueryImeStatus(HWND foreground, bool askIme = true);

// The events that may have changed the state, as a hook set: the foreground
// changing, the IME window showing, hiding or changing, and the keys that
// switch it (Shift, CapsLock, Win+Space, Ctrl+Space) or hand it a new mode.
// Each posts `message` to `window`; the receiver coalesces and reads. No timer,
// no polling: with nothing switching, nothing is read.
class ImeWatcher {
public:
    ImeWatcher(HWND window, UINT message);
    ~ImeWatcher();
    ImeWatcher(const ImeWatcher&) = delete;
    ImeWatcher& operator=(const ImeWatcher&) = delete;

    // CapsLock's latch, kept from the keyboard hook: a background thread's
    // GetKeyState can stay stale, while the low-level hook sees every press.
    bool CapsLock() const { return caps_; }

private:
    static void CALLBACK OnWinEvent(HWINEVENTHOOK hook, DWORD event, HWND window, LONG object, LONG child, DWORD thread, DWORD time);
    static LRESULT CALLBACK OnKey(int code, WPARAM wparam, LPARAM lparam);
    void Notify();

    HWND window_;
    UINT message_;
    HWINEVENTHOOK foreground_ = nullptr;
    HWINEVENTHOOK ime_ = nullptr;
    HHOOK keyboard_ = nullptr;
    bool caps_ = false;     // CapsLock's latch (seeded once, flipped by its presses)
    bool capsDown_ = false; // the key is held: its repeat does not flip the latch again
};

// What an announcement reacts to. A polled state alone is not news: focusing
// another window brings that window's own layout along without the user
// switching anything, so a foreground change only reseeds the baseline. A
// state that differs from the last announced one announces; CapsLock's latch
// is such a change, a Shift held alone is not (SameAs).
class ImeWatch {
public:
    // True when `status` should announce now; the caller shows state().
    bool Update(HWND window, const ImeStatus& status)
    {
        if (!window || status.empty() || !status.reliable)
            return false;
        if (!window_ || window != window_) {
            window_ = window;
            shown_ = status;
            return false;
        }
        if (status.SameAs(shown_))
            return false;
        shown_ = status;
        return true;
    }

    const ImeStatus& state() const { return shown_; }
    void reset() { *this = ImeWatch{}; }

private:
    HWND window_ = nullptr;
    ImeStatus shown_;
};

} // namespace atrium
