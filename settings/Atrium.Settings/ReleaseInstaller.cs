using System.Diagnostics;
using System.IO.Compression;
using System.Runtime.InteropServices;
using System.Security.Cryptography;
using System.Text.Json;

namespace Atrium.Settings;

// BITS owns downloads; its completion starts this executable in headless mode
// to validate/stage the package. Only the user's restart action replaces the app.
internal static class ReleaseInstaller
{
    internal sealed record Prepared(string Tag, string Version, string Directory);
    internal static string InstallDirectory => Path.GetDirectoryName(CoreClient.CoreExe()
        ?? throw new IOException(Loc.T("Could not find the Atrium program folder.")))!;
    internal static string CacheDirectory => Path.Combine(Path.GetDirectoryName(CoreClient.SettingsPath)!, "Updates",
        Convert.ToHexString(SHA256.HashData(System.Text.Encoding.UTF8.GetBytes(InstallDirectory.ToUpperInvariant())))[..16]);
    private static string StatePath => Path.Combine(CacheDirectory, "ready.json");
    private static string DownloadPath => Path.Combine(CacheDirectory, "download.json");
    private static string WorkDirectory => Path.Combine(CacheDirectory, "package-background");
    internal static string ResultPath => Path.Combine(CacheDirectory, "result.json");
    private sealed record Download(ReleaseInfo Release, Guid Job, string? Error = null);
    internal sealed record TransferStatus(string Text, bool CanRetry = false);

    internal static Prepared? ReadPrepared(Version current)
    {
        var ready = ReadJson<Prepared>(StatePath);
        if (ready == null || !Version.TryParse(ready.Version, out var version) || version <= current) return null;
        if (!Path.GetFullPath(ready.Directory).StartsWith(CacheDirectory + Path.DirectorySeparatorChar, StringComparison.OrdinalIgnoreCase))
            throw new IOException(Loc.T("The update cache path is invalid. Download again."));
        ValidatePackage(ready.Directory, version);
        return ready;
    }

    internal static string? ReadFailure()
    {
        if (!File.Exists(ResultPath)) return null;
        using var result = JsonDocument.Parse(File.ReadAllText(ResultPath));
        string? error = result.RootElement.GetProperty("error").GetString();
        File.Delete(ResultPath);
        return string.IsNullOrEmpty(error) ? null : error;
    }

    internal static Process StartBackgroundDownload(ReleaseInfo release)
    {
        var start = BackgroundStart("--queue-update");
        start.ArgumentList.Add(Convert.ToBase64String(System.Text.Encoding.UTF8.GetBytes(JsonSerializer.Serialize(release))));
        return Process.Start(start) ?? throw new IOException(Loc.T("Could not start the background download. Please try again."));
    }

    internal static Process ResumeBackgroundDownload() => Process.Start(BackgroundStart("--resume-update"))
        ?? throw new IOException(Loc.T("Could not resume the background download. Try again."));

    private static ProcessStartInfo BackgroundStart(string command)
    {
        var start = new ProcessStartInfo(Environment.ProcessPath!) { UseShellExecute = false, CreateNoWindow = true };
        start.ArgumentList.Add(command);
        return start;
    }

    internal static TransferStatus? ReadTransferStatus()
    {
        var download = ReadDownload();
        if (download == null || download.Release.Version <= CurrentVersion) return null;
        if (download.Error != null) return new(Loc.T("Could not prepare the update. Try again."), true);
        using var job = BitsTransfer.Open(download.Job);
        if (job == null) return File.Exists(Path.Combine(WorkDirectory, "release.zip"))
            ? new(Loc.T("Preparing the update…")) : new(Loc.T("The download did not finish. Try again."), true);
        if (job.CurrentState == BitsTransfer.State.Error) return new(Loc.T("The background download failed. Try again."), true);
        if (job.CurrentState == BitsTransfer.State.Transferred) return new(Loc.T("Preparing the update…"));
        if (job.CurrentState == BitsTransfer.State.TransientError) return new(Loc.T("Waiting to resume the background download…"));
        var progress = job.CurrentProgress;
        return new(progress.Total is > 0 and < ulong.MaxValue
            ? Loc.F("Downloading in background… {0}%", Math.Min(100, progress.Transferred * 100 / progress.Total)) : Loc.T("Downloading in background…"));
    }

    private static Version CurrentVersion => typeof(ReleaseInstaller).Assembly.GetName().Version!;
    private static Download? ReadDownload() => ReadJson<Download>(DownloadPath);
    private static T? ReadJson<T>(string path)
    {
        try
        {
            // The worker atomically replaces/removes state as the UI reads progress.
            using var stream = new FileStream(path, FileMode.Open, FileAccess.Read, FileShare.Read | FileShare.Delete);
            return JsonSerializer.Deserialize<T>(stream) ?? throw new JsonException(Loc.F("The update state is empty or incomplete: {0}", Path.GetFileName(path)));
        }
        catch (FileNotFoundException) { return default; }
        catch (DirectoryNotFoundException) { return default; }
    }
    private static void SaveJson<T>(string path, T value)
    {
        string temporary = path + ".tmp";
        File.WriteAllText(temporary, JsonSerializer.Serialize(value));
        File.Move(temporary, path, true);
    }

