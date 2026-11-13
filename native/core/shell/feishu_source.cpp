#include "shell/feishu_source.h"

#include "localization.h"

#include "log.h"
#include "shell/uia_client.h"

#include <tlhelp32.h>
#include <uiautomation.h>

#include <winrt/base.h>

#include <map>
#include <set>
#include <vector>

namespace atrium {

namespace {

constexpr DWORD kReconcileMs = 5000;
constexpr ULONGLONG kProcessesMs = 10000; // Feishu's processes are listed again after this
constexpr ULONGLONG kUnknownMs = 3000;    // or sooner when a popup-like window is not theirs

std::wstring Trim(std::wstring s)
{
    while (!s.empty() && iswspace(s.back()))
        s.pop_back();
    size_t a = s.find_first_not_of(L" \t\r\n");
    return a == std::wstring::npos ? std::wstring() : s.substr(a);
}

// Feishu's processes: id -> program path.
std::map<DWORD, std::wstring> FeishuProcesses()
{
    std::map<DWORD, std::wstring> out;
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE)
        return out;
    PROCESSENTRY32W pe{sizeof(pe)};
    for (BOOL ok = Process32FirstW(snap, &pe); ok; ok = Process32NextW(snap, &pe)) {
        if (_wcsicmp(pe.szExeFile, L"Feishu.exe") != 0 && _wcsicmp(pe.szExeFile, L"Lark.exe") != 0)
            continue;
        std::wstring path;
        if (HANDLE p = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pe.th32ProcessID)) {
            wchar_t buf[MAX_PATH * 2];
            DWORD len = DWORD(std::size(buf));
            if (QueryFullProcessImageNameW(p, 0, buf, &len))
                path = buf;
            CloseHandle(p);
        }
        out[pe.th32ProcessID] = path;
    }
    CloseHandle(snap);
    return out;
}

// Feishu's popups on screen: visible, untitled Chromium windows of at most
// 520 x 260 DIP (its main window and other dialogs are larger or titled) in
// the corner of the work area. `unknown`: such a window of a process not in
// `procs` (Feishu may have started since they were listed).
std::vector<std::pair<HWND, DWORD>> Popups(const std::map<DWORD, std::wstring>& procs, bool& unknown)
{
    struct Ctx {
        const std::map<DWORD, std::wstring>* procs;
        std::vector<std::pair<HWND, DWORD>> out;
        bool unknown = false;
    } ctx{&procs, {}};
    EnumWindows(
        [](HWND h, LPARAM l) -> BOOL {
            auto* c = reinterpret_cast<Ctx*>(l);
            // InternalGetWindowText sends no message: GetWindowText would wait for the window's
            // thread, Atrium's own UI thread too (a deadlock while it waits for this one to end).
            wchar_t title[2] = {};
            if (!IsWindowVisible(h) || IsIconic(h) || InternalGetWindowText(h, title, 2) > 0)
                return TRUE;
            wchar_t cls[64] = {};
            GetClassNameW(h, cls, 64);
            RECT r{};
            GetWindowRect(h, &r);
            float s = float(GetDpiForWindow(h)) / 96.f;
            if (s <= 0)
                s = 1;
            float w = float(r.right - r.left) / s, hh = float(r.bottom - r.top) / s;
            // A message popup sits in the work area's bottom-right corner; Feishu's
            // other small untitled windows (the text-selection toolbar, the update
            // notice) are elsewhere.
            MONITORINFO mi{sizeof(mi)};
            GetMonitorInfoW(MonitorFromWindow(h, MONITOR_DEFAULTTONEAREST), &mi);
            bool corner = float(mi.rcWork.right - r.right) / s <= 48 && float(mi.rcWork.bottom - r.bottom) / s <= 48;
            if (wcscmp(cls, L"Chrome_WidgetWin_1") != 0 || w < 160 || w > 520 || hh < 40 || hh > 260 || !corner)
                return TRUE;
            DWORD pid = 0;
            GetWindowThreadProcessId(h, &pid);
            if (c->procs->count(pid))
                c->out.push_back({h, pid});
            else
                c->unknown = true;
            return TRUE;
        },
        reinterpret_cast<LPARAM>(&ctx));
    unknown = ctx.unknown;
    return ctx.out;
}

// The popup's texts in order, the empty ones left out.
std::vector<std::wstring> Texts(IUIAutomation* uia, IUIAutomationCondition* text, IUIAutomationCacheRequest* cache, HWND h, UiaWatch& watch)
{
    std::vector<std::wstring> out;
    winrt::com_ptr<IUIAutomationElement> root;
    winrt::com_ptr<IUIAutomationElementArray> all;
    if (FAILED(uia->ElementFromHandle(h, root.put())) || !root)
        return out;
    watch.Add(h, root.get());
    if (FAILED(root->FindAllBuildCache(TreeScope_Descendants, text, cache, all.put())) || !all)
        return out;
    int n = 0;
    all->get_Length(&n);
    for (int i = 0; i < n && i < 16; ++i) {
        winrt::com_ptr<IUIAutomationElement> e;
        all->GetElement(i, e.put());
        BSTR name = nullptr;
        if (e && SUCCEEDED(e->get_CachedName(&name)) && name) {
            std::wstring t = Trim(name);
            if (!t.empty())
                out.push_back(t);
        }
        SysFreeString(name);
    }
    return out;
}

} // namespace

