using System.Text.Json.Nodes;
using Atrium.Settings;

static void Require(bool condition, string message)
{
    if (!condition) throw new InvalidOperationException(message);
}

var settings = new BarSettings
{
    DockDisplayMode = "all", DockDisplayDelay = 750, DockDisplay = @"\\?\DISPLAY#external",
    DesktopDisplay = "disconnected-screen", TaskbarApps = "primaryAndWindow", TaskbarRightPrimaryOnly = true, NotifyPlacement = "topRight",
    HoverRise = false,
};
var parsed = BarSettings.FromJson(settings.ToJson(), new());
Require(settings.SameAs(parsed), "Display preferences must survive a settings round trip.");
Require(BarSettings.FromJson(new JsonObject(), new()).SameAs(new()), "Old files must receive display defaults.");
Require(!BarSettings.FromJson(new JsonObject(), new()).HoverRise, "Old files must keep the glass flat.");
Require(BarSettings.FromJson(new JsonObject(), new() { HoverRise = true }).HoverRise, "Partial updates must retain enabled hover rise.");
Require(!BarSettings.FromJson(new JsonObject(), settings).HoverRise, "Partial updates must retain disabled hover rise.");
Require(!BarSettings.FromJson(JsonNode.Parse("""{"hoverRise":"bad"}""")!.AsObject(), settings).HoverRise,
    "Malformed hover rise must retain the current value.");
parsed = BarSettings.FromJson(JsonNode.Parse("""{"hoverRise":true}""")!.AsObject(), settings);
Require(parsed.HoverRise && parsed.Magnify == settings.Magnify, "Hover rise must toggle without changing icon magnification.");
Require(BarSettings.FromJson(new JsonObject(), new()).StartStatus && BarSettings.FromJson(new JsonObject(), new()).StartTray,
    "Old files must keep the start menu's status and tray buttons.");
parsed = BarSettings.FromJson(JsonNode.Parse("""{"startStatus":false}""")!.AsObject(), new());
Require(!parsed.StartStatus && parsed.StartTray && BarSettings.FromJson(parsed.ToJson(), new()).SameAs(parsed),
    "The start menu's status buttons must turn off on their own and survive a round trip.");
Require(!new BarSettings().SameAs(parsed), "The start menu's status buttons must participate in the Save/Revert dirty state.");
Require(!BarSettings.FromJson(new JsonObject(), new()).StageMinimize, "Old files must keep minimized windows out of Stage Manager.");
parsed = BarSettings.FromJson(JsonNode.Parse("""{"stageMinimize":true}""")!.AsObject(), new());
Require(parsed.StageMinimize && BarSettings.FromJson(parsed.ToJson(), new()).StageMinimize && !new BarSettings().SameAs(parsed),
    "Minimizing to Stage Manager must survive a round trip and mark the settings dirty.");
var excludedDefault = new BarSettings();
Require(excludedDefault.StageExcluded.SequenceEqual(BarSettings.StageExcludedDefaults) &&
        excludedDefault.StageExcluded.Contains("snippingtool.exe") && excludedDefault.StageExcluded.Contains("microsoft.screensketch_") &&
        !excludedDefault.StageExcluded.Contains("wechat.exe") && !excludedDefault.StageExcluded.Contains("notepad.exe"),
    "The Stage Manager blacklist must start with the Windows screenshot tools.");
var excluded = BarSettings.FromJson(JsonNode.Parse("""{"stageExcluded":["wechat.exe","tencent.qq_"]}""")!.AsObject(), new());
Require(excluded.StageExcluded.SequenceEqual(new[] { "wechat.exe", "tencent.qq_" }) &&
        BarSettings.FromJson(excluded.ToJson(), new()).SameAs(excluded) && !new BarSettings().SameAs(excluded),
    "The Stage Manager blacklist must survive a round trip and mark the settings dirty.");
