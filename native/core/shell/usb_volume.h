// Removable drives that went, for a card of Atrium's own. Windows' own
// "safe to remove" toast is a balloon of its tray icon, and Do Not Disturb
// swallows it whole (it never reaches the notification centre, where the
// listener reads), so an eject is mirrored by nothing: the host window's
// WM_DEVICECHANGE events are read here instead.
#pragma once

#include "localization.h"

#include <windows.h>
#include <dbt.h>

#include <map>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

namespace atrium {

// A removable volume that has gone: its letter, the label it had, and whether
// it was ejected (an orderly removal) rather than pulled out.
struct RemovedVolume {
    wchar_t letter = 0;  // 'E'
    std::wstring label;  // the volume label, empty when it had none
    bool ejected = false;
};

// The letters set in `before` and not in `after`, ascending (bit 0 is A:).
inline std::vector<wchar_t> RemovedLetters(unsigned before, unsigned after)
{
    std::vector<wchar_t> letters;
    const unsigned gone = before & ~after;
    for (int i = 0; i < 26; ++i)
        if (gone & (1u << i))
            letters.push_back(wchar_t(L'A' + i));
    return letters;
}

// How a card names a volume: "label (E:)", or just "E:" when it had no label.
inline std::wstring VolumeName(const std::wstring& label, wchar_t letter)
{
    const std::wstring drive = std::wstring(1, letter) + L":";
    return label.empty() ? drive : label + L" (" + drive + L")";
}

// The names of one removal, joined for the card's body.
inline std::wstring JoinNames(const std::vector<std::wstring>& names)
{
    std::wstring joined;
    for (const auto& name : names) {
        if (!joined.empty())
            joined += Tr(L", ");
        joined += name;
    }
    return joined;
}

struct UsbVolume {
    std::wstring label;
    std::wstring identity; // volume GUID path; distinguishes reuse of one drive letter
};

struct UsbVolumeSnapshot {
    unsigned present = 0; // all drive letters, including internal and network drives
    std::map<wchar_t, UsbVolume> volumes; // removable media and fixed-media USB disks
};

// DBTF_MEDIA describes a change of media, not the bus a disk is attached to.
inline bool WatchUsbVolume(UINT driveType, bool usbBus)
{
    return driveType == DRIVE_REMOVABLE || (driveType == DRIVE_FIXED && usbBus);
}

// May perform device I/O. Only the monitor's worker calls this in production.
std::optional<UsbVolumeSnapshot> ReadUsbVolumeSnapshot();

class UsbVolumes {
public:
    // Only a volume payload identifies drive letters. Interface events carry
    // no unitmask and request another snapshot; they are not proof of an eject.
    struct Event {
        UINT kind = 0; // DBT_DEVICEARRIVAL, DBT_DEVICEREMOVEPENDING, DBT_DEVICEREMOVECOMPLETE
        unsigned unitmask = 0;
    };

    void Observe(const Event& event);
    // State reconciliation only: no Windows queries and no disk I/O.
    std::vector<RemovedVolume> Sync(const UsbVolumeSnapshot& snapshot);

private:
    unsigned ejecting_ = 0; // the letters of a device Windows is ejecting (a REMOVEPENDING came)
    std::map<wchar_t, UsbVolume> watched_;
};

// Construction and Changed never inspect a disk. Results are queued for the
// UI thread, and ready only wakes it; no notification object crosses threads.
class UsbVolumeMonitor {
public:
    using Reader = std::function<std::optional<UsbVolumeSnapshot>()>;
    explicit UsbVolumeMonitor(std::function<void()> ready, Reader reader = ReadUsbVolumeSnapshot);
    ~UsbVolumeMonitor();
    UsbVolumeMonitor(const UsbVolumeMonitor&) = delete;
    UsbVolumeMonitor& operator=(const UsbVolumeMonitor&) = delete;
    void Changed(const UsbVolumes::Event& event);
    std::vector<RemovedVolume> TakeRemoved();

private:
    void Worker();
    std::function<void()> ready_;
    Reader reader_;
    std::mutex mutex_;
    std::condition_variable changed_;
    std::vector<UsbVolumes::Event> events_;
    std::vector<RemovedVolume> removed_;
    bool stopping_ = false;
    std::thread worker_;
};

} // namespace atrium
