using System.Numerics;
using Microsoft.UI;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Automation;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Input;
using Microsoft.UI.Xaml.Media;
using System.Runtime.InteropServices.WindowsRuntime;
using Microsoft.UI.Xaml.Media.Imaging;
using Microsoft.UI.Xaml.Shapes;
using Windows.UI;

namespace Atrium.Settings;

// The widget gallery (D43): every widget the core can show, as a small
// picture of it on a piece of glass over the specimen photo; a click puts one on the desktop.
public sealed partial class MainWindow
{
    private sealed record WidgetChoice(string Kind, string Size, string Name, string Note, string Category);

    // The gallery's groups, in order; a segmented control above shows all of them or one.
    private static readonly string[] WidgetCategoryNames = { "Clock", "Weather", "Calendar", "Productivity", "Photos", "System" };

    private static WidgetChoice[] WidgetChoices => new WidgetChoice[]
    {
        new("clock", "small", Loc.T("Clock"), Loc.T("Local time face"), "Clock"),
        new("clock", "medium", Loc.T("World clock"), Loc.T("Four city faces"), "Clock"),
        new("digital", "small", Loc.T("Digital clock"), Loc.T("Time, weekday and date"), "Clock"),
        new("digital", "medium", Loc.T("Digital clock"), Loc.T("Three more cities"), "Clock"),
        new("weather", "small", Loc.T("Weather"), Loc.T("Temperature and conditions"), "Weather"),
        new("weather", "medium", Loc.T("Weather"), Loc.T("Hourly forecast added"), "Weather"),
        new("weather", "large", Loc.T("Weather"), Loc.T("Five-day forecast added"), "Weather"),
        new("calendar", "small", Loc.T("Calendar"), Loc.T("Today and lunar date"), "Calendar"),
        new("calendar", "medium", Loc.T("Calendar"), Loc.T("Month calendar added"), "Calendar"),
        new("calendar", "large", Loc.T("Lunar month calendar"), Loc.T("Lunar date or festival under each day"), "Calendar"),
        new("notes", "small", Loc.T("Notes"), Loc.T("Click to type"), "Productivity"),
        new("reminders", "medium", Loc.T("Reminders"), Loc.T("Check off completed items"), "Productivity"),
        new("photos", "small", Loc.T("Photos"), Loc.T("One from your Pictures folder"), "Photos"),
        new("photos", "medium", Loc.T("Photos"), Loc.T("Changes every 10 minutes"), "Photos"),
        new("battery", "small", Loc.T("Battery"), Loc.T("Battery ring"), "System"),
        new("system", "small", Loc.T("System monitor"), Loc.T("CPU, memory and disk"), "System"),
        new("system", "medium", Loc.T("System monitor"), Loc.T("Three rings"), "System"),
        new("bluetooth", "small", Loc.T("Bluetooth devices"), Loc.T("Four device batteries"), "System"),
        new("bluetooth", "medium", Loc.T("Bluetooth devices"), Loc.T("Name and battery"), "System"),
        new("network", "small", Loc.T("Network"), Loc.T("Down and up rates, with the last minute"), "System"),
        new("network", "medium", Loc.T("Network"), Loc.T("Rates, the last minute and this session's traffic"), "System"),
    };

    private readonly Dictionary<string, FrameworkElement> _widgetSections = new();

    // Each group under its name; the control above narrows the gallery to one group.
    private void BuildWidgetGallery()
    {
        foreach (string category in WidgetCategoryNames)
        {
            var grid = new ResponsiveGrid { MinCellWidth = 180, Gap = 12 };
            foreach (var choice in WidgetChoices.Where(c => c.Category == category))
                grid.Children.Add(GalleryCard(choice));
            var section = new StackPanel { Spacing = 8 };
            section.Children.Add(new TextBlock { Text = Loc.T(category), Style = (Style)Application.Current.Resources["RowTitleStyle"], Margin = new Thickness(4, 0, 0, 0) });
            section.Children.Add(grid);
            _widgetSections[category] = section;
            WidgetGallery.Children.Add(section);
        }
    }