    // Called before XAML starts. The mutex covers the short queue/finalize work,
    // not the transfer; closing any Atrium process never calls BITS.Cancel().
    internal static int RunBackground(string[] args)
    {
        Download? pending = null;
        string? cache = null;
        Mutex? mutex = null;
        bool acquired = false;
        try
        {
            cache = CacheDirectory;
            Directory.CreateDirectory(cache);
            mutex = new Mutex(false, @"Local\Atrium.Update." + Path.GetFileName(cache));
            try { mutex.WaitOne(); } catch (AbandonedMutexException) { }
            acquired = true;
            // Read once. Error reporting must not depend on the same corrupt or
            // inaccessible state that brought us into the catch branch.
            pending = ReadDownload();
            if (args[0] == "--queue-update")
            {
                var release = JsonSerializer.Deserialize<ReleaseInfo>(Convert.FromBase64String(args[1]))
                    ?? throw new JsonException("Missing release.");
                Queue(release, pending);
            }
            else if (args[0] == "--resume-update")
            {
                if (pending != null) Queue(pending.Release, pending);
                else NotifyReady();
            }
            else Finish(pending);
            return 0;
        }
        catch (Exception error) when (error is IOException or UnauthorizedAccessException or COMException or JsonException
            or FormatException or InvalidOperationException or ArgumentException)
        {
            App.LogCrash(error);
            if (cache != null)
            {
                try
                {
                    // A known transfer keeps its identity. Unknown/unreadable
                    // state is left intact, including any recoverable package.
                    SaveJson(Path.Combine(cache, "result.json"), new { error = Loc.F("The update did not finish: {0}", error.Message) });
                    File.WriteAllText(Path.Combine(cache, "worker-error.txt"), error.ToString());
                    if (pending != null) SaveJson(DownloadPath, pending with { Error = error.Message });
                }
                catch (Exception reporting) when (reporting is IOException or UnauthorizedAccessException)
                { App.LogCrash(new AggregateException(Loc.T("Could not write the update error state"), error, reporting)); }
            }
            return args[0] == "--finish-update" ? 0 : 1; // permanent failures wait for a retry, not callback loops
        }
        finally
        {
            if (acquired) mutex!.ReleaseMutex();
            mutex?.Dispose();
        }
    }

    private static void Queue(ReleaseInfo release, Download? pending)
    {
        if (release.Download == null || release.Version <= CurrentVersion) return;
        if (ReadPrepared(CurrentVersion) is { } ready && Version.Parse(ready.Version) >= release.Version)
        { NotifyReady(); return; }
        if (pending != null && pending.Release.Version > release.Version) release = pending.Release;
        if (release.Download == null) return;
        if (pending != null && pending.Release.Version >= release.Version && pending.Error == null)
        {
            bool finish = false;
            using (var existing = BitsTransfer.Open(pending.Job))
            {
                if (existing != null)
                {
                    existing.OnTransferred(Environment.ProcessPath!);
                    finish = existing.CurrentState == BitsTransfer.State.Transferred;
                    if (!finish) { existing.Resume(); return; }
                }
            }
            if (finish || File.Exists(Path.Combine(WorkDirectory, "release.zip"))) { Finish(pending); return; }
        }
        if (pending != null)
        {
            using var previous = BitsTransfer.Open(pending.Job);
            previous?.Cancel(); // only this install's superseded or failed transfer
        }
        if (Directory.Exists(WorkDirectory)) Directory.Delete(WorkDirectory, true);
        File.Delete(StatePath);
        Directory.CreateDirectory(WorkDirectory);
        using var job = BitsTransfer.Create("Atrium update " + Path.GetFileName(CacheDirectory));
        try
        {
            SaveJson(DownloadPath, new Download(release, job.Id));
            job.Add(release.Download, Path.Combine(WorkDirectory, "release.zip"));
            job.OnTransferred(Environment.ProcessPath!);
            job.Resume();
        }
        catch { job.Cancel(); throw; }
    }

