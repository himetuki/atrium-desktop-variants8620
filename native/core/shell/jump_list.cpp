// Jump list files (see jump_list.h): the DestList of an automatic destinations
// file and the categories of a custom destinations file.

#include "shell/jump_list.h"

#include "shell/shell_verbs.h"
#include "log.h"

#include <propsys.h>
#include <shlobj.h>
#include <shlwapi.h>
#include <shobjidl.h>

#include <winrt/base.h>

#include <algorithm>
#include <cstring>
#include <map>
#include <mutex>
#include <thread>

namespace atrium {
namespace {

constexpr BYTE kFooter[4] = {0xAB, 0xFB, 0xBF, 0xBA};
constexpr BYTE kLnkHead[20] = {0x4C, 0x00, 0x00, 0x00, 0x01, 0x14, 0x02, 0x00, 0x00, 0x00,
                               0x00, 0x00, 0xC0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46};

std::vector<BYTE> ReadFileBytes(const std::wstring& path)
{
    std::vector<BYTE> bytes;
    HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
                              FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE)
        return bytes;
    LARGE_INTEGER size{};
    if (GetFileSizeEx(file, &size) && size.QuadPart > 0 && size.QuadPart < (1 << 24)) {
        bytes.resize(size_t(size.QuadPart));
        DWORD got = 0;
        if (!ReadFile(file, bytes.data(), DWORD(bytes.size()), &got, nullptr))
            bytes.clear();
        else
            bytes.resize(got);
    }
    CloseHandle(file);
    return bytes;
}

std::vector<BYTE> ReadStream(IStorage* storage, const wchar_t* name)
{
    std::vector<BYTE> bytes;
    winrt::com_ptr<IStream> stream;
    if (FAILED(storage->OpenStream(name, nullptr, STGM_READ | STGM_SHARE_EXCLUSIVE, 0, stream.put())) || !stream)
        return bytes;
    STATSTG stat{};
    if (FAILED(stream->Stat(&stat, STATFLAG_NONAME)) || stat.cbSize.QuadPart <= 0 || stat.cbSize.QuadPart > (1 << 24))
        return bytes;
    // A stream can return less than the requested size: keep reading.
    bytes.resize(size_t(stat.cbSize.QuadPart));
    size_t filled = 0;
    while (filled < bytes.size()) {
        ULONG got = 0;
        if (FAILED(stream->Read(bytes.data() + filled, ULONG(bytes.size() - filled), &got)) || got == 0)
            break;
        filled += got;
    }
    bytes.resize(filled);
    return bytes;
}

template <typename T>
T Read(const std::vector<BYTE>& bytes, size_t at)
{
    T value{};
    if (at + sizeof(T) <= bytes.size())
        std::memcpy(&value, bytes.data() + at, sizeof(T));
    return value;
}

size_t FindBytes(const std::vector<BYTE>& data, size_t from, const BYTE* pattern, size_t size)
{
    if (size == 0 || data.size() < size)
        return std::wstring::npos;
    for (size_t i = from; i + size <= data.size(); ++i) {
        if (std::memcmp(data.data() + i, pattern, size) == 0)
            return i;
    }
    return std::wstring::npos;
}

// PKEY_AppUserModel_ID; the shell's property key headers are kept out of this file.
std::wstring ShellDisplayName(const std::wstring& parsingName, SIGDN kind)
{
    winrt::com_ptr<IShellItem> item;
    if (FAILED(SHCreateItemFromParsingName(parsingName.c_str(), nullptr, IID_PPV_ARGS(item.put()))) || !item)
        return {};
    PWSTR text = nullptr;
    std::wstring out;
    if (SUCCEEDED(item->GetDisplayName(kind, &text)) && text) {
        out = text;
        CoTaskMemFree(text);
    }
    return out;
}

std::wstring BaseName(const std::wstring& path)
{
    const size_t slash = path.find_last_of(L"\\/");
    return slash == std::wstring::npos ? path : path.substr(slash + 1);
}

bool IsRemoteShare(const std::wstring& path)
{
    if (path.size() >= 2 && path[0] == L'\\' && path[1] == L'\\')
        return true; // \\server\share
    if (path.size() >= 3 && path[1] == L':' && (path[2] == L'\\' || path[2] == L'/')) {
        const wchar_t root[4] = {path[0], L':', L'\\', 0};
        const UINT type = GetDriveTypeW(root);
        return type == DRIVE_REMOTE || type == DRIVE_NO_ROOT_DIR;
    }
    return false;
}

bool IsDirectory(const std::wstring& path)
{
    if (IsRemoteShare(path))
        return false; // never wait on an unreachable server
    const DWORD attributes = GetFileAttributesW(path.c_str());
    return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
}

// The strings a shell link carries (the menu shows its name, not the file name).
struct LinkStrings {
    std::wstring name;
    std::wstring relative;
    std::wstring workDir;
    std::wstring args;
};

bool ParseLinkStrings(const BYTE* data, size_t size, LinkStrings& out)
{
    if (size < 0x4C)
        return false;
    uint32_t headerSize = 0;
    std::memcpy(&headerSize, data, 4);
    if (headerSize != 0x4C)
        return false;
    uint32_t flags = 0;
    std::memcpy(&flags, data + 20, 4);
    size_t at = 0x4C;
    if (flags & 0x1) { // an item id list comes first, with its own size
        if (at + 2 > size)
            return false;
        uint16_t length = 0;
        std::memcpy(&length, data + at, 2);
        at += 2 + length;
    }
    if (flags & 0x2) { // then the link info block
        if (at + 4 > size)
            return false;
        uint32_t length = 0;
        std::memcpy(&length, data + at, 4);
        at += length;
    }
    const bool unicode = (flags & 0x80) != 0;
    const auto read = [&](std::wstring& into) -> bool {
        if (at + 2 > size)
            return false;
        uint16_t count = 0;
        std::memcpy(&count, data + at, 2);
        at += 2;
        if (unicode) {
            if (at + size_t(count) * 2 > size)
                return false;
            into.assign(reinterpret_cast<const wchar_t*>(data + at), count);
            at += size_t(count) * 2;
        } else {
            if (at + count > size)
                return false;
            for (uint16_t i = 0; i < count; ++i)
                into.push_back(wchar_t(data[at + i]));
            at += count;
        }
        return true;
    };
    if ((flags & 0x4) && !read(out.name))
        return false;
    if ((flags & 0x8) && !read(out.relative))
        return false;
    if ((flags & 0x10) && !read(out.workDir))
        return false;
    if ((flags & 0x20) && !read(out.args))
        return false;
    return true;
}

// One entry of the DestList stream (Windows 10/11 layout, version 2 and later).
struct DestEntry {
    std::wstring path; // the parsing name the DestList stores (may be a shell name)
    std::wstring real; // the file-system path behind it
    int pin = 0;
    long long last = 0;
};

std::vector<DestEntry> ParseDestList(const std::vector<BYTE>& raw)
{
    std::vector<DestEntry> entries;
    if (raw.size() < 32)
        return entries;
    const int version = Read<int>(raw, 0);
    size_t at = 32;
    while (at + 130 <= raw.size()) {
        short pathChars = Read<short>(raw, at + 128);
        if (pathChars <= 0)
            break;
        const size_t recordSize = 128 + 2 + size_t(pathChars) * 2;
        if (at + recordSize + 4 > raw.size()) {
            Log(L"jump: DestList stops at %zu (chars=%d, %zu bytes)", at, int(pathChars), raw.size());
            break;
        }
        DestEntry entry;
        entry.path.assign(reinterpret_cast<const wchar_t*>(raw.data() + at + 130), size_t(pathChars));
        entry.real = entry.path; // "knownfolder:{GUID}" and "::{GUID}" are shell names
        entry.pin = Read<int>(raw, at + 108);
        entry.last = Read<long long>(raw, at + 100);
        const int store = Read<int>(raw, at + recordSize);
        if (store < 0 || at + recordSize + 4 + size_t(store) > raw.size()) {
            Log(L"jump: DestList stops at %zu (store=%d, %zu bytes)", at, store, raw.size());
            break;
        }
        if (!entry.path.empty())
            entries.push_back(std::move(entry));
        at += recordSize + 4 + size_t(store < 0 ? 0 : store);
        if (version == 1 && entries.size() > 10000)
            break;
    }
    int pinnedCount = 0;
    for (const DestEntry& entry : entries)
        if (entry.pin > 0)
            ++pinnedCount;
    Log(L"jump: destlist %zu entries, %d pinned", entries.size(), pinnedCount);
    return entries;
}

// One category of a custom destinations file.
struct CustomCategory {
    int type = 0; // 0 = custom (named), 1 = known (frequent/recent), 2 = tasks
    int known = 0;
    std::wstring title;
    std::vector<JumpItem> items;
};

// PKEY_Title, declared here so the shell's property key headers stay out.
const PROPERTYKEY kTitle = {{0xF29F85E0, 0x4FF9, 0x1068, {0xAB, 0x91, 0x08, 0x00, 0x2B, 0x27, 0xB3, 0xD9}}, 2};

std::wstring CacheFolder()
{
    PWSTR base = nullptr;
    std::wstring folder;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &base)) && base) {
        folder = std::wstring(base) + L"\\Atrium\\cache\\jumplist";
        CoTaskMemFree(base);
    }
    return folder;
}