    private void OnWidgetCategoryChecked(object sender, RoutedEventArgs e)
    {
        if (sender is not RadioButton { Tag: string category }) return;
        foreach (var (name, section) in _widgetSections)
            section.Visibility = category == "All" || category == name ? Visibility.Visible : Visibility.Collapsed;
    }

    // The gallery only adds while the widgets are on.
    private void UpdateGallery()
    {
        WidgetGallery.Opacity = WidgetsSwitch.IsOn ? 1 : 0.45;
        WidgetGallery.IsHitTestVisible = WidgetsSwitch.IsOn;
        // Only why the gallery does not answer: the widgets are off.
        WidgetHint.Text = Loc.T("Turn on Desktop widgets first.");
        WidgetHint.Visibility = WidgetsSwitch.IsOn ? Visibility.Collapsed : Visibility.Visible;
    }

    // A gallery card: the widget on a piece of the bar's glass over the picture, rendered like the hero.
    private sealed record GalleryPreview(WidgetChoice Choice, Border Stage, Viewbox Face, Canvas Viewport)
    {
        public bool? DarkInk;
        public long Stamp;
        public string? Material;
    }

    private readonly List<GalleryPreview> _galleryStages = new();
    private BarSettings? _galleryMaterial;
    private int _galleryGen = -1;

    private FrameworkElement GalleryCard(WidgetChoice c)
    {
        var stage = new Border { Height = 118, CornerRadius = new CornerRadius(14), Background = PhotoBrush() };
        var face = new Viewbox { Stretch = Stretch.Fill };
        // The bitmap and the face share the last completed viewport. While a
        // resize render is pending, XAML stretches both by the same amount.
        var holder = new Canvas { Width = 1, Height = 1, IsHitTestVisible = false, Children = { face } };
        stage.Child = new Viewbox { Stretch = Stretch.Fill, Child = holder };
        var preview = new GalleryPreview(c, stage, face, holder);
        _galleryStages.Add(preview);
        stage.SizeChanged += (_, _) => RenderGalleryCard(preview);
        var name = new TextBlock
        {
            Text = c.Name + " · " + Loc.T(c.Size switch { "small" => "Small", "medium" => "Medium", _ => "Large" }),
            Style = (Style)Application.Current.Resources["RowTitleStyle"], Margin = new Thickness(4, 0, 4, 2),
            TextTrimming = TextTrimming.CharacterEllipsis,
        };
        var body = new StackPanel { Spacing = 10, Children = { stage, name } };
        var button = new Button
        {
            Content = body, Padding = new Thickness(10), CornerRadius = new CornerRadius(24), HorizontalAlignment = HorizontalAlignment.Stretch,
            HorizontalContentAlignment = HorizontalAlignment.Stretch, Background = Tokens.Brush("CardBrush"),
            BorderBrush = Tokens.Brush("CardStrokeBrush"), BorderThickness = new Thickness(1),
        };
        // Pointer over: the card rises a little.
        button.TranslationTransition = new Vector3Transition { Duration = TimeSpan.FromMilliseconds(180) };
        button.PointerEntered += (_, _) => button.Translation = new Vector3(0, -2, 0);
        button.PointerExited += (_, _) => button.Translation = Vector3.Zero;
        AutomationProperties.SetName(button, c.Size == "small" ? Loc.F("Add {0} (small)", c.Name)
            : c.Size == "medium" ? Loc.F("Add {0} (medium)", c.Name) : Loc.F("Add {0} (large)", c.Name));
        ToolTipService.SetToolTip(button, c.Note);
        button.Click += async (_, _) =>
        {
            var error = await _client.AddWidgetAsync(c.Kind, c.Size);
            WidgetHint.Text = error == null ? Loc.F("Added “{0}” to the desktop.", c.Name) : error.Contains("room") ? Loc.T("No room on the desktop: remove or shrink a widget first.") : Loc.F("Not added: {0}", error);
            WidgetHint.Visibility = Visibility.Visible;
        };
        return button;
    }