var excludedEmpty = BarSettings.FromJson(JsonNode.Parse("""{"stageExcluded":[]}""")!.AsObject(), new());
Require(excludedEmpty.StageExcluded.Count == 0 && BarSettings.FromJson(excludedEmpty.ToJson(), new()).SameAs(excludedEmpty),
    "An empty Stage Manager blacklist must stay empty and survive a round trip.");
var excludedMalformed = BarSettings.FromJson(JsonNode.Parse("""{"stageExcluded":false}""")!.AsObject(), excluded);
Require(excludedMalformed.StageExcluded.SequenceEqual(excluded.StageExcluded),
    "A malformed Stage Manager blacklist must retain the current list.");
Require(!BarSettings.FromJson(new JsonObject(), new()).ShowClockSeconds, "Old files must keep the clock at minutes.");
Require(!new BarSettings().MagnifyNear && !BarSettings.FromJson(new JsonObject(), new()).MagnifyNear,
    "Icons must not magnify above the bar unless chosen.");
parsed = BarSettings.FromJson(JsonNode.Parse("""{"remoteVisible":true}""")!.AsObject(), new());
Require(parsed.RemoteVisible && BarSettings.FromJson(parsed.ToJson(), new()).RemoteVisible && !new BarSettings().SameAs(parsed),
    "Remote-control mode must survive a round trip and mark the settings dirty.");
parsed = BarSettings.FromJson(JsonNode.Parse("""{"showClockSeconds":true}""")!.AsObject(), new());
Require(parsed.ShowClockSeconds && parsed.ShowClock &&
        BarSettings.FromJson(parsed.ToJson(), new()).SameAs(parsed) && !new BarSettings().SameAs(parsed),
    "Clock seconds must survive a round trip, mark the settings dirty and leave the clock on.");
Require(BarSettings.FromJson(JsonNode.Parse("""{"showClockSeconds":"bad"}""")!.AsObject(),
        new() { ShowClockSeconds = true }).ShowClockSeconds,
    "Malformed clock seconds must retain the current value.");
Require(!BarSettings.FromJson(new JsonObject(), new()).TaskbarRightPrimaryOnly, "Old files must keep right-side elements on every display.");
Require(BarSettings.FromJson(new JsonObject(), settings).TaskbarRightPrimaryOnly, "Partial updates must retain primary-only visibility.");
Require(BarSettings.FromJson(JsonNode.Parse("""{"taskbarRightPrimaryOnly":"bad"}""")!.AsObject(), settings).TaskbarRightPrimaryOnly,
    "Malformed primary-only visibility must retain the current value.");
parsed = BarSettings.FromJson(JsonNode.Parse("""{"taskbarRightPrimaryOnly":false}""")!.AsObject(), settings);
Require(!parsed.TaskbarRightPrimaryOnly && parsed.ShowTray && parsed.ShowStatus && parsed.ShowClock && parsed.ShowNotifications,
    "Disabling primary-only visibility must retain the individual content switches.");