// The title the system menu shows for a link that carries no name string. The
// link's own property store holds it; the shell reads that format, so the link
// is written once to a cache file and asked for its title.
std::wstring LinkTitleFromShell(const std::vector<BYTE>& bytes)
{
    if (bytes.empty())
        return {};
    const std::wstring folder = CacheFolder();
    if (folder.empty())
        return {};
    ULONGLONG hash = 0xcbf29ce484222325ull;
    for (BYTE byte : bytes) {
        hash ^= byte;
        hash *= 0x100000001b3ull;
    }
    wchar_t name[32]{};
    swprintf_s(name, L"%016llX.lnk", hash);
    const std::wstring file = folder + L"\\" + name;
    if (GetFileAttributesW(file.c_str()) == INVALID_FILE_ATTRIBUTES) {
        SHCreateDirectoryExW(nullptr, folder.c_str(), nullptr);
        HANDLE handle = CreateFileW(file.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (handle == INVALID_HANDLE_VALUE)
            return {};
        DWORD written = 0;
        WriteFile(handle, bytes.data(), DWORD(bytes.size()), &written, nullptr);
        CloseHandle(handle);
    }
    winrt::com_ptr<IPropertyStore> store;
    if (FAILED(SHGetPropertyStoreFromParsingName(file.c_str(), nullptr, GPS_DEFAULT, IID_PPV_ARGS(store.put()))) || !store)
        return {};
    PROPVARIANT value;
    PropVariantInit(&value);
    std::wstring title;
    if (SUCCEEDED(store->GetValue(kTitle, &value)) && value.vt == VT_LPWSTR && value.pwszVal)
        title = value.pwszVal;
    PropVariantClear(&value);
    return title;
}

bool LoadShellLink(const BYTE* data, size_t size, JumpItem& item)
{
    winrt::com_ptr<IStream> stream;
    stream.attach(SHCreateMemStream(data, UINT(size)));
    if (!stream)
        return false;
    winrt::com_ptr<IPersistStream> persist;
    if (FAILED(CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(persist.put()))) || !persist)
        return false;
    if (FAILED(persist->Load(stream.get())))
        return false;
    winrt::com_ptr<IShellLinkW> link;
    if (FAILED(persist->QueryInterface(IID_PPV_ARGS(link.put()))) || !link)
        return false;
    wchar_t target[1024]{};
    wchar_t args[1024]{};
    link->GetPath(target, 1024, nullptr, SLGP_RAWPATH);
    link->GetArguments(args, 1024);
    item.program = target;
    item.args = args;
    LinkStrings strings;
    ParseLinkStrings(data, size, strings);
    item.name = strings.name;
    if (item.name.empty()) {
        const std::vector<BYTE> blob(data, data + size);
        item.name = LinkTitleFromShell(blob);
    }
    return true;
}