    // The gallery again when the picture or the material changed since it was drawn.
    private void RefreshGallery()
    {
        if (_galleryMaterial != null && _galleryMaterial.SameMaterial(_current) && _galleryMaterial.WidgetMaterial == _current.WidgetMaterial && _galleryGen == Photo.Generation) return;
        foreach (var preview in _galleryStages) RenderGalleryCard(preview);
    }

    private void RenderGalleryCard(GalleryPreview preview)
    {
        var (c, stage, face, viewport) = preview;
        double sw = stage.ActualWidth, sh = stage.ActualHeight;
        if (sw < 8 || sh < 8) return;
        _galleryMaterial = _current.Clone();
        _galleryGen = Photo.Generation;
        // The widget fitted into the card, 14 DIP of air around.
        var (w, h) = WidgetFaces.Size(c.Size);
        double k = Math.Min((sw - 28) / w, (sh - 28) / h);
        var tile = new GlassTile((sw - w * k) / 2, (sh - h * k) / 2, w * k, h * k, 22 * k);
        PostTiles(stage, sw, sh, k, new[] { tile }, null, frame =>
        {
            frame.Apply(stage);
            viewport.Width = sw;
            viewport.Height = sh;
            bool dark = frame.Lumas[0] > 0.5f;
            long stamp = WidgetFaces.Stamp(c.Kind);
            string material = _current.WidgetMaterial;
            if (preview.DarkInk != dark || preview.Stamp != stamp || preview.Material != material)
            {
                face.Child = WidgetFaces.Face(c.Kind, c.Size, dark, PhotoBrush(), material);
                ContinuousCorners.Install(face);
                preview.DarkInk = dark;
                preview.Stamp = stamp;
                preview.Material = material;
            }
            face.Width = tile.W;
            face.Height = tile.H;
            Canvas.SetLeft(face, tile.X);
            Canvas.SetTop(face, tile.Y);
        });
    }

    // ---- The widgets' glass on the CPU: the picture, the tiles of glass, their mean brightness ----

    // One drain for all tile previews, with at most two tiles rendering at once. Pending requests
    // replace only the same target; equal viewports share a backdrop. No full-size map or obsolete
    // photo is kept alive after the batch.
    private sealed record TileJob(object Target, int Id, double W, double H, double K, double Rs,
                                  IReadOnlyList<GlassTile> Tiles, GlassTile? Bar, BarSettings Material,
                                  Lazy<Photo> Photo, Action<TileFrame> Done, bool Overlays, bool Scaled);
    private readonly object _tileLock = new();
    private readonly Dictionary<object, TileJob> _pendingTiles = new();
    private bool _tilesBusy, _tilesStopped;
    private readonly Dictionary<object, int> _tileRequests = new();

    // A rendered stage: its pixels (BGRA), and each tile's brightness.
    private sealed record TileFrame(byte[] Px, int PW, int PH, double Rs, float[] Lumas, GlassOverlay[]? Overlays = null)
    {
        public WriteableBitmap Overlay(int index)
        {
            var image = Overlays![index];
            var bmp = new WriteableBitmap(image.Width, image.Height);
            using (var stream = bmp.PixelBuffer.AsStream()) stream.Write(image.Pixels);
            bmp.Invalidate();
            return bmp;
        }
        public void Apply(Border stage)
        {
            var brush = ContinuousCorners.Original(stage) as ImageBrush;
            var bmp = brush?.ImageSource as WriteableBitmap;
            if (bmp == null || bmp.PixelWidth != PW || bmp.PixelHeight != PH)
            {
                bmp = new WriteableBitmap(PW, PH);
                stage.Background = new ImageBrush { ImageSource = bmp, Stretch = Stretch.Fill };
            }
            using (var stream = bmp.PixelBuffer.AsStream()) stream.Write(Px, 0, Px.Length);
            bmp.Invalidate();
        }