Require(BarSettings.FromJson(new JsonObject(), new()).NotifyPlacement == "dock", "Existing users must retain the default notification placement.");
Require(BarSettings.FromJson(new JsonObject(), new()).Language == "auto", "Old files must follow the system language.");
settings.Language = "en";
Require(BarSettings.FromJson(settings.ToJson(), new()).Language == "en", "The interface language must survive a round trip.");
Require(BarSettings.FromJson(JsonNode.Parse("""{"language":"bad tag"}""")!.AsObject(), settings).Language == "auto", "Invalid languages must follow the system.");
Require(BarSettings.FromJson(JsonNode.Parse("""{"language":false}""")!.AsObject(), settings).Language == "en", "Malformed languages must retain the current value.");
Require(BarSettings.FromJson(JsonNode.Parse("""{"notifyPlacement":"bad"}""")!.AsObject(), new()).NotifyPlacement == "dock", "Invalid notification placement must normalize.");
Require(BarSettings.FromJson(JsonNode.Parse("""{"notifyPlacement":false}""")!.AsObject(), settings).NotifyPlacement == "topRight", "Malformed notification placement must retain the current value.");
foreach (string mode in new[] { "follow", "primary", "fixed", "all" })
{
    settings.DockDisplayMode = mode;
    Require(BarSettings.FromJson(settings.ToJson(), new()).DockDisplayMode == mode, "Every Dock mode must be retained.");
}
foreach (string mode in new[] { "all", "primaryAndWindow", "window" })
{
    settings.TaskbarApps = mode;
    Require(BarSettings.FromJson(settings.ToJson(), new()).TaskbarApps == mode, "Every task distribution mode must be retained.");
}
parsed = BarSettings.FromJson(JsonNode.Parse("""{"dockDisplayMode":"bad","taskbarApps":"bad","dockDisplayDelay":99999}""")!.AsObject(), new());
Require(parsed.DockDisplayMode == "primary" && parsed.TaskbarApps == "all" && parsed.DockDisplayDelay == 2000, "Invalid values must normalize like the native core.");
parsed = BarSettings.FromJson(JsonNode.Parse("""{"dockDisplayDelay":-1}""")!.AsObject(), new());
Require(parsed.DockDisplayDelay == 100, "Delay must have a lower bound.");
parsed = BarSettings.FromJson(JsonNode.Parse("""{"dockDisplayDelay":"bad","desktopDisplay":false}""")!.AsObject(), settings);
Require(parsed.DockDisplayDelay == settings.DockDisplayDelay && parsed.DesktopDisplay == settings.DesktopDisplay, "Malformed values must retain the current settings.");
var original = settings.Clone();
settings.ApplyMaterial(BarSettings.Frosted());
Require(settings.DockDisplayMode == original.DockDisplayMode && settings.DockDisplay == original.DockDisplay &&
    settings.DesktopDisplay == original.DesktopDisplay && settings.DockDisplayDelay == original.DockDisplayDelay &&
    settings.TaskbarApps == original.TaskbarApps && settings.TaskbarRightPrimaryOnly == original.TaskbarRightPrimaryOnly &&
    settings.NotifyPlacement == original.NotifyPlacement && settings.HoverRise == original.HoverRise &&
    settings.WindowsContextMenu == original.WindowsContextMenu,
    "Material presets must preserve monitor, notification and hover-rise choices.");
foreach (Action<BarSettings> edit in new Action<BarSettings>[]
{
    s => s.DockDisplayMode = "fixed", s => s.DockDisplayDelay += 50, s => s.DockDisplay += "other",
    s => s.DesktopDisplay += "other", s => s.TaskbarApps = "all", s => s.NotifyPlacement = "dock",
    s => s.TaskbarRightPrimaryOnly = false,
    s => s.HoverRise = true,
})
{
    var changed = settings.Clone();
    edit(changed);
    Require(!settings.SameAs(changed), "Every display preference must participate in the Save/Revert dirty state.");
}
var menuOff = BarSettings.FromJson(JsonNode.Parse("""{"windowsContextMenu":false}""")!.AsObject(), new());
Require(!menuOff.WindowsContextMenu && BarSettings.FromJson(menuOff.ToJson(), new()).SameAs(menuOff),
    "The Windows-style context menu must turn off and survive a round trip.");
Require(new BarSettings().WindowsContextMenu && BarSettings.FromJson(new JsonObject(), new()).WindowsContextMenu,
    "The Windows-style context menu is on by default, also for files written before it existed.");
Require(!new BarSettings().SameAs(menuOff), "The Windows-style context menu must participate in the Save/Revert dirty state.");
var displays = DesktopDisplay.Read();
Require(displays.Count > 0 && displays.Any(d => d.Primary), "Connected displays and system primary must be discoverable.");
Require(displays.All(d => d.Id.Length > 0 && d.Scale > 0 && d.Width > 0 && d.WorkWidth > 0), "Display IDs, geometry and scale must be valid.");
Console.WriteLine($"Settings round trips, validation, dirty tracking and display discovery passed ({displays.Count} connected display(s)).");

namespace Atrium.Settings
{
    // Pin icon resolution is outside this settings-model check.
    internal static class ShellIcons { public static string ExecutablePath(string path) => path; }
}