std::vector<CustomCategory> ParseCustom(const std::vector<BYTE>& raw)
{
    std::vector<CustomCategory> categories;
    if (raw.size() < 24) // 12 byte header + an empty category and its footer
        return categories;
    size_t chunkStart = 0;
    while (chunkStart < raw.size()) {
        const size_t footer = FindBytes(raw, chunkStart, kFooter, 4);
        if (footer == std::wstring::npos)
            break;
        const size_t chunkSize = footer - chunkStart + 4;
        const size_t categoryAt = chunkStart == 0 ? 12 : 0;
        CustomCategory category;
        category.type = Read<int>(raw, chunkStart + categoryAt);
        size_t cursor = categoryAt + 4;
        if (category.type == 0) {
            const short nameChars = Read<short>(raw, chunkStart + cursor);
            cursor += 2;
            if (nameChars > 0 && chunkStart + cursor + size_t(nameChars) * 2 <= raw.size())
                category.title.assign(reinterpret_cast<const wchar_t*>(raw.data() + chunkStart + cursor), size_t(nameChars));
        } else if (category.type == 1) {
            category.known = Read<int>(raw, chunkStart + cursor);
        }
        // The links are shell link records; a 16 byte class id precedes each one.
        size_t at = chunkStart + categoryAt;
        while (true) {
            const size_t start = FindBytes(raw, at, kLnkHead, sizeof(kLnkHead));
            if (start == std::wstring::npos || start >= footer)
                break;
            const size_t next = FindBytes(raw, start + 4, kLnkHead, sizeof(kLnkHead));
            size_t end = (next == std::wstring::npos || next > footer) ? footer : next;
            if (next != std::wstring::npos && next <= footer && end - 16 > start)
                end -= 16;
            JumpItem item;
            if (LoadShellLink(raw.data() + start, end - start, item) && !item.program.empty()) {
                if (item.name.empty())
                    item.name = BaseName(item.program);
                item.target = item.program;
                item.folder = false;
                category.items.push_back(std::move(item));
            }
            at = start + 4;
        }
        categories.push_back(std::move(category));
        chunkStart += chunkSize;
    }
    return categories;
}

