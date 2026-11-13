using System.Diagnostics;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;

namespace Atrium.Settings;

public sealed partial class MainWindow
{
    private List<DesktopDisplay> _displays = new();

    private void InitDisplays()
    {
        DisplayDelay.Configure(Loc.T("Cross-display dwell time"), 100, 2000, 50, "ms");
        DisplayDelay.UserValueChanged += value =>
        {
            if (_loading) return;
            _current.DockDisplayDelay = value;
            Changed();
        };
        RefreshDisplays();
    }

    private void RefreshDisplays()
    {
        bool loading = _loading;
        _loading = true;
        _displays = DesktopDisplay.Read();
        ConnectedDisplayList.ItemsSource = _displays;
        DisplaySummary.Text = Loc.F("{0} display(s) connected", _displays.Count);
        var choices = new List<DesktopDisplay> { new("", Loc.T("Follow the system primary display"), "", true) };
        choices.AddRange(_displays);
        // Keep an unplugged selection visible; reconnection restores it without resaving.
        foreach (string id in new[] { _current.DockDisplay, _current.DesktopDisplay }.Distinct())
            if (id.Length > 0 && !choices.Any(d => d.Id == id))
                choices.Add(new(id, Loc.T("Disconnected display (using the primary until it returns)"), "", false));
        DockDisplayPicker.ItemsSource = choices;
        DesktopDisplayPicker.ItemsSource = choices;
        DockDisplayPicker.SelectedItem = choices.First(d => d.Id == _current.DockDisplay);
        DesktopDisplayPicker.SelectedItem = choices.First(d => d.Id == _current.DesktopDisplay);
        DisplayFallback.Text = new[] { _current.DockDisplayMode == "fixed" ? _current.DockDisplay : "", _current.DesktopDisplay }
            .Any(id => id.Length > 0 && !_displays.Any(d => d.Id == id))
            ? Loc.T("The chosen display is disconnected; the primary is used and the choice returns when it reconnects.") : "";
        DisplayFallback.Visibility = DisplayFallback.Text.Length > 0 ? Visibility.Visible : Visibility.Collapsed;
        _loading = loading;
    }

    private void LoadDisplayControls()
    {
        (_current.DockDisplayMode switch { "follow" => DockDisplayFollow, "fixed" => DockDisplayFixed, "all" => DockDisplayAll, _ => DockDisplayPrimary }).IsChecked = true;
        DisplayDelay.SetValue(_current.DockDisplayDelay);
        TaskbarAppsPicker.SelectedIndex = _current.TaskbarApps switch { "primaryAndWindow" => 1, "window" => 2, _ => 0 };
        TaskbarRightPrimaryOnlySwitch.IsOn = _current.TaskbarRightPrimaryOnly;
        UpdateDisplayControls();
        RefreshDisplays();
    }

    private void UpdateDisplayControls()
    {
        DockDisplayPicker.IsEnabled = _current.DockDisplayMode == "fixed";
        DisplayDelay.IsEnabled = _current.DockDisplayMode == "follow";
    }

    private void OnDisplayModeChecked(object sender, RoutedEventArgs e)
    {
        if (_loading) return;
        _current.DockDisplayMode = (string)((RadioButton)sender).Tag;
        UpdateDisplayControls();
        RefreshDisplays();
        Changed();
    }

    private void OnDockDisplayChanged(object sender, SelectionChangedEventArgs e)
    {
        if (_loading || e.AddedItems.Count == 0) return;
        _current.DockDisplay = ((DesktopDisplay)e.AddedItems[0]).Id;
        Changed();
    }

    private void OnDesktopDisplayChanged(object sender, SelectionChangedEventArgs e)
    {
        if (_loading || e.AddedItems.Count == 0) return;
        _current.DesktopDisplay = ((DesktopDisplay)e.AddedItems[0]).Id;
        Changed();
    }

    private void OnTaskbarAppsChanged(object sender, SelectionChangedEventArgs e)
    {
        if (_loading || TaskbarAppsPicker.SelectedIndex < 0) return;
        _current.TaskbarApps = TaskbarAppsPicker.SelectedIndex switch { 1 => "primaryAndWindow", 2 => "window", _ => "all" };
        Changed();
    }

    private void OnTaskbarRightPrimaryOnlyToggled(object sender, RoutedEventArgs e)
    {
        if (_loading) return;
        _current.TaskbarRightPrimaryOnly = TaskbarRightPrimaryOnlySwitch.IsOn;
        Changed();
    }

    private void OnRefreshDisplays(object sender, RoutedEventArgs e) => RefreshDisplays();
    private void OnSystemDisplays(object sender, RoutedEventArgs e) => Process.Start(new ProcessStartInfo("ms-settings:display") { UseShellExecute = true });
}