FeishuSource::FeishuSource(HWND notify, UINT msg) : notify_(notify), msg_(msg)
{
    wake_ = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    thread_ = std::thread([this] { Run(); });
}

FeishuSource::~FeishuSource()
{
    stop_ = true;
    SetEvent(wake_);
    thread_.join();
    CloseHandle(wake_);
}

void FeishuSource::Run()
{
    winrt::init_apartment(winrt::apartment_type::multi_threaded);
    winrt::com_ptr<IUIAutomationCondition> text;
    winrt::com_ptr<IUIAutomationCacheRequest> cache;
    winrt::com_ptr<IUIAutomation> uia = CreateUia(stop_, wake_, L"feishu", [&](IUIAutomation* u) {
        text = nullptr;
        VARIANT v{};
        v.vt = VT_I4;
        v.lVal = UIA_TextControlTypeId;
        HRESULT hr = u->CreatePropertyCondition(UIA_ControlTypePropertyId, v, text.put());
        if (FAILED(hr))
            return hr;
        cache = nullptr;
        winrt::com_ptr<IUIAutomationCondition> raw;
        if (FAILED(hr = u->CreateCacheRequest(cache.put())) || FAILED(hr = cache->AddProperty(UIA_NamePropertyId)) ||
            FAILED(hr = cache->put_TreeScope(TreeScope_Element)) || FAILED(hr = u->get_RawViewCondition(raw.put())) ||
            FAILED(hr = cache->put_TreeFilter(raw.get())))
            return hr;
        return cache->put_AutomationElementMode(AutomationElementMode_None);
    });
    // Popups already handled, by window and text (a popup may be reused for the next message).
    std::map<HWND, std::wstring> seen;
    bool baseline = false, running = false;
    // Listing every process is the costly part of a pass: done every few seconds, not every pass.
    std::map<DWORD, std::wstring> procs;
    ULONGLONG listed = 0;
    {
        UiaWatch watch(uia.get(), wake_);
        UiaWindowWake windows(wake_, L"Chrome_WidgetWin_1");
        while (!stop_ && uia && text) {
            bool loading = false;
            try {
                watch.Prune();
                ULONGLONG tick = GetTickCount64();
                bool unknown = false;
                auto popups = Popups(procs, unknown);
                if (!listed || tick - listed >= kProcessesMs || (unknown && tick - listed >= kUnknownMs)) {
                    procs = FeishuProcesses();
                    windows.SetProcesses(procs);
                    listed = tick;
                    popups = Popups(procs, unknown);
                    if (procs.empty() != !running) {
                        running = !procs.empty();
                        Log(L"feishu: %s", running ? L"watching Feishu's message popups" : L"Feishu is not running");
                    }
                }
                std::map<HWND, std::wstring> now;
                for (const auto& [h, pid] : popups) {
                    auto old = seen.find(h);
                    const bool firstRead = !watch.Contains(h);
                    std::vector<std::wstring> t = Texts(uia.get(), text.get(), cache.get(), h, watch);
                    if (t.empty()) {
                        // Keep startup popups in the baseline even before their text loads.
                        if (!baseline || old != seen.end())
                            now[h] = old != seen.end() ? old->second : L"";
                        loading = loading || firstRead; // one settling pass; later changes wake through UIA
                        continue; // still loading: read again next time
                    }
                    std::wstring key;
                    for (const auto& s : t)
                        key += s + L"\n";
                    now[h] = key;
                    if (!baseline || (old != seen.end() && (old->second.empty() || old->second == key)))
                        continue;
                    auto* n = new Notice();
                    n->source = L"feishu";
                    n->app = Tr(L"Feishu");
                    n->appId = procs.at(pid);
                    n->title = t[0];
                    for (size_t i = 1; i < t.size(); ++i)
                        n->body += (n->body.empty() ? L"" : L" ") + t[i];
                    if (!n->appId.empty())
                        n->action = L"app:" + n->appId;
                    if (!PostMessageW(notify_, msg_, 0, reinterpret_cast<LPARAM>(n)))
                        delete n;
                    else
                        Log(L"feishu: new message popup");
                }
                seen = std::move(now);
                baseline = true; // popups on screen when Atrium started are not new
            } catch (...) {
                Log(L"feishu: reading failed");
            }
            if (!stop_)
                WaitForUiaWake(wake_, loading ? 200 : kReconcileMs);
        }
    }
    text = nullptr;
    cache = nullptr;
    uia = nullptr;
    winrt::uninit_apartment();
}

} // namespace atrium