std::wstring RecentFolder()
{
    PWSTR path = nullptr;
    std::wstring out;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Recent, 0, nullptr, &path)) && path) {
        out = path;
        CoTaskMemFree(path);
    }
    return out;
}

JumpItem ItemForPath(const std::wstring& parsingName)
{
    JumpItem item;
    std::wstring path = parsingName;
    if (path.rfind(L"knownfolder:", 0) == 0 || path.rfind(L"::", 0) == 0) {
        // A known folder or shell namespace item: the shell knows its real path and name.
        item.name = ShellDisplayName(parsingName, SIGDN_NORMALDISPLAY);
        const std::wstring resolved = ShellDisplayName(parsingName, SIGDN_DESKTOPABSOLUTEPARSING);
        if (!resolved.empty())
            path = resolved;
    }
    if (item.name.empty())
        item.name = BaseName(path);
    item.target = path;
    item.folder = IsDirectory(path);
    return item;
}


// ---------------------------------------------------------------------------
// File index
//
// A jump list file is named after a hash of the AppUserModelID that the shell
// never publishes, so the only way to know which file belongs to which app is
// to read them. That is done once, off the UI thread: the click then looks the
// program up in the index instead of walking the profile.

struct FileIndex {
    std::mutex lock;
    bool ready = false;
    bool building = false;
    ULONGLONG builtAt = 0;
    std::map<std::wstring, std::wstring> byExe;  // lower-case program -> file hash
    std::map<std::wstring, std::wstring> byPath; // lower-case item path -> file hash
};

std::wstring Lower(std::wstring text)
{
    for (wchar_t& c : text)
        c = wchar_t(towlower(c));
    return text;
}

FileIndex& Index()
{
    static FileIndex index;
    return index;
}