    private static void Finish(Download? pending)
    {
        if (pending == null) { NotifyReady(); return; }
        using (var job = BitsTransfer.Open(pending.Job))
        {
            if (job != null)
            {
                if (job.CurrentState == BitsTransfer.State.Error)
                { SaveJson(DownloadPath, pending with { Error = Loc.T("The background download failed") }); return; }
                if (job.CurrentState != BitsTransfer.State.Transferred) return;
                job.Complete();
            }
        }
        string zip = Path.Combine(WorkDirectory, "release.zip");
        if (pending.Release.Digest is { } digest)
        {
            using var stream = File.OpenRead(zip);
            string actual = "sha256:" + Convert.ToHexString(SHA256.HashData(stream));
            if (!actual.Equals(digest, StringComparison.OrdinalIgnoreCase)) throw new IOException(Loc.T("The update package failed verification. Download again."));
        }
        string unpacked = Path.Combine(WorkDirectory, "unpacked");
        if (Directory.Exists(unpacked)) Directory.Delete(unpacked, true); // an interrupted extraction, not transfer data
        ZipFile.ExtractToDirectory(zip, unpacked);
        string root = File.Exists(Path.Combine(unpacked, "Atrium.exe")) ? unpacked : Path.Combine(unpacked, "Atrium");
        ValidatePackage(root, pending.Release.Version);
        SaveJson(StatePath, new Prepared(pending.Release.Tag, pending.Release.Version.ToString(), root));
        File.Delete(DownloadPath);
        File.Delete(zip);
        NotifyReady();
    }

    [DllImport("user32.dll", CharSet = CharSet.Unicode)] private static extern IntPtr FindWindow(string cls, string? title);
    [DllImport("user32.dll")] private static extern uint GetWindowThreadProcessId(IntPtr hwnd, out uint process);
    [DllImport("user32.dll")] private static extern bool PostMessage(IntPtr hwnd, uint message, UIntPtr wp, IntPtr lp);
    private static void NotifyReady()
    {
        IntPtr host = FindWindow("AtriumCoreHost", null);
        if (host == IntPtr.Zero) return;
        GetWindowThreadProcessId(host, out uint pid);
        try
        {
            using var core = Process.GetProcessById((int)pid);
            if (string.Equals(core.MainModule?.FileName, CoreClient.CoreExe(), StringComparison.OrdinalIgnoreCase))
                PostMessage(host, 0x8000 + 11, UIntPtr.Zero, IntPtr.Zero);
        }
        catch (Exception error) when (error is ArgumentException or InvalidOperationException or System.ComponentModel.Win32Exception)
        { /* The core may have exited; it reads ready.json on its next startup. */ }
    }

    private static void ValidatePackage(string directory, Version expected)
    {
        string core = Path.Combine(directory, "Atrium.exe");
        string settings = Path.Combine(directory, "Settings", "Atrium.Settings.exe");
        if (!File.Exists(core) || !File.Exists(settings) || !File.Exists(Path.ChangeExtension(settings, ".dll")))
            throw new IOException(Loc.T("The update package is missing program files. Download again."));
        var info = FileVersionInfo.GetVersionInfo(core);
        var version = new Version(info.FileMajorPart, info.FileMinorPart, info.FileBuildPart, info.FilePrivatePart);
        var settingsInfo = FileVersionInfo.GetVersionInfo(Path.ChangeExtension(settings, ".dll"));
        var settingsVersion = new Version(settingsInfo.FileMajorPart, settingsInfo.FileMinorPart, settingsInfo.FileBuildPart, settingsInfo.FilePrivatePart);
        if (version != expected || settingsVersion != expected) throw new IOException(Loc.T("The update package does not match the release. Download again."));
    }

    internal static void Restart(Prepared ready, bool startCore)
    {
        ValidatePackage(ready.Directory, Version.Parse(ready.Version));
        // Copy the helper out of the install directory before replacing that directory.
        string script = Path.Combine(CacheDirectory, "Update.ps1");
        // Windows PowerShell 5.1 needs a BOM to read the helper's Chinese messages.
        File.WriteAllText(script, File.ReadAllText(Path.Combine(AppContext.BaseDirectory, "Update.ps1")), new System.Text.UTF8Encoding(true));
        var start = new ProcessStartInfo(Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.System),
            "WindowsPowerShell", "v1.0", "powershell.exe")) { UseShellExecute = false, CreateNoWindow = true, WorkingDirectory = CacheDirectory };
        foreach (string arg in new[] { "-NoProfile", "-ExecutionPolicy", "Bypass", "-File", script,
            "-InstallDirectory", InstallDirectory, "-PackageDirectory", ready.Directory, "-StatePath", StatePath,
            "-WaitForProcessId", Environment.ProcessId.ToString(), "-StartCore", startCore ? "1" : "0" }) start.ArgumentList.Add(arg);
        // Check before closing any UI. Program Files installations need a writable
        // parent directory; do not stop the app and then discover access was denied.
        string probe = Path.Combine(Path.GetDirectoryName(InstallDirectory)!, ".atrium-update-" + Guid.NewGuid().ToString("N"));
        using (File.Create(probe)) { }
        File.Delete(probe);
        using var helper = Process.Start(start) ?? throw new IOException(Loc.T("Could not start the update. Try again."));
    }
}
