#include "shell/audio_devices.h"

#include "localization.h"

#include "log.h"

#include <windows.h>
#include <audiopolicy.h>
#include <propsys.h>
#include <functiondiscoverykeys_devpkey.h>
#include <mmdeviceapi.h>

#include <winrt/base.h>

#include <algorithm>
#include <cmath>
#include <cwctype>
#include <functional>
#include <map>

#pragma comment(lib, "version.lib")

namespace atrium {

namespace {

// IPolicyConfig (Windows 7 and later): only SetDefaultEndpoint is used; the
// methods before it hold its place in the table.
MIDL_INTERFACE("f8679f50-850a-41cf-9c72-430f290290c8")
IPolicyConfig : public IUnknown
{
    virtual HRESULT STDMETHODCALLTYPE GetMixFormat(PCWSTR, void**) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetDeviceFormat(PCWSTR, INT, void**) = 0;
    virtual HRESULT STDMETHODCALLTYPE ResetDeviceFormat(PCWSTR) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetDeviceFormat(PCWSTR, void*, void*) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetProcessingPeriod(PCWSTR, INT, PINT64, PINT64) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetProcessingPeriod(PCWSTR, PINT64) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetShareMode(PCWSTR, void*) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetShareMode(PCWSTR, void*) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetPropertyValue(PCWSTR, const PROPERTYKEY&, PROPVARIANT*) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetPropertyValue(PCWSTR, const PROPERTYKEY&, PROPVARIANT*) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetDefaultEndpoint(PCWSTR id, ERole role) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetEndpointVisibility(PCWSTR, INT) = 0;
};
// PKEY_AudioEndpoint_FormFactor (mmdeviceapi.h declares it without INITGUID).
constexpr PROPERTYKEY kFormFactor = {{0x1da5d803, 0xd492, 0x4edd, {0x8c, 0x23, 0xe0, 0xc0, 0xff, 0xee, 0x7f, 0x0e}}, 0};
constexpr CLSID kPolicyConfigClient = {0x870af99c, 0x171d, 0x4f9e, {0xaf, 0x0d, 0xe6, 0x3d, 0xf4, 0x0c, 0x2b, 0xc9}};

winrt::com_ptr<IMMDeviceEnumerator> Enumerator()
{
    winrt::com_ptr<IMMDeviceEnumerator> e;
    CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(e.put()));
    return e;
}

std::wstring Prop(IPropertyStore* props, const PROPERTYKEY& key)
{
    std::wstring s;
    PROPVARIANT v;
    PropVariantInit(&v);
    if (SUCCEEDED(props->GetValue(key, &v)) && v.vt == VT_LPWSTR && v.pwszVal)
        s = v.pwszVal;
    PropVariantClear(&v);
    return s;
}

std::wstring Lower(std::wstring s)
{
    for (wchar_t& c : s)
        c = wchar_t(std::towlower(c));
    return s;
}

std::wstring ProcessPath(DWORD pid)
{
    std::wstring path;
    if (HANDLE p = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid)) {
        wchar_t buf[MAX_PATH * 2];
        DWORD n = DWORD(std::size(buf));
        if (QueryFullProcessImageNameW(p, 0, buf, &n))
            path.assign(buf, n);
        CloseHandle(p);
    }
    return path;
}

// The program's own name (its file description), else the file's name.
std::wstring ProgramName(const std::wstring& path)
{
    DWORD handle = 0, size = GetFileVersionInfoSizeW(path.c_str(), &handle);
    if (size) {
        std::vector<BYTE> data(size);
        struct Lang {
            WORD language, codepage;
        }* langs = nullptr;
        UINT bytes = 0;
        if (GetFileVersionInfoW(path.c_str(), 0, size, data.data()) &&
            VerQueryValueW(data.data(), L"\\VarFileInfo\\Translation", reinterpret_cast<void**>(&langs), &bytes) && bytes >= sizeof(Lang)) {
            wchar_t key[64];
            swprintf_s(key, L"\\StringFileInfo\\%04x%04x\\FileDescription", langs[0].language, langs[0].codepage);
            wchar_t* text = nullptr;
            UINT len = 0;
            if (VerQueryValueW(data.data(), key, reinterpret_cast<void**>(&text), &len) && text && len > 1 && text[0])
                return text;
        }
    }
    size_t slash = path.find_last_of(L"\\/");
    std::wstring file = slash == std::wstring::npos ? path : path.substr(slash + 1);
    size_t dot = file.rfind(L'.');
    return dot == std::wstring::npos ? file : file.substr(0, dot);
}