        // The pixels under one tile, for a card that moves with its own piece of glass.
        public WriteableBitmap Crop(GlassTile t)
        {
            int x0 = Math.Clamp((int)Math.Round(t.X * Rs), 0, PW - 1), y0 = Math.Clamp((int)Math.Round(t.Y * Rs), 0, PH - 1);
            int w = Math.Clamp((int)Math.Round(t.W * Rs), 1, PW - x0), h = Math.Clamp((int)Math.Round(t.H * Rs), 1, PH - y0);
            var bmp = new WriteableBitmap(w, h);
            using (var stream = bmp.PixelBuffer.AsStream())
                for (int y = 0; y < h; y++) stream.Write(Px, ((y0 + y) * PW + x0) * 4, w * 4);
            bmp.Invalidate();
            return bmp;
        }
    }

    private void PostTiles(object target, double w, double h, double k, IReadOnlyList<GlassTile> tiles, GlassTile? bar,
                           Action<TileFrame> done, bool overlays = false, bool scaled = false)
    {
        double rs = Math.Clamp(Content.XamlRoot?.RasterizationScale ?? 1, 1, 2);
        int mine = _tileRequests[target] = (_tileRequests.TryGetValue(target, out int n) ? n : 0) + 1;
        lock (_tileLock)
        {
            if (_tilesStopped) return;
            _pendingTiles[target] = new TileJob(target, mine, w, h, k, rs, tiles, bar, _current.Clone(), Photo.Capture(), done, overlays, scaled);
            if (_tilesBusy) return;
            _tilesBusy = true;
        }
        Task.Run(RenderTiles);
    }

    private void StopTiles()
    {
        lock (_tileLock) { _tilesStopped = true; _pendingTiles.Clear(); }
    }

    private void RenderTiles()
    {
        Backdrop? bg = null;
        (double W, double H, double Rs, Lazy<Photo> Photo)? backdrop = null;
        while (true)
        {
            TileJob[] jobs;
            lock (_tileLock)
            {
                if (_pendingTiles.Count == 0) { _tilesBusy = false; return; }
                jobs = _pendingTiles.Values.ToArray();
                _pendingTiles.Clear();
            }
            foreach (var group in jobs.GroupBy(j => (j.W, j.H, j.Rs, j.Photo)))
            {
                var key = group.Key;
                int pw = (int)Math.Round(key.W * key.Rs), ph = (int)Math.Round(key.H * key.Rs);
                try
                {
                    if (bg == null || backdrop != key)
                    {
                        bg = Backdrop.Create(pw, ph, key.W, key.H, 0, 0, key.Rs, key.Photo.Value);
                        backdrop = key;
                    }
                    Parallel.ForEach(group, new ParallelOptions { MaxDegreeOfParallelism = 2 }, job =>
                    {
                        lock (_tileLock)
                            if (_tilesStopped || _pendingTiles.ContainsKey(job.Target)) return;
                        TileFrame frame;
                        if (job.Overlays)
                        {
                            // Scaled: the widget map, whose picture comes along (its cards move over it).
                            var cards = GlassRenderer.CardOverlays(bg, job.Material, job.Rs, job.Tiles, job.Scaled ? job.K : 0);
                            frame = new TileFrame(job.Scaled ? bg.Base : Array.Empty<byte>(), pw, ph, job.Rs, cards.Select(c => c.Luma).ToArray(), cards);
                        }
                        else
                        {
                            var px = new byte[bg.Base.Length];
                            var lumas = GlassRenderer.DrawTiles(bg, px, job.Material, job.K, job.Rs, job.Tiles, job.Bar);
                            frame = new TileFrame(px, pw, ph, job.Rs, lumas);
                        }
                        DispatcherQueue.TryEnqueue(() =>
                        {
                            if (!_tilesStopped && _tileRequests[job.Target] == job.Id) job.Done(frame);
                        });
                    });
                }
                catch (Exception e)
                {
                    System.Diagnostics.Debug.WriteLine("tile preview render failed: " + e);
                }
            }
        }
    }
}