void BuildIndex()
{
    const std::wstring recent = RecentFolder();
    if (recent.empty())
        return;
    std::map<std::wstring, std::wstring> byExe;
    std::map<std::wstring, std::wstring> byPath;

    WIN32_FIND_DATAW found{};
    HANDLE search = FindFirstFileW((recent + L"\\CustomDestinations\\*.customDestinations-ms").c_str(), &found);
    if (search != INVALID_HANDLE_VALUE) {
        do {
            const std::wstring hash = std::wstring(found.cFileName).substr(0, 16);
            const auto bytes = ReadFileBytes(recent + L"\\CustomDestinations\\" + found.cFileName);
            for (const CustomCategory& category : ParseCustom(bytes))
                for (const JumpItem& item : category.items)
                    if (!item.program.empty())
                        byExe[Lower(item.program)] = hash;
        } while (FindNextFileW(search, &found));
        FindClose(search);
    }

    search = FindFirstFileW((recent + L"\\AutomaticDestinations\\*.automaticDestinations-ms").c_str(), &found);
    if (search != INVALID_HANDLE_VALUE) {
        do {
            const std::wstring hash = std::wstring(found.cFileName).substr(0, 16);
            winrt::com_ptr<IStorage> storage;
            if (FAILED(StgOpenStorage((recent + L"\\AutomaticDestinations\\" + found.cFileName).c_str(), nullptr,
                                      STGM_READ | STGM_SHARE_DENY_WRITE, nullptr, 0, storage.put())) ||
                !storage)
                continue;
            for (const DestEntry& entry : ParseDestList(ReadStream(storage.get(), L"DestList"))) {
                std::wstring path = entry.real;
                if (path.rfind(L"knownfolder:", 0) == 0 || path.rfind(L"::", 0) == 0) {
                    const std::wstring resolved = ShellDisplayName(path, SIGDN_DESKTOPABSOLUTEPARSING);
                    if (resolved.empty())
                        continue;
                    path = resolved;
                }
                byPath[Lower(path)] = hash;
            }
        } while (FindNextFileW(search, &found));
        FindClose(search);
    }

    FileIndex& index = Index();
    std::lock_guard<std::mutex> guard(index.lock);
    index.byExe = std::move(byExe);
    index.byPath = std::move(byPath);
    index.ready = true;
    index.builtAt = GetTickCount64();
    Log(L"jump list: indexed %zu programs, %zu items", index.byExe.size(), index.byPath.size());
}

// The app's file, from the index alone: by its program, else by any of its
// recent items. Empty while the index is still being built.
std::wstring FindIndexedHash(const std::wstring& appId, const std::wstring& exePath)
{
    FileIndex& index = Index();
    std::lock_guard<std::mutex> guard(index.lock);
    if (!index.ready)
        return {};
    if (!exePath.empty()) {
        auto it = index.byExe.find(Lower(exePath));
        if (it != index.byExe.end())
            return it->second;
    }
    if (!appId.empty()) {
        for (const ShellRecent& entry : ShellDocumentList(appId, 0 /*ADLT_RECENT*/, 16)) {
            auto it = index.byPath.find(Lower(entry.path));
            if (it != index.byPath.end())
                return it->second;
        }
    }
    return {};
}

} // namespace

bool IsRemotePath(const std::wstring& path)
{
    return IsRemoteShare(path);
}

void StartJumpListIndex(bool enabled)
{
    if (!enabled)
        return; // the menus that read the index are off
    {
        FileIndex& index = Index();
        std::lock_guard<std::mutex> guard(index.lock);
        if (index.building)
            return;
        index.building = true;
    }
    std::thread([] {
        // A worker without a message pump must not own an STA: the UI thread's
        // shell calls would have to marshal into it and wait for a timeout.
        const HRESULT com = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        try {
            BuildIndex();
        } catch (...) {
            Log(L"jump list: index failed");
        }
        if (SUCCEEDED(com))
            CoUninitialize();
        FileIndex& index = Index();
        std::lock_guard<std::mutex> guard(index.lock);
        index.building = false; // also after a failed build
    }).detach();
}