// Calls `f` with every live session on the default playback device.
void ForEachSession(const std::function<void(IAudioSessionControl2*, ISimpleAudioVolume*)>& f)
{
    auto e = Enumerator();
    winrt::com_ptr<IMMDevice> device;
    winrt::com_ptr<IAudioSessionManager2> manager;
    winrt::com_ptr<IAudioSessionEnumerator> sessions;
    if (!e || FAILED(e->GetDefaultAudioEndpoint(eRender, eConsole, device.put())) ||
        FAILED(device->Activate(__uuidof(IAudioSessionManager2), CLSCTX_INPROC_SERVER, nullptr, manager.put_void())) ||
        FAILED(manager->GetSessionEnumerator(sessions.put())))
        return;
    int n = 0;
    sessions->GetCount(&n);
    for (int i = 0; i < n; ++i) {
        winrt::com_ptr<IAudioSessionControl> control;
        if (FAILED(sessions->GetSession(i, control.put())))
            continue;
        auto control2 = control.try_as<IAudioSessionControl2>();
        auto volume = control.try_as<ISimpleAudioVolume>();
        AudioSessionState state = AudioSessionStateExpired;
        if (!control2 || !volume || FAILED(control->GetState(&state)) || state == AudioSessionStateExpired)
            continue;
        f(control2.get(), volume.get());
    }
}

std::wstring SessionKey(IAudioSessionControl2* s, std::wstring* path = nullptr)
{
    if (s->IsSystemSoundsSession() == S_OK)
        return L"#system";
    DWORD pid = 0;
    if (FAILED(s->GetProcessId(&pid)) || !pid)
        return {};
    std::wstring p = ProcessPath(pid);
    if (path)
        *path = p;
    return p.empty() ? L"#pid:" + std::to_wstring(pid) : Lower(p);
}

} // namespace

std::vector<AudioOutput> ListAudioOutputs()
{
    std::vector<AudioOutput> list;
    try {
        auto e = Enumerator();
        winrt::com_ptr<IMMDeviceCollection> all;
        if (!e || FAILED(e->EnumAudioEndpoints(eRender, DEVICE_STATE_ACTIVE, all.put())))
            return list;
        std::wstring def;
        winrt::com_ptr<IMMDevice> d;
        if (SUCCEEDED(e->GetDefaultAudioEndpoint(eRender, eMultimedia, d.put()))) {
            LPWSTR id = nullptr;
            if (SUCCEEDED(d->GetId(&id)) && id) {
                def = id;
                CoTaskMemFree(id);
            }
        }
        UINT n = 0;
        all->GetCount(&n);
        for (UINT i = 0; i < n; ++i) {
            winrt::com_ptr<IMMDevice> dev;
            winrt::com_ptr<IPropertyStore> props;
            LPWSTR id = nullptr;
            if (FAILED(all->Item(i, dev.put())) || FAILED(dev->GetId(&id)) || !id)
                continue;
            AudioOutput o;
            o.id = id;
            CoTaskMemFree(id);
            o.isDefault = o.id == def;
            if (SUCCEEDED(dev->OpenPropertyStore(STGM_READ, props.put()))) {
                o.name = Prop(props.get(), PKEY_Device_DeviceDesc);
                o.adapter = Prop(props.get(), PKEY_DeviceInterface_FriendlyName);
                if (o.name.empty())
                    o.name = Prop(props.get(), PKEY_Device_FriendlyName);
                PROPVARIANT v;
                PropVariantInit(&v);
                if (SUCCEEDED(props->GetValue(kFormFactor, &v)) && v.vt == VT_UI4) {
                    switch (v.ulVal) {
                    case Speakers:
                    case LineLevel:
                        o.kind = AudioOutput::Speakers;
                        break;
                    case Headphones:
                    case Headset:
                    case Handset:
                        o.kind = AudioOutput::Headphones;
                        break;
                    case DigitalAudioDisplayDevice:
                        o.kind = AudioOutput::Display;
                        break;
                    default:
                        break;
                    }
                }
                PropVariantClear(&v);
            }
            list.push_back(std::move(o));
        }
    } catch (...) {
        Log(L"audio: listing the outputs failed");
    }
    return list;
}

