#include "ime_status.h"

#include <imm.h>

#include <cwctype>

namespace atrium {

namespace {

ImeWatcher* g_watcher = nullptr; // one watcher, installed on the UI thread

} // namespace

std::wstring LanguageGlyph(LANGID langid)
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

std::wstring ImeGlyph(const ImeStatus& status)
{
    if (status.Latin())
        return status.upper ? L"A" : L"a";
    if (status.caps && status.upper)
        return L"A";
    switch (PRIMARYLANGID(LOWORD(status.layout))) {
    case LANG_CHINESE: return L"中";
    case LANG_JAPANESE: return L"あ";
    case LANG_KOREAN: return L"가";
    default: return LanguageGlyph(LOWORD(status.layout));
    }
}

ImeStatus QueryImeStatus(HWND foreground, bool askIme)
{
    ImeStatus status;
    if (!foreground)
        return status;
    const DWORD thread = GetWindowThreadProcessId(foreground, nullptr);
    if (!thread)
        return status;
    const HKL hkl = GetKeyboardLayout(thread);
    status.layout = DWORD(reinterpret_cast<uintptr_t>(hkl) & 0xffffffff);
    if (!status.layout)
        return status;
    status.ime = ImmIsIME(hkl) != FALSE;
    if (!askIme) {
        status.reliable = false; // the identity alone: the control messages were not asked
        return status;
    }
    if (status.ime) {
        if (HWND ime = ImmGetDefaultIMEWnd(foreground)) {
            DWORD_PTR answer = 0;
            if (SendMessageTimeoutW(ime, WM_IME_CONTROL, IMC_GETOPENSTATUS, 0, SMTO_ABORTIFHUNG | SMTO_BLOCK, 120, &answer))
                status.open = answer != 0;
            else
                status.reliable = false; // a stalled IME window answers nothing: not a state
            if (SendMessageTimeoutW(ime, WM_IME_CONTROL, IMC_GETCONVERSIONMODE, 0, SMTO_ABORTIFHUNG | SMTO_BLOCK, 120, &answer)) {
                status.native = (answer & IME_CMODE_NATIVE) != 0;
                status.fullShape = (answer & IME_CMODE_FULLSHAPE) != 0;
            } else {
                status.reliable = false;
            }
        } else {
            status.reliable = false; // no window to ask: not a state, and not a false English either
        }
    }
    return status;
}

ImeWatcher::ImeWatcher(HWND window, UINT message) : window_(window), message_(message)
{
    g_watcher = this;
    foreground_ = SetWinEventHook(EVENT_SYSTEM_FOREGROUND, EVENT_SYSTEM_FOREGROUND, nullptr, OnWinEvent, 0, 0, WINEVENT_OUTOFCONTEXT);
    ime_ = SetWinEventHook(EVENT_OBJECT_IME_SHOW, EVENT_OBJECT_IME_CHANGE, nullptr, OnWinEvent, 0, 0, WINEVENT_OUTOFCONTEXT);
    keyboard_ = SetWindowsHookExW(WH_KEYBOARD_LL, OnKey, GetModuleHandleW(nullptr), 0);
}

ImeWatcher::~ImeWatcher()
{
    if (keyboard_)
        UnhookWindowsHookEx(keyboard_);
    if (ime_)
        UnhookWinEvent(ime_);
    if (foreground_)
        UnhookWinEvent(foreground_);
    g_watcher = nullptr;
}

void ImeWatcher::Notify()
{
    if (window_)
        PostMessageW(window_, message_, 0, 0);
}

void CALLBACK ImeWatcher::OnWinEvent(HWINEVENTHOOK, DWORD event, HWND, LONG, LONG, DWORD, DWORD)
{
    if (!g_watcher)
        return;
    if (event == EVENT_SYSTEM_FOREGROUND || (event >= EVENT_OBJECT_IME_SHOW && event <= EVENT_OBJECT_IME_CHANGE))
        g_watcher->Notify();
}

LRESULT CALLBACK ImeWatcher::OnKey(int code, WPARAM wparam, LPARAM lparam)
{
    if (code == HC_ACTION && g_watcher && (wparam == WM_KEYDOWN || wparam == WM_SYSKEYDOWN)) {
        const KBDLLHOOKSTRUCT* key = reinterpret_cast<const KBDLLHOOKSTRUCT*>(lparam);
        const DWORD vk = key->vkCode;
        const bool caseKey = vk == VK_SHIFT || vk == VK_LSHIFT || vk == VK_RSHIFT || vk == VK_CAPITAL;
        const bool switchKey = vk == VK_SPACE && ((GetAsyncKeyState(VK_LWIN) | GetAsyncKeyState(VK_RWIN) | GetAsyncKeyState(VK_CONTROL)) & 0x8000) != 0;
        const bool imeKey = vk == VK_KANA || vk == VK_KANJI || vk == VK_HANGUL || vk == VK_HANJA;
        // Atrium's Win key hook replays the Space of Win+Space itself: the Win
        // key is already up when the replay arrives, so the combination above
        // no longer sees it.
        const bool replay = vk == VK_SPACE && (key->flags & LLKHF_INJECTED) != 0;
        if (caseKey || switchKey || imeKey || replay)
            g_watcher->Notify();
    }
    return CallNextHookEx(nullptr, code, wparam, lparam);
}

} // namespace atrium
