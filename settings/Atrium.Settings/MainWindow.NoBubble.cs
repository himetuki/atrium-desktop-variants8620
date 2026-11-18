using System.Collections.ObjectModel;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;

namespace Atrium.Settings;

// A game that keeps the input-method bubble quiet while it is fullscreen in front: one entry of
// imeNoBubbleGames as the user sees it (a program name, a package prefix or an install folder).
public sealed class NoBubbleVm
{
    private readonly MainWindow _owner;

    internal NoBubbleVm(MainWindow owner, string entry)
    {
        _owner = owner;
        Entry = entry;
        RemoveName = Loc.F("Remove {0}", entry);
    }

    public string Entry { get; }
    public string RemoveName { get; }
    public void Remove() => _owner.RemoveNoBubble(this);
}

public sealed partial class MainWindow
{
    private readonly ObservableCollection<NoBubbleVm> _noBubble = new();

    private void InitNoBubble()
    {
        NoBubbleList.ItemsSource = _noBubble;
    }

    // The effective list: imeNoBubbleGames, then the stageExcluded entries the user has not pinned,
    // as the core builds it at startup. A removed game goes into imeNoBubblePinned, so it stays out.
    private void RebuildNoBubble()
    {
        var pinned = new HashSet<string>(_current.ImeNoBubblePinned, StringComparer.OrdinalIgnoreCase);
        var entries = new List<string>(_current.ImeNoBubbleGames);
        foreach (string entry in _current.StageExcluded)
            if (!pinned.Contains(entry) && !entries.Contains(entry, StringComparer.OrdinalIgnoreCase))
                entries.Add(entry);
        if (!_noBubble.Select(vm => vm.Entry).SequenceEqual(entries, StringComparer.OrdinalIgnoreCase))
        {
            _noBubble.Clear();
            foreach (string entry in entries) _noBubble.Add(new NoBubbleVm(this, entry));
        }
        UpdateNoBubbleState();
    }

    // The list only means something while the pause is on.
    private void UpdateNoBubbleState()
    {
        bool on = _current.ImeBubbleGamePause;
        bool empty = _noBubble.Count == 0;
        NoBubbleEmpty.Visibility = empty ? Visibility.Visible : Visibility.Collapsed;
        NoBubbleList.Visibility = empty ? Visibility.Collapsed : Visibility.Visible;
        NoBubbleList.IsEnabled = on;
        NoBubbleSection.Opacity = on ? 1 : 0.45;
    }

    internal void RemoveNoBubble(NoBubbleVm vm)
    {
        if (!_noBubble.Contains(vm)) return;
        string entry = vm.Entry;
        _current.ImeNoBubbleGames.RemoveAll(e => string.Equals(e, entry, StringComparison.OrdinalIgnoreCase));
        if (!_current.ImeNoBubblePinned.Contains(entry, StringComparer.OrdinalIgnoreCase))
            _current.ImeNoBubblePinned.Add(entry);
        RebuildNoBubble();
        Changed();
    }

    private void OnImeBubbleGamePauseToggled(object sender, RoutedEventArgs e)
    {
        if (_loading) return;
        _current.ImeBubbleGamePause = ImeBubbleGamePauseSwitch.IsOn;
        UpdateNoBubbleState();
        Changed();
    }
}