bool SetDefaultOutput(const std::wstring& id)
{
    winrt::com_ptr<IPolicyConfig> policy;
    if (FAILED(CoCreateInstance(kPolicyConfigClient, nullptr, CLSCTX_ALL, __uuidof(IPolicyConfig), policy.put_void()))) {
        Log(L"audio: the policy interface is not there");
        return false;
    }
    bool ok = true;
    for (ERole role : {eConsole, eMultimedia, eCommunications})
        ok = SUCCEEDED(policy->SetDefaultEndpoint(id.c_str(), role)) && ok;
    Log(L"audio: default output set (%s)", ok ? L"ok" : L"failed");
    return ok;
}

std::vector<AudioApp> ListAudioApps(int iconPx)
{
    std::vector<AudioApp> list;
    try {
        std::map<std::wstring, size_t> at;
        ForEachSession([&](IAudioSessionControl2* s, ISimpleAudioVolume* v) {
            std::wstring path;
            std::wstring key = SessionKey(s, &path);
            if (key.empty())
                return;
            float level = 1;
            BOOL mute = FALSE;
            v->GetMasterVolume(&level);
            v->GetMute(&mute);
            AudioSessionState state = AudioSessionStateInactive;
            s->GetState(&state);
            auto it = at.find(key);
            if (it == at.end()) {
                AudioApp a;
                a.key = key;
                a.path = path;
                a.name = key == L"#system" ? Tr(L"System Sounds") : path.empty() ? Tr(L"Apps") : ProgramName(path);
                a.volume = int(std::lround(level * 100));
                a.muted = mute != FALSE;
                a.active = state == AudioSessionStateActive;
                at[key] = list.size();
                list.push_back(std::move(a));
            } else {
                AudioApp& a = list[it->second];
                a.volume = std::max(a.volume, int(std::lround(level * 100)));
                a.muted = a.muted && mute;
                a.active = a.active || state == AudioSessionStateActive;
            }
        });
        std::stable_sort(list.begin(), list.end(), [](const AudioApp& a, const AudioApp& b) {
            bool sa = a.key == L"#system", sb = b.key == L"#system";
            if (sa != sb)
                return sa;
            if (a.active != b.active)
                return a.active;
            return a.name < b.name;
        });
        // Periodic volume reads pass zero to reuse the icons already on screen.
        if (iconPx > 0)
            for (AudioApp& a : list)
                if (!a.path.empty())
                    LoadShellImage(a.path, iconPx, a.icon);
    } catch (...) {
        Log(L"audio: listing the apps failed");
    }
    return list;
}

void SetAppVolume(const std::wstring& key, int percent)
{
    float level = float(std::clamp(percent, 0, 100)) / 100.f;
    ForEachSession([&](IAudioSessionControl2* s, ISimpleAudioVolume* v) {
        if (SessionKey(s) == key) {
            v->SetMasterVolume(level, nullptr);
            if (percent > 0)
                v->SetMute(FALSE, nullptr);
        }
    });
}

void SetAppMute(const std::wstring& key, bool mute)
{
    ForEachSession([&](IAudioSessionControl2* s, ISimpleAudioVolume* v) {
        if (SessionKey(s) == key)
            v->SetMute(mute, nullptr);
    });
}

} // namespace atrium
