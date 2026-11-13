using System.Diagnostics;
using System.IO.Pipes;
using System.Runtime.InteropServices;
using System.Text;
using System.Text.Json;
using System.Text.Json.Nodes;
using Microsoft.Win32;

namespace Atrium.Settings;

// Client of the Atrium.Core settings pipe (docs/SETTINGS-PROTOCOL.md). When the
// core is not running, settings are read from and written to the file directly.
internal sealed class CoreClient : IDisposable
{
    [DllImport("kernel32.dll", SetLastError = true)]
    private static extern bool GetNamedPipeServerProcessId(IntPtr pipe, out uint processId);

    private readonly string _pipeName;
    internal CoreClient(string? pipeName = null) => _pipeName = pipeName ?? "Atrium.Core." + Process.GetCurrentProcess().SessionId;
    public static string SettingsPath => Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "Atrium", "settings.json");

    private readonly SemaphoreSlim _io = new(1, 1);
    private readonly CancellationTokenSource _lifetime = new();
    private CancellationTokenSource? _connection;
    private readonly byte[] _readBuffer = new byte[4096];
    private NamedPipeClientStream? _pipe;
    private bool _connectionLost;
    internal static bool Development
    {
        get
        {
#if ATRIUM_DEVELOPMENT
            return true;
#else
            var exe = CoreExe();
            return Environment.GetCommandLineArgs().Contains("--development") ||
                (exe != null && File.Exists(Path.Combine(Path.GetDirectoryName(exe)!, "development.txt")));
#endif
        }
    }
    public string? LastSaveWarning { get; private set; }
    public string? ConnectionError { get; private set; }

    public bool Connected => _pipe is { IsConnected: true };
    public int ProcessId { get; private set; }
    public bool StateConfirmed { get; private set; }

    public static string? CoreExe()
    {
        foreach (var p in new[] { Path.Combine(AppContext.BaseDirectory, "..", "Atrium.exe"), Path.Combine(AppContext.BaseDirectory, "Atrium.exe") })
            if (File.Exists(p)) return Path.GetFullPath(p);
        return null;
    }

    public async Task<bool> ConnectAsync(int timeoutMs)
    {
        Disconnect();
        var pipe = new NamedPipeClientStream(".", _pipeName, PipeDirection.InOut, PipeOptions.Asynchronous);
        try
        {
            await pipe.ConnectAsync(timeoutMs, _lifetime.Token);
            pipe.ReadMode = PipeTransmissionMode.Message;
            // The name is session-wide; only this distribution's executable owns our settings.
            if (!GetNamedPipeServerProcessId(pipe.SafePipeHandle.DangerousGetHandle(), out var pid))
            {
                pipe.Dispose();
                return false;
            }
            using var process = Process.GetProcessById((int)pid);
            var expected = CoreExe();
            if (expected == null || !string.Equals(process.MainModule?.FileName, expected, StringComparison.OrdinalIgnoreCase))
            {
                _connectionLost = true;
                ConnectionError = Loc.T("Atrium is running from another location; the core in this folder was not connected.");
                pipe.Dispose();
                return false;
            }
            _lifetime.Token.ThrowIfCancellationRequested();
            _connection = CancellationTokenSource.CreateLinkedTokenSource(_lifetime.Token);
            _pipe = pipe;
            ProcessId = (int)pid;
            _connectionLost = false;
            ConnectionError = null;
            return true;
        }
        catch (Exception e) when (e is TimeoutException or IOException or UnauthorizedAccessException or ArgumentException or InvalidOperationException or OperationCanceledException or System.ComponentModel.Win32Exception)
        {
            if (e is not TimeoutException and not OperationCanceledException)
            {
                _connectionLost = true;
                ConnectionError = Loc.F("Could not connect to the core in this folder: {0}", e.Message);
            }
            pipe.Dispose();
            return false;
        }
    }

    public void Disconnect(bool coreStopped = false)
    {
        _connection?.Cancel(); // also wakes every request waiting for the semaphore
        _connection?.Dispose();
        _connection = null;
        _pipe?.Dispose();
        _pipe = null;
        ProcessId = 0;
        StateConfirmed = false;
        if (coreStopped) { _connectionLost = false; ConnectionError = null; }
    }

    private async Task<JsonObject?> RequestAsync(JsonObject request)
    {
        var pipe = _pipe;
        var connection = _connection;
        if (pipe is not { IsConnected: true } || connection == null || _lifetime.IsCancellationRequested) return null;
        using var deadline = CancellationTokenSource.CreateLinkedTokenSource(connection.Token, _lifetime.Token);
        deadline.CancelAfter(TimeSpan.FromSeconds(5)); // includes queueing, writing and the reply
        bool entered = false;
        try
        {
            await _io.WaitAsync(deadline.Token);
            entered = true;
            deadline.Token.ThrowIfCancellationRequested();
            if (!ReferenceEquals(pipe, _pipe)) return null;
            var bytes = JsonSerializer.SerializeToUtf8Bytes(request);
            await pipe.WriteAsync(bytes, deadline.Token);
            await pipe.FlushAsync(deadline.Token);
            using var message = new MemoryStream();
            do
            {
                int n = await pipe.ReadAsync(_readBuffer, deadline.Token);
                if (n == 0) throw new IOException("pipe closed");
                message.Write(_readBuffer, 0, n);
                if (message.Length > 1024 * 1024) throw new IOException(Loc.T("The core's reply was too large"));
            } while (!pipe.IsMessageComplete);
            return JsonNode.Parse(message.GetBuffer().AsSpan(0, (int)message.Length)) as JsonObject;
        }
        catch (Exception e) when (e is IOException or ObjectDisposedException or System.Text.Json.JsonException or OperationCanceledException)
        {
            if (ReferenceEquals(pipe, _pipe))
            {
                _connectionLost = true;
                ConnectionError = Loc.T("The core did not respond or the connection was lost; the running state is not confirmed.");
                Disconnect();
            }
            return null;
        }
        finally
        {
            if (entered) _io.Release();
        }
    }

    // (saved, current) from the core, or (file, file) when it is not running.
    public async Task<(BarSettings Saved, BarSettings Current)> GetAsync()
    {
        var r = await RequestAsync(new JsonObject { ["op"] = "get" });
        if (r?["op"]?.GetValue<string>() == "state")
        {
            StateConfirmed = true;
            return (BarSettings.FromJson(r["saved"] as JsonObject, new()), BarSettings.FromJson(r["current"] as JsonObject, new()));
        }
        var file = ReadFile();
        return (file, file.Clone());
    }

    public Task<string?> PreviewAsync(BarSettings s) => Send("preview", s);

    public async Task<string?> CommitAsync(BarSettings s)
    {
        LastSaveWarning = null;
        if (Connected)
            return await Send("commit", s);
        if (_connectionLost || _lifetime.IsCancellationRequested)
            return Loc.T("The connection to the core was lost; the save result is not confirmed. Reconnect and check the saved settings.");
        try
        {
            LastSaveWarning = WriteFile(s);
            return null;
        }
        catch (Exception e) when (e is IOException or UnauthorizedAccessException)
        {
            return e.Message;
        }
    }

    public Task<string?> CancelAsync() => Send("cancel", null);

    // Puts a desktop widget in the first free place ("clock", "medium"); null on success.
    public async Task<string?> AddWidgetAsync(string kind, string size)
    {
        var r = await RequestAsync(new JsonObject { ["op"] = "add-widget", ["kind"] = kind, ["size"] = size });
        if (r == null) return Loc.T("Atrium is not running");
        return r["op"]?.GetValue<string>() == "ok" ? null : r["message"]?.GetValue<string>() ?? "error";
    }

    // Null on success, otherwise an error message.
    private async Task<string?> Send(string op, BarSettings? s)
    {
        var request = new JsonObject { ["op"] = op };
        if (s != null) request["settings"] = s.ToJson();
        bool connected = Connected;
        var r = await RequestAsync(request);
        // Not connected: nothing to preview. A connection lost on the way: not done.
        if (r == null) return connected || _connectionLost ? Loc.T("The core did not reply in time or the connection closed; the result is not confirmed. Please reconnect.") : null;
        if (op == "commit") LastSaveWarning = r["warning"]?.GetValue<string>();
        return r["op"]?.GetValue<string>() == "ok" ? null : r["message"]?.GetValue<string>() ?? "error";
    }

    private static BarSettings ReadFile()
    {
        BarSettings s;
        try
        {
            s = BarSettings.FromJson(JsonNode.Parse(File.ReadAllText(SettingsPath)) as JsonObject, new());
        }
        catch (Exception e) when (e is IOException or UnauthorizedAccessException or System.Text.Json.JsonException)
        {
            s = new BarSettings();
        }
        // First run (no "pins" key yet): show what the core would import.
        s.Pins ??= ImportTaskbarPins();
        return s;
    }

    // The pins of the Windows taskbar, as the core imports them on first run.
    public static List<PinnedApp> ImportTaskbarPins()
    {
        var pins = new List<PinnedApp>();
        try
        {
            var dir = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.ApplicationData),
                "Microsoft", "Internet Explorer", "Quick Launch", "User Pinned", "TaskBar");
            if (!Directory.Exists(dir)) return pins;
            foreach (var f in Directory.EnumerateFiles(dir, "*.lnk").OrderBy(x => x, StringComparer.OrdinalIgnoreCase))
                pins.Add(new PinnedApp(Path.GetFileNameWithoutExtension(f), f, "", ""));
        }
        catch (Exception e) when (e is IOException or UnauthorizedAccessException)
        {
        }
        return pins.Count > 64 ? pins.GetRange(0, 64) : pins;
    }

    private static string? WriteFile(BarSettings s)
    {
        bool startupChanged = ReadFile().StartOnLogin != s.StartOnLogin;
        Directory.CreateDirectory(Path.GetDirectoryName(SettingsPath)!);
        var tmp = SettingsPath + ".tmp";
        File.WriteAllText(tmp, s.ToJson().ToJsonString(), new UTF8Encoding(false));
        File.Move(tmp, SettingsPath, overwrite: true);
        if (!startupChanged || Development) return null;
        try
        {
            using var run = Registry.CurrentUser.CreateSubKey(@"Software\Microsoft\Windows\CurrentVersion\Run");
            var exe = CoreExe() ?? throw new FileNotFoundException(Loc.T("Atrium.exe was not found"));
            var command = $"\"{exe}\"";
            if (s.StartOnLogin)
            {
                run.SetValue("Atrium", command);
            }
            else
            {
                if (run.GetValue("Atrium") is string owner && !string.Equals(owner, command, StringComparison.OrdinalIgnoreCase))
                    throw new IOException(Loc.T("That startup entry belongs to Atrium in another location"));
                RemoveLogonTask(exe);
                run.DeleteValue("Atrium", throwOnMissingValue: false);
            }
        }
        catch (Exception e) when (e is IOException or UnauthorizedAccessException or System.Security.SecurityException)
        {
            return Loc.F("Settings were saved, but Windows startup could not be changed: {0}. Toggle Start with Windows again, then save.", e.Message);
        }
        return null;
    }

    // The core starts from a Task Scheduler logon task (shell/logon_task.cpp); it registers it
    // itself, from the Run entry written above, when it next starts.
    private static void RemoveLogonTask(string exe)
    {
        var name = "Atrium Startup-" + System.Security.Principal.WindowsIdentity.GetCurrent().User?.Value;
        try
        {
            dynamic service = Activator.CreateInstance(Type.GetTypeFromProgID("Schedule.Service")!)!;
            service.Connect();
            dynamic folder = service.GetFolder("\\");
            dynamic task;
            try { task = folder.GetTask(name); }
            catch (COMException e) when (e.HResult == unchecked((int)0x80070002)) { return; } // no task
            string command = ((string)task.Definition.Actions.Item(1).Path).Trim('"');
            if (!string.Equals(command, exe, StringComparison.OrdinalIgnoreCase))
                throw new IOException(Loc.T("That startup entry belongs to Atrium in another location"));
            folder.DeleteTask(name, 0);
        }
        catch (COMException e)
        {
            throw new IOException(e.Message, e);
        }
    }

    public void Dispose()
    {
        _lifetime.Cancel();
        Disconnect();
        // In-flight requests still release this semaphore after Disconnect completes their I/O.
    }
}
