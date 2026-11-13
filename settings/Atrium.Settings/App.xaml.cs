using System.Runtime.InteropServices;
using Microsoft.UI.Xaml;

namespace Atrium.Settings;

public partial class App : Application
{
    [DllImport("user32.dll", CharSet = CharSet.Unicode)] private static extern IntPtr FindWindow(string? cls, string title);
    [DllImport("user32.dll")] private static extern bool SetForegroundWindow(IntPtr hwnd);
    [DllImport("user32.dll")] private static extern bool ShowWindow(IntPtr hwnd, int cmd);
    [DllImport("user32.dll")] private static extern bool PostMessage(IntPtr hwnd, uint message, UIntPtr wparam, IntPtr lparam);
    [DllImport("user32.dll")] private static extern uint GetWindowThreadProcessId(IntPtr hwnd, out uint process);
    [DllImport("user32.dll", CharSet = CharSet.Unicode)] private static extern int MessageBox(IntPtr hwnd, string text, string title, uint type);
    internal const uint OpenAboutMessage = 0x8000 + 31;

    public static string WindowTitle => Loc.T("Atrium Settings");
    internal static MainWindow? CurrentWindow { get; private set; }
    private static BarSettings? s_pendingCurrent, s_pendingSaved;
    private static Mutex? s_instance;
    private MainWindow? _window;

    public App()
    {
        InitializeComponent();
        // A crash leaves its exception in %LOCALAPPDATA%\Atrium\logs\settings.log (WinUI itself reports only 0xc000027b).
        UnhandledException += (_, e) => LogCrash(e.Exception);
    }

    public static void LogCrash(Exception e)
    {
        try
        {
            var dir = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "Atrium", "logs");
            Directory.CreateDirectory(dir);
            File.AppendAllText(Path.Combine(dir, "settings.log"), $"{DateTime.Now:yyyy-MM-dd HH:mm:ss} {e}\n");
        }
        catch (IOException) { }
        catch (UnauthorizedAccessException) { }
    }

    protected override void OnLaunched(LaunchActivatedEventArgs args)
    {
        Loc.Load(); // also before the single-instance lookup: the title follows the language
        // Single instance: a second launch (tray menu, double click) brings the
        // existing window to the front instead of opening another one.
        s_instance = new Mutex(true, @"Local\Atrium.Settings", out bool created);
        if (!created)
        {
            var other = FindWindow(null, WindowTitle);
            if (other != IntPtr.Zero)
            {
                GetWindowThreadProcessId(other, out uint pid);
                bool same = false;
                try
                {
                    using var process = System.Diagnostics.Process.GetProcessById((int)pid);
                    same = string.Equals(process.MainModule?.FileName, Environment.ProcessPath, StringComparison.OrdinalIgnoreCase);
                }
                catch (Exception error) when (error is ArgumentException or InvalidOperationException or System.ComponentModel.Win32Exception) { }
                if (!same)
                {
                    MessageBox(IntPtr.Zero, Loc.T("Atrium Settings is running from another location. Close that window, then open the settings in this folder."), "Atrium", 0x40);
                    Exit();
                    return;
                }
                ShowWindow(other, 9 /* SW_RESTORE */);
                SetForegroundWindow(other);
                var command = Environment.GetCommandLineArgs();
                int page = Array.IndexOf(command, "--page");
                if (page >= 0 && page + 1 < command.Length && command[page + 1] == "about")
                    PostMessage(other, OpenAboutMessage, UIntPtr.Zero, IntPtr.Zero);
            }
            Exit();
            return;
        }
        try
        {
            _window = new MainWindow();
            CurrentWindow = _window;
            _window.Activate();
        }
        catch (Exception e)
        {
            LogCrash(e);
            throw;
        }
    }

    // A language change re-creates the window: every XAML string is loaded
    // again in the new language. The snapshots keep edits made while the save
    // was on its way, so the new window can show them as unsaved.
    internal static void ReloadWindow(int page, BarSettings? current = null, BarSettings? saved = null)
    {
        s_pendingCurrent = current?.Clone();
        s_pendingSaved = saved?.Clone();
        var old = CurrentWindow;
        old?.ReleaseForReload();
        var window = new MainWindow(page);
        CurrentWindow = window;
        window.Activate();
        old?.Close();
    }

    // The new window takes the pending snapshots after reading the core's
    // saved settings; they were still unsaved when the old window closed.
    internal static (BarSettings? Current, BarSettings? Saved) TakePendingState()
    {
        var state = (s_pendingCurrent, s_pendingSaved);
        s_pendingCurrent = s_pendingSaved = null;
        return state;
    }

    internal static void SignalStartupReady(bool coreReady)
    {
        var command = Environment.GetCommandLineArgs();
        if (command.Contains("--require-core-ready") && !coreReady) return;
        int index = Array.IndexOf(command, "--ready-event");
        if (index < 0 || index + 1 >= command.Length || !command[index + 1].StartsWith(@"Local\Atrium.Update.Settings.", StringComparison.Ordinal)) return;
        try
        {
            using var ready = EventWaitHandle.OpenExisting(command[index + 1]);
            ready.Set();
        }
        catch (WaitHandleCannotBeOpenedException) { }
    }
}
