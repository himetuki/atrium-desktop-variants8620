// Reconciliation and worker regressions. No real disks or system settings change.

#include "shell/usb_volume.h"

#include <cstdio>
#include <chrono>
#include <string>
#include <vector>

namespace {

int failures = 0;

void Check(bool ok, const char* what)
{
    if (!ok) {
        std::fprintf(stderr, "FAIL: %s\n", what);
        ++failures;
    }
}

constexpr unsigned kC = 1u << 2, kE = 1u << 4;

atrium::UsbVolumeSnapshot Attached(const wchar_t* identity = L"volume-A")
{
    return {kC | kE, {{L'E', {L"BACKUP", identity}}}};
}

void CheckReconciliation()
{
    using namespace atrium;
    Check(WatchUsbVolume(DRIVE_FIXED, true), "a fixed-media USB disk is watched");
    Check(!WatchUsbVolume(DRIVE_FIXED, false), "an internal fixed disk is not watched");
    Check(WatchUsbVolume(DRIVE_REMOVABLE, false), "removable media is watched");
    Check(!WatchUsbVolume(DRIVE_CDROM, true), "a media-change flag cannot turn a CD into a USB disk");
    Check(!WatchUsbVolume(DRIVE_REMOTE, false), "network volumes are not USB devices");

    UsbVolumes volumes;
    Check(volumes.Sync(Attached()).empty(), "startup records already-attached USB disks silently");
    volumes.Observe({DBT_DEVICEARRIVAL, kE}); // no DBTF_MEDIA requirement
    Check(volumes.Sync(Attached()).empty(), "a duplicate arrival does not post a removal");
    auto removed = volumes.Sync({kC, {}});
    Check(removed.size() == 1 && removed[0].letter == L'E' && removed[0].label == L"BACKUP" && !removed[0].ejected,
          "a fixed USB disk removal uses its cached label without a media event");
    Check(volumes.Sync({kC, {}}).empty(), "interface and volume removal broadcasts do not duplicate the card");

    volumes.Sync(Attached());
    volumes.Observe({DBT_DEVICEREMOVEPENDING, kE});
    removed = volumes.Sync({kC, {}});
    Check(removed.size() == 1 && removed[0].ejected, "an identified eject keeps its removal hint");
    volumes.Sync(Attached());
    volumes.Observe({DBT_DEVICEREMOVEPENDING, kE});
    volumes.Observe({DBT_DEVICEQUERYREMOVEFAILED, kE});
    removed = volumes.Sync({kC, {}});
    Check(removed.size() == 1 && !removed[0].ejected, "a cancelled eject is not reused for a later unplug");

    volumes.Sync(Attached());
    Check(volumes.Sync({kC | kE, {}}).empty(), "a temporarily unreadable but still-present disk is retained");
    removed = volumes.Sync({kC, {}});
    Check(removed.size() == 1 && removed[0].label == L"BACKUP", "a failed query does not erase the cached label");

    volumes.Sync(Attached());
    volumes.Observe({DBT_DEVICEREMOVEPENDING, kE});
    removed = volumes.Sync(Attached(L"volume-B"));
    Check(removed.size() == 1 && !removed[0].ejected, "drive-letter reuse removes the old identity without a safe-eject claim");
}

void CheckWorker()
{
    using namespace atrium;
    using namespace std::chrono_literals;
    std::mutex mutex;
    std::condition_variable changed;
    bool reading = false, release = false, ready = false;
    unsigned reads = 0;
    std::thread::id readerThread;
    UsbVolumeMonitor monitor([&] {
        std::lock_guard lock(mutex);
        ready = true;
        changed.notify_all();
    }, [&]() -> std::optional<UsbVolumeSnapshot> {
        std::unique_lock lock(mutex);
        const unsigned call = ++reads;
        readerThread = std::this_thread::get_id();
        changed.notify_all();
        if (call == 1) {
            reading = true;
            changed.notify_all();
            changed.wait(lock, [&] { return release; }); // a slow disk, held until Changed has returned
            return Attached();
        }
        if (call == 2)
            return std::nullopt; // transient enumeration failure
        return UsbVolumeSnapshot{kC, {}};
    });
    {
        std::unique_lock lock(mutex);
        Check(changed.wait_for(lock, 3s, [&] { return reading; }), "startup disk query runs asynchronously");
        Check(readerThread != std::this_thread::get_id(), "device reads run off the caller thread");
        monitor.Changed({DBT_DEVICEREMOVECOMPLETE, kE});
        Check(reads == 1, "Changed returns while the disk read is still blocked");
        release = true;
        changed.notify_all();
        Check(changed.wait_for(lock, 3s, [&] { return reads >= 2; }), "events arriving during the startup read are retained");
        Check(!ready, "failed enumeration does not claim the disks were unplugged");
    }
    Check(monitor.TakeRemoved().empty(), "failed enumeration keeps prior device state");
    monitor.Changed({DBT_DEVICEREMOVECOMPLETE, 0});
    {
        std::unique_lock lock(mutex);
        Check(changed.wait_for(lock, 3s, [&] { return ready; }), "the UI is notified after a successful background reconciliation");
    }
    const auto removed = monitor.TakeRemoved();
    Check(removed.size() == 1 && removed[0].letter == L'E' && removed[0].label == L"BACKUP",
          "background results retain the original disk identity and label");
    Check(monitor.TakeRemoved().empty(), "results are consumed once");
}

} // namespace

int main(int argc, char** argv)
{
    using namespace atrium;

    // Optional read-only integration probe. Only counts are printed; no volume
    // labels, identities or device paths leave the process.
    if (argc == 2 && std::string(argv[1]) == "--scan") {
        DWORD before = 0;
        const bool restore = SetThreadErrorMode(SEM_FAILCRITICALERRORS | SEM_NOOPENFILEERRORBOX, &before) != FALSE;
        const ULONGLONG start = GetTickCount64();
        const auto snapshot = ReadUsbVolumeSnapshot();
        if (restore)
            SetThreadErrorMode(before, nullptr);
        if (!snapshot)
            return 1;
        std::printf("read-only system scan: %zu drive letters, %zu removable/USB volumes, %llu ms\n",
                    RemovedLetters(snapshot->present, 0).size(), snapshot->volumes.size(), GetTickCount64() - start);
        return 0;
    }

    auto letters = RemovedLetters(0x000A, 0x0002); // B and D were watched, B stayed
    Check(letters.size() == 1 && letters[0] == L'D', "the letter that went is found");

    Check(RemovedLetters(0x03FFFFFF, 0).size() == 26, "every letter can go at once");
    Check(RemovedLetters(0x0002, 0x000A).empty(), "a letter that arrived is no removal");

    Check(VolumeName(L"BACKUP", L'E') == L"BACKUP (E:)", "a labelled volume is named by its label");
    Check(VolumeName(L"", L'E') == L"E:", "a volume without a label is named by its letter");

    Check(JoinNames({L"E:"}) == L"E:", "a single name is not joined");
    Check(JoinNames({L"E:", L"F:"}) == std::wstring(L"E:") + Tr(L", ") + L"F:", "the names are joined");

    CheckReconciliation();
    CheckWorker();

    if (failures) {
        std::fprintf(stderr, "%d usb volume check(s) failed\n", failures);
        return 1;
    }
    std::puts("usb volume checks passed");
    return 0;
}