std::shared_ptr<const JumpListData> LoadJumpList(const std::wstring& appId, const std::wstring& exePath)
{
    static std::map<std::wstring, std::wstring> hashByAppId;
    std::wstring hash;
    const std::wstring cacheKey = appId + L"|" + exePath;
    auto cached = hashByAppId.find(cacheKey);
    if (cached != hashByAppId.end()) {
        hash = cached->second;
    } else {
        hash = FindIndexedHash(appId, exePath);
        Log(L"jump list: '%s' -> %s", exePath.c_str(), hash.empty() ? L"(none)" : hash.c_str());
        if (!hash.empty())
            hashByAppId[cacheKey] = hash;
    }
    if (hash.empty()) {
        // Not found: the index may be older than the app's first jump list (or still being built).
        // Indexed again in the background, at most once a minute; a later click finds it.
        bool stale = false;
        {
            FileIndex& index = Index();
            std::lock_guard<std::mutex> guard(index.lock);
            stale = index.ready && GetTickCount64() - index.builtAt > 60000;
        }
        if (stale)
            StartJumpListIndex(true);
        return nullptr;
    }

    const std::wstring recent = RecentFolder();
    auto data = std::make_shared<JumpListData>();

    // Automatic destinations: the MRU list, with the pinned entries marked.
    std::vector<DestEntry> entries;
    {
        winrt::com_ptr<IStorage> storage;
        const std::wstring path = recent + L"\\AutomaticDestinations\\" + hash + L".automaticDestinations-ms";
        if (SUCCEEDED(StgOpenStorage(path.c_str(), nullptr, STGM_READ | STGM_SHARE_DENY_WRITE, nullptr, 0,
                                     storage.put())) &&
            storage)
            entries = ParseDestList(ReadStream(storage.get(), L"DestList"));
        Log(L"jump: parsed %zu entries from %s", entries.size(), hash.c_str());
    }
    std::vector<DestEntry> pinnedEntries;
    std::vector<DestEntry> otherEntries;
    for (DestEntry& entry : entries) {
        if (entry.path.empty())
            continue;
        if (entry.real.rfind(L"knownfolder:", 0) == 0 || entry.real.rfind(L"::", 0) == 0) {
            const std::wstring resolved = ShellDisplayName(entry.real, SIGDN_DESKTOPABSOLUTEPARSING);
            if (resolved.empty()) {
                Log(L"jump: shell name '%s' does not resolve", entry.real.c_str());
                continue;
            }
            entry.real = resolved;
        }
        const bool shellItem = entry.real.rfind(L"::", 0) == 0;
        // A network item is kept without asking: an offline share answers in
        // tens of seconds, and the entry is the system's own recent item.
        if (!shellItem && !IsRemoteShare(entry.real) && GetFileAttributesW(entry.real.c_str()) == INVALID_FILE_ATTRIBUTES) {
            if (entry.pin > 0)
                Log(L"jump: pinned %d dropped '%s'", entry.pin, entry.real.c_str());
            continue; // a drive that is gone: opening it would land on Documents
        }
        if (entry.pin > 0)
            Log(L"jump: pinned %d '%s' -> '%s'", entry.pin, entry.path.c_str(), entry.real.c_str());
        (entry.pin > 0 ? pinnedEntries : otherEntries).push_back(std::move(entry));
    }
    std::stable_sort(pinnedEntries.begin(), pinnedEntries.end(),
                     [](const DestEntry& a, const DestEntry& b) { return a.pin < b.pin; });
    for (const DestEntry& entry : pinnedEntries)
        data->pinned.push_back(ItemForPath(entry.path));

    // The automatic list's unpinned entries: what a "recent" or "frequent"
    // category shows, and the fallback when the app has no such category.
    const auto recentItems = [&](size_t max) {
        std::vector<JumpItem> items;
        for (const DestEntry& entry : otherEntries) {
            if (items.size() >= max)
                break;
            JumpItem item = ItemForPath(entry.path);
            if (!item.folder && !exePath.empty()) {
                item.program = exePath; // the app opened it, so the app reopens it
                item.args = L"\"" + item.target + L"\"";
            }
            items.push_back(std::move(item));
        }
        return items;
    };

    // Custom destinations: the app's own categories and tasks.
    bool recentCovered = false;
    {
        const std::wstring path = recent + L"\\CustomDestinations\\" + hash + L".customDestinations-ms";
        const auto bytes = ReadFileBytes(path);
        std::vector<JumpCategory> named, known, tasks;
        for (CustomCategory& category : ParseCustom(bytes)) {
            JumpCategory out;
            out.title = category.title;
            out.items = std::move(category.items);
            if (category.type == 1) {
                out.title = category.known == 1 ? L"Frequent" : L"Recent";
                recentCovered = true;
                // A known category is not stored in the file: its rows are the automatic list.
                if (out.items.empty())
                    out.items = recentItems(category.known == 1 ? 5 : 5);
            }
            if (out.items.empty())
                continue;
            (category.type == 2 ? tasks : category.type == 1 ? known : named).push_back(std::move(out));
        }
        for (JumpCategory& category : named)
            data->categories.push_back(std::move(category));
        for (JumpCategory& category : known)
            data->categories.push_back(std::move(category));
        for (JumpCategory& category : tasks) {
            if (category.title.empty())
                category.title = L"Tasks";
            data->categories.push_back(std::move(category));
        }
    }
    // An app that registered categories opted out of the automatic recent list:
    // Windows then shows only what it registered (Chrome lists closed tabs and
    // tasks, not the files it was asked to open).
    if (!recentCovered && data->categories.empty())
        data->recent = recentItems(5);
    return data;
}

} // namespace atrium
