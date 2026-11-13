using System.Diagnostics;
using System.Numerics;
using System.Runtime.InteropServices.WindowsRuntime;
using Microsoft.UI;
using Microsoft.UI.Text;
using Microsoft.UI.Windowing;
using Microsoft.UI.Xaml.Automation;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Controls.Primitives;
using Microsoft.UI.Xaml.Input;
using Microsoft.UI.Xaml.Media;
using Microsoft.UI.Xaml.Media.Imaging;
using Microsoft.UI.Xaml.Shapes;
using Windows.Foundation;
using Windows.UI;

namespace Atrium.Settings;

// The design language page, in two layers. First what Atrium looks and moves like, to look at and
// touch: the two glasses side by side, corners and curvature, type, spacing, colour, the dock and a
// notification, the curves. Then, behind "查看详细参数", the numbers: every one of them taken from
// the code (the curves use the core's own formulas, native/core/motion.h; the materials are
// BarSettings' presets; the colours are this app's resources).
public sealed partial class MainWindow
{
    private static class Motion
    {
        public static double EaseOut(double t)
        {
            t = Math.Clamp(t, 0, 1);
            double u = 1 - t;
            return 1 - u * u * u;
        }

        public static double EaseOutBack(double t, double s)
        {
            t = Math.Clamp(t, 0, 1) - 1;
            return 1 + (s + 1) * t * t * t + s * t * t;
        }

        // A spring from 0 at rest towards 1, at time t (closed form, as motion::Spring::Step).
        public static double Spring(double t, double response, double damping)
        {
            double w0 = 2 * Math.PI / response, z = damping, y;
            if (Math.Abs(z - 1) < 1e-3)
                y = -Math.Exp(-w0 * t) * (1 + w0 * t);
            else if (z < 1)
            {
                double a = z * w0, wd = w0 * Math.Sqrt(1 - z * z);
                y = -Math.Exp(-a * t) * (Math.Cos(wd * t) + a / wd * Math.Sin(wd * t));
            }
            else
            {
                double q = w0 * Math.Sqrt(z * z - 1), r1 = -z * w0 + q, r2 = -z * w0 - q, c2 = r1 / (r2 - r1);
                y = (-1 - c2) * Math.Exp(r1 * t) + c2 * Math.Exp(r2 * t);
            }
            return 1 + y;
        }

        public static double Approach(double t, double rate) => 1 - Math.Exp(-rate * t);

        // The launch hop (render/bar_painter.cpp): sin², no speed at either end.
        public static double Hop(double t, double seconds)
        {
            if (t <= 0 || t >= seconds) return 0;
            double s = Math.Sin(Math.PI * t / seconds);
            return s * s;
        }
    }

    private sealed record Curve(string Name, string Spec, string Formula, string Use, double Seconds, Func<double, double> At);

    private static Curve[] Curves => new Curve[]
    {
        new(Loc.T("kPop · Spring"), Loc.T("Duration 0.36 s · Bounce 0.38"), Loc.T("Closed-form spring, damping ratio 0.62; split into 12 Hermite segments for DirectComposition"), Loc.T("Popup appears: scale 0.92 → 1, peaks at 1.0067"), 0.6, t => Motion.Spring(t, 0.36, 0.62)),
        new(Loc.T("kSmooth · Spring"), Loc.T("Duration 0.48 s · Bounce 0.23"), Loc.T("Closed-form spring, damping ratio 0.77"), Loc.T("Status ring: battery, volume and brightness"), 0.8, t => Motion.Spring(t, 0.48, 0.77)),
        new(Loc.T("Stack · Critically damped spring"), Loc.T("Duration 0.60 s · Bounce 0"), Loc.T("Closed-form spring, damping ratio 1"), Loc.T("Scrolling through notifications snaps to one"), 1.0, t => Motion.Spring(t, 0.60, 1.0)),
        new(Loc.T("EaseOut · Cubic ease-out"), "1 − (1 − t)³ · 320 ms", "1 − (1 − t)³", Loc.T("Page changes, fades, staggered lists, ink shifts"), 0.32, t => Motion.EaseOut(t / 0.32)),
        new(Loc.T("EaseOutBack · Back ease-out"), "s = 1.5 · 260 ms", "1 + (s + 1)(t − 1)³ + s(t − 1)²", Loc.T("Start menu tiles appearing"), 0.26, t => Motion.EaseOutBack(t / 0.26, 1.5)),
        new(Loc.T("Approach · Exponential approach"), Loc.T("In 20/s · Out 11/s"), Loc.T("1 − e^(−rate · t), continuing from the current value each frame"), Loc.T("Hover and press plates"), 0.3, t => Motion.Approach(t, 20)),
        new(Loc.T("Launch · One small hop"), Loc.T("sin² · 480 ms · 0.32 × icon height"), Loc.T("sin²(πt / 0.48); no velocity jump at takeoff or landing"), Loc.T("Opening a taskbar app: one hop only"), 0.48, t => Motion.Hop(t, 0.48)),
    };

    // The type ramp (DESIGN.md §1.2): name, size, MiSans file, its weight, macOS counterpart, where.
    private static (string Name, double Size, string Font, string Weight, string Mac, string Use)[] TypeRamp =>
        new (string Name, double Size, string Font, string Weight, string Mac, string Use)[]
    {
        ("Large Title", 30, "TitleFont", "Semibold 520", "Large Title · 26", Loc.T("Page titles, product name")),
        ("Title", 20, "TitleFont", "Semibold 520", "Title 1 · 22", Loc.T("Empty-state titles, calendar month")),
        ("Value", 15, "EmphasisFont", "Demibold 450", "—", Loc.T("Slider values (tabular figures)")),
        ("Row", 15, "TextFont", "Medium 380", "Body · 13", Loc.T("Settings row titles")),
        ("Body", 13.5, "TextFont", "Medium 380", "Body · 13", Loc.T("Panel body, notification content")),
        ("Section", 13, "EmphasisFont", "Demibold 450", "—", Loc.T("Group headings")),
        ("Note", 13, "TextFont", "Medium 380", "Callout · 12", Loc.T("Notes under a row")),
        ("Footnote", 12, "TextFont", "Medium 380", "Footnote · 10", Loc.T("Footnotes; glass rises in Demibold")),
    };

    // Logical width from, typical screens, bar height, icon, bottom margin, magnification.
    private static (double From, string Screens, int Height, int Icon, int Margin, double Magnify)[] SizeClasses =>
        new (double From, string Screens, int Height, int Icon, int Margin, double Magnify)[]
    {
        (0, Loc.T("1366×768 @100%, 1920×1080 @150%"), 52, 26, 8, 1.5),
        (1280, Loc.T("1920×1080 @125%, 2560×1600 @175%, 3200×2000 @200%"), 60, 30, 10, 1.7),
        (1680, Loc.T("1920×1080 @100%, 2560×1440 @125%"), 64, 32, 12, 1.7),
        (2400, Loc.T("2560×1440 @100%, 3840×2160 @150%"), 68, 34, 14, 1.6),
    };

    // The spacing scale (DESIGN.md §4.1) and the corner ladder (§2.1).
    private static (string Name, int Value, string Use)[] Spacing => new[]
    {
        ("2xs", 4, Loc.T("Icons and badges, tight elements")), ("xs", 8, Loc.T("Adjacent elements in a group")), ("s", 10, Loc.T("Cards inside a panel (kGap)")),
        ("m", 12, Loc.T("Popup and bar (kPopupGapDip)")), ("l", 14, Loc.T("Panel padding (kPad), glass shadow")), ("xl", 16, Loc.T("Gap between widgets")),
        ("2xl", 24, Loc.T("Widgets to the screen edge, between sections")),
    };

    private static (string Element, string Radius, string Rule)[] Corners => new[]
    {
        (Loc.T("Bar"), Loc.T("cornerRadius, default 30"), Loc.T("At most half the bar height: a capsule by default")),
        (Loc.T("Popup panel"), Loc.T("Same as the bar, 8 to height / 2"), Loc.T("Popup and bar share one material and one shape")),
        (Loc.T("Modules inside a panel"), Loc.T("min(panel radius, 22)"), Loc.T("A very round panel does not turn its modules into capsules")),
        (Loc.T("Tray icon plate"), "clamp(R − m − 2, 6, 12)", Loc.T("Concentric: outer radius minus the inset")),
        (Loc.T("Desktop widget"), Loc.T("22 (cell 164)"), Loc.T("About 13% of the side, as macOS widgets")),
        (Loc.T("Capsule control"), Loc.T("Height / 2"), Loc.T("G3 continuous ends; round knobs stay circular")),
        (Loc.T("Hover and press plates"), "8–12", Loc.T("At most the container radius minus the inset")),
    };

    private bool _designBuilt;
    private readonly List<(double Start, Curve Curve, Action<double> Show)> _playing = new();
    private bool _designTicking;
    private static readonly Stopwatch DesignClock = Stopwatch.StartNew();
    private StackPanel? _designDetails;

    private static Style StyleOf(string key) => (Style)Application.Current.Resources[key];

    private static TextBlock Text(string text, string style, double? size = null)
    {
        var t = new TextBlock { Text = text, Style = StyleOf(style) };
        if (size is double s) t.FontSize = s;
        return t;
    }

    private static TextBlock Tabular(TextBlock t)
    {
        Microsoft.UI.Xaml.Documents.Typography.SetNumeralAlignment(t, FontNumeralAlignment.Tabular);
        return t;
    }

    private static Border Card(UIElement child) => new() { Style = StyleOf("TileStyle"), Padding = new Thickness(20, 16, 20, 16), Child = child };

    private static StackPanel Section(string label, string? note, UIElement body)
    {
        var s = new StackPanel { Spacing = 12 };
        var title = Text(label, "TitleStyle");
        title.Margin = new Thickness(4, 0, 0, 0);
        s.Children.Add(title);
        if (note != null)
        {
            var n = Text(note, "RowNoteStyle");
            n.Margin = new Thickness(4, -4, 0, 2);
            n.MaxWidth = 760;
            n.HorizontalAlignment = HorizontalAlignment.Left;
            s.Children.Add(n);
        }
        s.Children.Add(body);
        return s;
    }

    private void BuildDesignPage()
    {
        if (_designBuilt) return;
        _designBuilt = true;
        // ---- First layer: to look at and to touch ----
        DesignStack.Children.Add(Principles());
        DesignStack.Children.Add(Section(Loc.T("Material"), Loc.T("One shader, two parameter sets: liquid glass refracts only at the rim and is almost clear in the middle; frosted glass blurs the backdrop into colour fields and hardly refracts. Drag the divider left and right to compare."),
            MaterialCompare()));
        DesignStack.Children.Add(Section(Loc.T("Geometry"), Loc.T("One G3 continuous corner system, keeping the large radii and capsule proportions."),
            GeometryCard()));
        DesignStack.Children.Add(Section(Loc.T("Typography"), Loc.T("Three MiSans weights. At most three sizes per screen; hierarchy first uses opacity, then size, then weight; all figures are tabular."), TypeCard(false)));
        DesignStack.Children.Add(Section(Loc.T("Spacing and colour"), Loc.T("One spacing scale; one accent colour, everything else neutral grey. Ink on glass follows the brightness behind it."), SpaceAndColour()));
        DesignStack.Children.Add(Section(Loc.T("Interaction"), Loc.T("Move the pointer over the taskbar: icons grow in a wave (the current magnification); click an icon to see the launch hop. Click a notification card to bring it back."), InteractionCard()));
        DesignStack.Children.Add(Section(Loc.T("Motion"), Loc.T("Animations can change direction from where they are and advance in real time; springs keep velocity, while the widgets' ease-out currently restarts. Click a card to play."), CurveGrid()));

        // ---- Second layer: the numbers ----
        var toggle = new Button { HorizontalAlignment = HorizontalAlignment.Left, Padding = new Thickness(16, 7, 14, 8) };
        var label = new TextBlock { Text = Loc.T("Show details") };
        var chevron = new FontIcon { Glyph = "\uE70D", FontSize = 11, Margin = new Thickness(10, 1, 0, 0) };
        chevron.RenderTransformOrigin = new Point(0.5, 0.5);
        chevron.RenderTransform = new RotateTransform();
        toggle.Content = new StackPanel { Orientation = Orientation.Horizontal, Children = { label, chevron } };
        AutomationProperties.SetName(toggle, Loc.T("Show details"));
        _designDetails = new StackPanel { Spacing = 36, Visibility = Visibility.Collapsed };
        toggle.Click += (_, _) =>
        {
            RefreshDesignDetails(true);
            bool open = _designDetails.Visibility != Visibility.Visible;
            _designDetails.Visibility = open ? Visibility.Visible : Visibility.Collapsed;
            label.Text = open ? Loc.T("Hide details") : Loc.T("Show details");
            ((RotateTransform)chevron.RenderTransform).Angle = open ? 180 : 0;
            if (open) AnimateIn(_designDetails);
        };
        var foot = new StackPanel { Spacing = 8 };
        foot.Children.Add(Text(Loc.T("Technical details"), "TitleStyle"));
        var why = Text(Loc.T("Animation curves and durations, every material preset value, sizes and weights, corners and spacing: the same values Atrium runs with."), "RowNoteStyle");
        why.Margin = new Thickness(0, 0, 0, 6);
        foot.Children.Add(why);
        foot.Children.Add(toggle);
        foot.Margin = new Thickness(4, 0, 0, 0);
        DesignStack.Children.Add(foot);
        DesignStack.Children.Add(_designDetails);
    }

    // The details are built when they are opened, and again with the current settings each time the page shows.
    private void RefreshDesignDetails(bool opening = false)
    {
        if (_designDetails == null || (!opening && _designDetails.Visibility != Visibility.Visible)) return;
        _designDetails.Children.Clear();
        BuildDesignDetails(_designDetails);
        ContinuousCorners.Install(_designDetails);
    }

    private void BuildDesignDetails(StackPanel into)
    {
        into.Children.Add(Section(Loc.T("Motion parameters"), Loc.T("Springs are given Apple's way: duration and bounce (= 1 − damping ratio). Popup animations play in DirectComposition; the rest are computed per frame by the render thread on the composition clock."),
            Table(new[] { Loc.T("Curve"), Loc.T("Parameters"), Loc.T("Form"), Loc.T("Used for") }, new[] { 1.2, 1.2, 2.0, 1.6 },
                Curves.Select(c => new[] { c.Name, c.Spec, c.Formula, c.Use }))));
        var liquid = BarSettings.Liquid();
        var frosted = BarSettings.Frosted();
        string P(double v) => $"{v * 100:0}%";
        string Hex(uint c) => "#" + c.ToString("X6");
        into.Children.Add(Section(Loc.T("Material parameters"), Loc.T("Two presets and the current settings. The bar, popups and widgets share one shader (glass.hlsl) and one parameter set."),
            Table(new[] { Loc.T("Parameters"), Loc.T("Liquid glass"), Loc.T("Frosted glass"), Loc.T("Current") }, new[] { 1.4, 1.0, 1.0, 1.0 }, new[]
            {
                new[] { Loc.T("Transparency"), P(liquid.Transparency), P(frosted.Transparency), P(_current.Transparency) },
                new[] { Loc.T("Blur"), P(liquid.Blur), P(frosted.Blur), P(_current.Blur) },
                new[] { Loc.T("Refraction"), $"{liquid.Refraction:0} px", $"{frosted.Refraction:0} px", $"{_current.Refraction:0} px" },
                new[] { Loc.T("Refraction edge"), $"{liquid.Edge:0} px", $"{frosted.Edge:0} px", $"{_current.Edge:0} px" },
                new[] { Loc.T("Dispersion"), P(liquid.Dispersion), P(frosted.Dispersion), P(_current.Dispersion) },
                new[] { Loc.T("Highlight"), P(liquid.Highlight), P(frosted.Highlight), P(_current.Highlight) },
                new[] { Loc.T("Shadow"), P(liquid.Shadow), P(frosted.Shadow), P(_current.Shadow) },
                new[] { Loc.T("Tint"), Hex(liquid.Tint), Hex(frosted.Tint), Hex(_current.Tint) },
            })));
        into.Children.Add(Section(Loc.T("Typography parameters"), Loc.T("Weights are never synthesized: the style picks a font file. MiSans's weights sit one step below their names: Medium 380 as regular, Demibold 450 as medium, Semibold 520 as semibold."),
            TypeCard(true)));
        into.Children.Add(Section(Loc.T("Corner radius"), Loc.T("Every outline resolves as G3: curvature and its rate are zero where straight edges meet. Cards 24, buttons take half their height; circles keep constant curvature."),
            Table(new[] { Loc.T("Element"), Loc.T("Corner radius"), Loc.T("Rule") }, new[] { 1.1, 1.3, 2.2 }, Corners.Select(c => new[] { c.Element, c.Radius, c.Rule }))));
        into.Children.Add(RadiusCard());
        into.Children.Add(Section(Loc.T("Spacing"), null,
            Table(new[] { Loc.T("Name"), Loc.T("Value"), Loc.T("Use") }, new[] { 0.6, 0.5, 2.6 }, Spacing.Select(s => new[] { s.Name, s.Value.ToString(), s.Use }))));
        into.Children.Add(Section(Loc.T("Dimensions"), Loc.T("Resolution becomes logical width (pixels ÷ scale), then the bar's size is chosen. Icons are half the bar height; the bar is about 6% of the logical screen height."), SizeCard()));
        into.Children.Add(Section(Loc.T("Ink"), Loc.T("Decided by the brightness behind the glass: above 0.50 dark ink, below 0.42 light ink, with no switch in between; a 450 ms ease-out crossfade on change. Ink has three opacity levels."), InkCard()));
        var link = new HyperlinkButton
        {
            Content = Loc.T("Full specification: docs/DESIGN.md"),
            NavigateUri = new Uri("https://github.com/wynter-yaxley/atrium-desktop/blob/main/docs/DESIGN.md"),
            Padding = new Thickness(0),
        };
        into.Children.Add(link);
    }

    // A plain table on a card: a header in footnote ink, rows split by hairlines, numbers tabular.
    private static Border Table(string[] head, double[] widths, IEnumerable<string[]> rows)
    {
        var g = new Grid { ColumnSpacing = 16 };
        foreach (var w in widths) g.ColumnDefinitions.Add(new ColumnDefinition { Width = new GridLength(w, GridUnitType.Star) });
        int r = 0;
        void Row(string[] cells, bool header)
        {
            g.RowDefinitions.Add(new RowDefinition { Height = GridLength.Auto });
            if (!header && r > 1)
            {
                var line = new Rectangle { Height = 1, Fill = Tokens.Brush("SeparatorBrush"), VerticalAlignment = VerticalAlignment.Top };
                Grid.SetRow(line, r);
                Grid.SetColumnSpan(line, head.Length);
                g.Children.Add(line);
            }
            for (int c = 0; c < cells.Length && c < head.Length; c++)
            {
                var t = Tabular(Text(cells[c], header ? "FootnoteStyle" : c == 0 ? "RowTitleStyle" : "RowNoteStyle"));
                if (!header && c == 0) t.FontSize = 14;
                t.TextWrapping = TextWrapping.Wrap;
                t.Margin = new Thickness(0, header ? 0 : 9, 0, header ? 6 : 9);
                Grid.SetRow(t, r);
                Grid.SetColumn(t, c);
                g.Children.Add(t);
            }
            r++;
        }
        Row(head, true);
        foreach (var row in rows) Row(row, false);
        return Card(g);
    }

    private static UIElement Principles()
    {
        var grid = new ResponsiveGrid { MinCellWidth = 240, Gap = 16, StretchLastRow = true };
        (string N, string Title, string Body)[] items =
        {
            ("01", Loc.T("Yield to content"), Loc.T("Glass carries content. Ink follows the brightness behind it; decoration stays within what keeps text legible.")),
            ("02", Loc.T("Motion behaves like real objects"), Loc.T("It can change direction mid-flight with continuous position; springs keep velocity, while the widgets' ease-out currently restarts.")),
            ("03", Loc.T("Same things, same treatment"), Loc.T("The same kind of element uses one curve, one radius and one type size.")),
        };
        foreach (var (n, title, body) in items)
        {
            var s = new StackPanel { Spacing = 8 };
            var num = Text(n, "ValueStyle");
            num.HorizontalAlignment = HorizontalAlignment.Left;
            num.Foreground = Tokens.Brush("AccentBrush");
            s.Children.Add(num);
            s.Children.Add(Text(title, "RowTitleStyle"));
            s.Children.Add(Text(body, "RowNoteStyle"));
            grid.Children.Add(Card(s));
        }
        return Section(Loc.T("Principles"), null, grid);
    }

    // ---- Material: the two presets on one photo, split by a handle the pointer moves ----

    private UIElement MaterialCompare()
    {
        const double H = 240;
        var frosted = new Border { CornerRadius = new CornerRadius(20), Background = PhotoBrush() };
        var liquid = new Border { CornerRadius = new CornerRadius(20), Background = PhotoBrush() };
        // Keep the split's XAML rectangle clip on a separate, unrounded host.
        // XAML otherwise replaces the same visual Clip used for the photo's
        // continuous boundary, and attempts to treat it as its own inset clip.
        var reveal = new Grid { Children = { liquid } };
        var split = new Rectangle { Width = 2, Fill = new SolidColorBrush(Color.FromArgb(230, 255, 255, 255)), HorizontalAlignment = HorizontalAlignment.Left };
        var knob = new Border
        {
            Width = 30, Height = 30, CornerRadius = new CornerRadius(15), Background = new SolidColorBrush(Color.FromArgb(240, 255, 255, 255)),
            HorizontalAlignment = HorizontalAlignment.Left, VerticalAlignment = VerticalAlignment.Center,
            Child = new FontIcon { Glyph = "\uE76F", FontSize = 12, Foreground = new SolidColorBrush(Color.FromArgb(200, 0x1D, 0x1D, 0x1F)) },
            Shadow = new ThemeShadow(), Translation = new Vector3(0, 0, 8),
        };
        Border Tag(string text, HorizontalAlignment side) => new()
        {
            HorizontalAlignment = side, VerticalAlignment = VerticalAlignment.Top, Margin = new Thickness(14), CornerRadius = new CornerRadius(8),
            Padding = new Thickness(8, 3, 8, 4), Background = new SolidColorBrush(Color.FromArgb(150, 0, 0, 0)),
            Child = new TextBlock { Text = text, FontSize = 12, Foreground = new SolidColorBrush(Colors.White) },
        };
        var stage = new Grid { Height = H, Background = new SolidColorBrush(Colors.Transparent) };
        stage.Children.Add(frosted);
        stage.Children.Add(reveal);
        stage.Children.Add(split);
        stage.Children.Add(knob);
        stage.Children.Add(Tag(Loc.T("Liquid glass"), HorizontalAlignment.Left));
        stage.Children.Add(Tag(Loc.T("Frosted glass"), HorizontalAlignment.Right));
        stage.Children.Add(new Border { CornerRadius = new CornerRadius(20), BorderThickness = new Thickness(1), BorderBrush = new SolidColorBrush(Color.FromArgb(31, 0, 0, 0)), IsHitTestVisible = false });
        AutomationProperties.SetName(stage, Loc.T("Material comparison"));

        double at = 0.5, width = 0;
        void Place()
        {
            double x = Math.Round(width * at);
            reveal.Clip = new RectangleGeometry { Rect = new Rect(0, 0, Math.Max(0, x), H) };
            split.Translation = new Vector3((float)(x - 1), 0, 0);
            knob.Translation = new Vector3((float)(x - 15), 0, 8);
        }
        bool dragging = false;
        stage.PointerPressed += (_, e) => { dragging = stage.CapturePointer(e.Pointer); at = Math.Clamp(e.GetCurrentPoint(stage).Position.X / Math.Max(1, width), 0.04, 0.96); Place(); };
        stage.PointerMoved += (_, e) => { if (!dragging) return; at = Math.Clamp(e.GetCurrentPoint(stage).Position.X / Math.Max(1, width), 0.04, 0.96); Place(); };
        stage.PointerReleased += (_, e) => { dragging = false; stage.ReleasePointerCapture(e.Pointer); };
        stage.PointerCaptureLost += (_, _) => dragging = false;

        // Both renders at the stage's size, on the thread pool; redone when the width changes much,
        // and over a new picture when another is picked.
        double rendered = 0;
        int request = 0; // the newest render: an older one finishing later is dropped
        void Render()
        {
            rendered = width;
            double rs = Math.Clamp(stage.XamlRoot?.RasterizationScale ?? 1, 1, 2), w = width;
            int mine = ++request;
            int photoGeneration = Photo.Generation;
            var photo = Photo.Capture();
            Task.Run(() =>
            {
                var a = RenderMaterial(BarSettings.Liquid(), w, H, rs, photo.Value);
                var b = RenderMaterial(BarSettings.Frosted(), w, H, rs, photo.Value);
                DispatcherQueue.TryEnqueue(() =>
                {
                    if (mine != request || photoGeneration != Photo.Generation) return;
                    liquid.Background = a.Brush();
                    frosted.Background = b.Brush();
                    Ready("design", photoGeneration);
                });
            });
        }
        stage.SizeChanged += (_, e) =>
        {
            width = e.NewSize.Width;
            Place();
            if (Math.Abs(width - rendered) >= 48) Render();
        };
        PhotoChanged += () => { if (width > 0) Render(); };
        _compareBuilt = true;
        return stage;
    }

    private sealed record Rendered(int W, int H, byte[] Pixels)
    {
        public ImageBrush Brush()
        {
            var bmp = new WriteableBitmap(W, H);
            using (var stream = bmp.PixelBuffer.AsStream()) stream.Write(Pixels, 0, Pixels.Length);
            bmp.Invalidate();
            return new ImageBrush { ImageSource = bmp, Stretch = Stretch.Fill };
        }
    }

    // A long capsule of the material across the photo, 84 DIP high (the bar at 1.4×).
    private static Rendered RenderMaterial(BarSettings material, double w, double h, double rs, Photo photo)
    {
        int pw = (int)Math.Round(w * rs), ph = (int)Math.Round(h * rs);
        var bg = Backdrop.Create(pw, ph, w, h, 0, 0, rs, photo);
        var s = material.Clone();
        s.Height = 60;
        s.CornerRadius = 30;
        double k = 1.4;
        var spec = GlassSpec.For(s, k * rs);
        int inset = (int)spec.Inset;
        int bw = (int)Math.Round((w - 80) * rs), bh = (int)Math.Round(60 * k * rs);
        int bx = (pw - bw) / 2, by = (ph - bh) / 2;
        var pixels = new byte[bg.Base.Length];
        Buffer.BlockCopy(bg.Base, 0, pixels, 0, pixels.Length);
        GlassRenderer.Draw(bg, pixels, bx - inset, by - inset, bw + 2 * inset, bh + 2 * inset, spec);
        return new Rendered(pw, ph, pixels);
    }

    // ---- Geometry: the same continuous corner profile used by the application ----

    private UIElement GeometryCard()
    {
        var outline = new Rectangle
        {
            Width = 176, Height = 176, RadiusX = 46, RadiusY = 46,
            Style = StyleOf("GlyphAccentStyle"), HorizontalAlignment = HorizontalAlignment.Center,
        };
        var face = new StackPanel { Spacing = 12, HorizontalAlignment = HorizontalAlignment.Center, VerticalAlignment = VerticalAlignment.Top };
        face.Children.Add(outline);
        var caption = Text(Loc.T("G3 large corners"), "RowTitleStyle");
        caption.HorizontalAlignment = HorizontalAlignment.Center;
        caption.MinHeight = caption.LineHeight = 22;
        face.Children.Add(caption);
        var grid = new ResponsiveGrid { MinCellWidth = 300, MaxColumns = 2, Gap = 28 };
        grid.Children.Add(face);
        grid.Children.Add(RadiusDemo());
        return Card(grid);
    }
    // The concentric rule in one picture: the bar's capsule radius, a 12 inset, the inner radius.
    private static UIElement RadiusDemo()
    {
        const double R = 30, P = 12, Ww = 220, Hh = 176;
        var demo = new Canvas { Width = Ww, Height = Hh };
        demo.Children.Add(new Rectangle { Width = Ww, Height = Hh, RadiusX = R, RadiusY = R, Style = StyleOf("GlyphInkStyle") });
        var inner = new Rectangle { Width = Ww - 2 * P, Height = Hh - 2 * P, RadiusX = R - P, RadiusY = R - P, Style = StyleOf("GlyphAccentStyle") };
        Canvas.SetLeft(inner, P);
        Canvas.SetTop(inner, P);
        demo.Children.Add(inner);
        var label = Tabular(Text(Loc.F("R {0} − inset {1} = r {2}", R, P, R - P), "RowTitleStyle"));
        label.Width = Ww - 2 * P;
        label.TextAlignment = TextAlignment.Center;
        Canvas.SetLeft(label, P);
        Canvas.SetTop(label, Hh / 2 - 10);
        demo.Children.Add(label);
        var s = new StackPanel { Spacing = 12, HorizontalAlignment = HorizontalAlignment.Center, VerticalAlignment = VerticalAlignment.Top };
        s.Children.Add(demo);
        var why = Text(Loc.T("Nesting: the inner radius shrinks with the inset, keeping the outline consistent."), "RowNoteStyle");
        why.HorizontalAlignment = HorizontalAlignment.Center;
        why.MinHeight = why.LineHeight = 22;
        s.Children.Add(why);
        return s;
    }

    // ---- Spacing and colour ----

    private static UIElement SpaceAndColour()
    {
        var space = new ResponsiveGrid { MinCellWidth = 280, MaxColumns = 1, Gap = 10, DistributeRows = true };
        foreach (var (name, value, _) in Spacing)
        {
            var row = new Grid { ColumnSpacing = 12 };
            row.ColumnDefinitions.Add(new ColumnDefinition { Width = new GridLength(40) });
            row.ColumnDefinitions.Add(new ColumnDefinition { Width = new GridLength(1, GridUnitType.Star) });
            row.Children.Add(Tabular(Text(name, "RowNoteStyle")));
            var bar = new StackPanel { Orientation = Orientation.Horizontal, Spacing = 8 };
            bar.Children.Add(new Rectangle { Width = value * 6, Height = 10, RadiusX = 3, RadiusY = 3, Fill = Tokens.Brush("AccentBrush"), Opacity = 0.35 + 0.65 * value / 24.0, VerticalAlignment = VerticalAlignment.Center });
            bar.Children.Add(Tabular(Text(value.ToString(), "FootnoteStyle")));
            Grid.SetColumn(bar, 1);
            row.Children.Add(bar);
            space.Children.Add(row);
        }

        var colours = new ResponsiveGrid { MinCellWidth = 112, MaxColumns = 4, Gap = 12, DistributeRows = true };
        (string Key, string Name)[] tokens =
        {
            ("AccentBrush", Loc.T("Accent")), ("InkBrush", Loc.T("Primary text")), ("InkSecondaryBrush", Loc.T("Secondary text")), ("InkTertiaryBrush", Loc.T("Tertiary text")),
            ("FillBrush", Loc.T("Fill")), ("SeparatorBrush", Loc.T("Divider")), ("CardBrush", Loc.T("Card")), ("PageBrush", Loc.T("Page")),
        };
        foreach (var (key, name) in tokens) colours.Children.Add(Chip(Tokens.Brush(key), name));
        // The few colours widgets use (WidgetFaces.cs, widget_faces.cpp), as on macOS.
        foreach (var (c, name) in new[] { (WidgetFaces.Green, Loc.T("Normal · Green")), (WidgetFaces.Orange, Loc.T("Reminder · Orange")), (WidgetFaces.Red, Loc.T("Warning · Red")), (WidgetFaces.Blue, Loc.T("Calendar · Blue")) })
            colours.Children.Add(Chip(new SolidColorBrush(c), name));

        var grid = new ResponsiveGrid { MinCellWidth = 300, Gap = 28, StretchLastRow = true };
        grid.Children.Add(space);
        // Bring the outer colour rows in towards xs and xl, leaving room above and below.
        grid.Children.Add(new Border { Padding = new Thickness(0, 24, 0, 24), Child = colours });
        return Card(grid);
    }

    private static UIElement Chip(Brush brush, string name)
    {
        var row = new Grid { ColumnSpacing = 10 };
        row.ColumnDefinitions.Add(new ColumnDefinition { Width = new GridLength(28) });
        row.ColumnDefinitions.Add(new ColumnDefinition { Width = new GridLength(1, GridUnitType.Star) });
        row.Children.Add(new Border { Width = 28, Height = 28, VerticalAlignment = VerticalAlignment.Center, CornerRadius = new CornerRadius(8),
                                      Background = brush, BorderBrush = Tokens.Brush("CardStrokeBrush"), BorderThickness = new Thickness(1) });
        var labels = new StackPanel();
        labels.Children.Add(Text(name, "RowNoteStyle"));
        string hex = brush is SolidColorBrush b ? (b.Color.A == 255 ? $"#{b.Color.R:X2}{b.Color.G:X2}{b.Color.B:X2}" : $"#{b.Color.A:X2}{b.Color.R:X2}{b.Color.G:X2}{b.Color.B:X2}") : "";
        labels.Children.Add(Tabular(Text(hex, "FootnoteStyle")));
        Grid.SetColumn(labels, 1);
        row.Children.Add(labels);
        return row;
    }

    // ---- Interaction: a dock that magnifies under the pointer and hops once, a card that pops ----

    private UIElement InteractionCard()
    {
        var grid = new ResponsiveGrid { MinCellWidth = 340, Gap = 16, StretchLastRow = true };
        grid.Children.Add(DockDemo());
        grid.Children.Add(PopDemo());
        return grid;
    }

    private static Border InteractionFrame(Grid stage, UIElement content)
    {
        var frame = Card(content);
        frame.Padding = new Thickness(16);
        // The border stroke is part of the inset too: the photo follows the same corner centre.
        double inset = frame.BorderThickness.Left + frame.Padding.Left;
        stage.CornerRadius = new CornerRadius(ContinuousCorners.RadiusOf(frame) - inset);
        return frame;
    }

    private FrameworkElement DockDemo()
    {
        const double Icon = 34, Gap = 9, StageH = 190, BarH = 54;
        var stage = new Grid { Height = StageH, Background = PhotoBrush() };
        var lane = new Canvas { Height = StageH };
        stage.Children.Add(lane);
        var bar = new Border
        {
            Height = BarH, CornerRadius = new CornerRadius(BarH / 2), VerticalAlignment = VerticalAlignment.Bottom, HorizontalAlignment = HorizontalAlignment.Center,
            Margin = new Thickness(0, 0, 0, 16), Background = new SolidColorBrush(Color.FromArgb(120, 255, 255, 255)),
            BorderBrush = new SolidColorBrush(Color.FromArgb(170, 255, 255, 255)), BorderThickness = new Thickness(1),
        };
        stage.Children.Insert(0, bar);
        var apps = _previewPins.Append(_previewRunning).ToArray();
        int n = apps.Length;
        bar.Width = n * Icon + (n - 1) * Gap + 2 * 14;
        var holders = new List<(Border Holder, Image Icon)>();
        for (int i = 0; i < n; i++)
        {
            var icon = new Image
            {
                Width = Icon, Height = Icon, Stretch = Stretch.Uniform, DataContext = apps[i],
            };
            icon.SetBinding(Image.SourceProperty, new Microsoft.UI.Xaml.Data.Binding { Path = new PropertyPath(nameof(PinVm.Icon)) });
            var holder = new Border { Width = Icon, Height = Icon, Child = icon };
            holder.CenterPoint = new Vector3((float)(Icon / 2), (float)Icon, 0);
            Canvas.SetTop(holder, StageH - 16 - BarH / 2 - Icon / 2);
            lane.Children.Add(holder);
            holders.Add((holder, icon));
            var hop = new Curve("hop", "", "", "", 0.48, t => Motion.Hop(t, 0.48));
            Action<double> show = t => icon.Translation = new Vector3(0, (float)(-Motion.Hop(t, 0.48) * 0.32 * Icon * 1.6), 0);
            holder.PointerPressed += (_, _) => Play(hop, show);
            AutomationProperties.SetName(holder, apps[i].Title);
        }
        double width = 0, x0 = 0, row = n * Icon + (n - 1) * Gap;
        // As the bar does (BarPainter): the pointer drives the wave, its strength follows the pointer
        // in and out exponentially (20/s in, 11/s out), all on the frame clock with render transforms
        // only (no layout but the bar's width, no restarted transitions).
        double? pointer = null;
        double amount = 0, lastX = 0;
        long last = 0;
        bool ticking = false;
        void Apply()
        {
            double mag = Math.Max(1, _current.Magnify), k = (mag - 1) * amount, range = (Icon + Gap) * 2.3;
            double centre = pointer ?? lastX;
            double Map(double u)
            {
                if (k <= 0) return u;
                double x = (u - centre) / range;
                return u + k * range * (x >= 1 ? 0.5 : x <= -1 ? -0.5 : x / 2 + Math.Sin(Math.PI * x) / (2 * Math.PI));
            }
            for (int i = 0; i < n; i++)
            {
                double left = x0 + i * (Icon + Gap), bx = left + Icon / 2;
                double grow = (Map(bx + Icon / 2) - Map(bx - Icon / 2)) / Icon;
                holders[i].Holder.Translation = new Vector3((float)(Map(bx) - bx), 0, 0);
                holders[i].Holder.Scale = new Vector3((float)grow, (float)grow, 1);
            }
            bar.Width = Map(x0 + row + 14) - Map(x0 - 14);
            bar.Translation = new Vector3((float)((Map(x0 - 14) + Map(x0 + row + 14)) / 2 - (x0 + row / 2)), 0, 0);
        }
        void Tick(object? sender, object e)
        {
            long now = Environment.TickCount64;
            double dt = Math.Clamp((now - last) / 1000.0, 0, 0.05);
            last = now;
            double target = pointer.HasValue ? 1 : 0, rate = pointer.HasValue ? 20 : 11;
            amount += (target - amount) * (1 - Math.Exp(-dt * rate));
            if (Math.Abs(target - amount) < 0.002)
            {
                amount = target;
                CompositionTarget.Rendering -= Tick;
                ticking = false;
            }
            Apply();
        }
        void Wake()
        {
            if (ticking) { Apply(); return; }
            ticking = true;
            last = Environment.TickCount64;
            CompositionTarget.Rendering += Tick;
        }
        stage.SizeChanged += (_, e) =>
        {
            width = e.NewSize.Width;
            x0 = (width - row) / 2;
            for (int i = 0; i < n; i++) Canvas.SetLeft(holders[i].Holder, x0 + i * (Icon + Gap));
            Apply();
        };
        stage.PointerMoved += (_, e) => { pointer = lastX = e.GetCurrentPoint(lane).Position.X; Wake(); };
        stage.PointerExited += (_, _) => { pointer = null; Wake(); };
        stage.Unloaded += (_, _) => { if (ticking) { CompositionTarget.Rendering -= Tick; ticking = false; } };

        var s = new StackPanel { Spacing = 10 };
        s.Children.Add(stage);
        s.Children.Add(Text(Loc.T("Hover magnification and launch hop"), "RowTitleStyle"));
        s.Children.Add(Text(Loc.T("Wave: (1 + cos πd) / 2, where d is the pointer's distance in icons (radius 2.3 icons); magnification comes from the Taskbar page."), "RowNoteStyle"));
        return InteractionFrame(stage, s);
    }

    private FrameworkElement PopDemo()
    {
        var stage = new Grid { Height = 190, Background = PhotoBrush() };
        var card = new Border
        {
            Width = 300, Padding = new Thickness(14, 12, 16, 13), CornerRadius = new CornerRadius(22), HorizontalAlignment = HorizontalAlignment.Center,
            VerticalAlignment = VerticalAlignment.Center, Background = new SolidColorBrush(Color.FromArgb(205, 250, 250, 252)),
            BorderBrush = new SolidColorBrush(Color.FromArgb(150, 255, 255, 255)), BorderThickness = new Thickness(1),
        };
        var text = new StackPanel { Spacing = 2 };
        text.Children.Add(new TextBlock { Text = Loc.T("Calendar · Now"), FontSize = 12, Foreground = new SolidColorBrush(Color.FromArgb(150, 0x1D, 0x1D, 0x1F)) });
        text.Children.Add(new TextBlock { Text = Loc.T("Product review"), FontSize = 14, FontFamily = (FontFamily)Application.Current.Resources["EmphasisFont"], Foreground = new SolidColorBrush(Color.FromArgb(240, 0x1D, 0x1D, 0x1F)) });
        text.Children.Add(new TextBlock { Text = Loc.T("Click me to show it again."), FontSize = 13, Foreground = new SolidColorBrush(Color.FromArgb(190, 0x1D, 0x1D, 0x1F)) });
        card.Child = text;
        var popup = new Grid { HorizontalAlignment = HorizontalAlignment.Center, VerticalAlignment = VerticalAlignment.Center };
        popup.Children.Add(card);
        stage.Children.Add(popup);
        stage.PointerPressed += (_, _) => ReplayNotification(popup,
            new Vector2((float)(popup.ActualWidth - _current.Height / 2 - 4), (float)popup.ActualHeight), card);
        AutomationProperties.SetName(stage, Loc.T("Notification popup example"));

        var s = new StackPanel { Spacing = 10 };
        s.Children.Add(stage);
        s.Children.Add(Text(Loc.T("Notification popup"), "RowTitleStyle"));
        s.Children.Add(Text(Loc.T("Spring kPop: from 0.92 to 1, overshooting only slightly (peak 1.0067), fading in over 160 ms. No back-and-forth bounce."), "RowNoteStyle"));
        return InteractionFrame(stage, s);
    }

    // ---- Motion ----

    private UIElement CurveGrid()
    {
        var grid = new ResponsiveGrid { MinCellWidth = 280, Gap = 16 };
        foreach (var c in Curves) grid.Children.Add(CurveCard(c));
        return grid;
    }

    // ---- Type: the samples (and in the details, the numbers) ----

    private static UIElement TypeCard(bool detailed)
    {
        var rows = new StackPanel { Spacing = 0 };
        for (int i = 0; i < TypeRamp.Length; i++)
        {
            var (name, size, font, weight, mac, use) = TypeRamp[i];
            var g = new Grid { ColumnSpacing = 16, Padding = new Thickness(0, 10, 0, 10) };
            g.ColumnDefinitions.Add(new ColumnDefinition { Width = new GridLength(1, GridUnitType.Star) });
            g.ColumnDefinitions.Add(new ColumnDefinition { Width = new GridLength(detailed ? 170 : 120) });
            if (detailed) g.ColumnDefinitions.Add(new ColumnDefinition { Width = new GridLength(200) });
            var sample = new TextBlock
            {
                Text = detailed ? name : Loc.T("Under the glass, content first Aa 0123"),
                FontFamily = (FontFamily)Application.Current.Resources[font],
                FontSize = detailed ? 15 : size,
                VerticalAlignment = VerticalAlignment.Center,
                TextTrimming = TextTrimming.CharacterEllipsis,
            };
            var spec = Tabular(Text(detailed ? $"{size} · {weight}" : name, "RowNoteStyle"));
            spec.VerticalAlignment = VerticalAlignment.Center;
            Grid.SetColumn(spec, 1);
            g.Children.Add(sample);
            g.Children.Add(spec);
            if (detailed)
            {
                var where = new StackPanel { VerticalAlignment = VerticalAlignment.Center };
                where.Children.Add(Text("macOS " + mac, "FootnoteStyle"));
                where.Children.Add(Text(use, "RowNoteStyle"));
                Grid.SetColumn(where, 2);
                g.Children.Add(where);
            }
            if (i > 0) rows.Children.Add(new Rectangle { Height = 1, Fill = Tokens.Brush("SeparatorBrush") });
            rows.Children.Add(g);
        }
        return Card(rows);
    }

    private FrameworkElement CurveCard(Curve c)
    {
        // Drawn again at the card's width: the curve, its 0 and 1 lines, a cursor on it, and below
        // the same motion on a moving chip, the way it looks on screen.
        const double H = 96, Y0 = 82, Y1 = 22, Chip = 22;
        double w = 240;
        double X(double t) => 4 + (w - 8) * t / c.Seconds;
        double Y(double v) => Y0 + (Y1 - Y0) * v;

        var plot = new Canvas { Height = H };
        var lines = new[] { new Line { Style = StyleOf("GlyphFaintStyle"), StrokeDashArray = new DoubleCollection { 2, 3 } },
                            new Line { Style = StyleOf("GlyphFaintStyle"), StrokeDashArray = new DoubleCollection { 2, 3 } } };
        var line = new Polyline { Style = StyleOf("GlyphAccentStyle"), Points = new PointCollection() };
        var cursor = new Ellipse { Width = 8, Height = 8, Style = StyleOf("GlyphAccentFillStyle") };
        foreach (var l in lines) plot.Children.Add(l);
        plot.Children.Add(line);
        plot.Children.Add(cursor);

        var track = new Grid { Height = 22 };
        track.Children.Add(new Border { CornerRadius = new CornerRadius(11), Background = Tokens.Brush("FillBrush") });
        var lane = new Canvas();
        var chip = new Rectangle { Width = Chip, Height = 14, RadiusX = 7, RadiusY = 7, Style = StyleOf("GlyphAccentFillStyle") };
        Canvas.SetTop(chip, 4);
        lane.Children.Add(chip);
        track.Children.Add(lane);

        double shown = c.Seconds;
        void Show(double t)
        {
            shown = Math.Min(t, c.Seconds);
            double v = c.At(shown);
            Canvas.SetLeft(cursor, X(shown) - 4);
            Canvas.SetTop(cursor, Y(v) - 4);
            Canvas.SetLeft(chip, 4 + (w - Chip - 8) * v);
        }
        void Draw(double width)
        {
            w = Math.Max(120, width);
            for (int k = 0; k < 2; k++)
            {
                lines[k].X2 = w;
                lines[k].Y1 = lines[k].Y2 = Y(k);
            }
            line.Points.Clear();
            for (int i = 0; i <= 120; i++)
            {
                double t = c.Seconds * i / 120;
                line.Points.Add(new Point(X(t), Y(c.At(t))));
            }
            Show(shown);
        }
        plot.SizeChanged += (_, e) => Draw(e.NewSize.Width);
        Draw(w);

        var text = new StackPanel { Spacing = 2 };
        text.Children.Add(Text(c.Name, "RowTitleStyle"));
        var spec = Text(c.Spec, "RowNoteStyle");
        Microsoft.UI.Xaml.Documents.Typography.SetNumeralAlignment(spec, FontNumeralAlignment.Tabular);
        text.Children.Add(spec);
        text.Children.Add(Text(c.Use, "RowNoteStyle"));

        var body = new StackPanel { Spacing = 10 };
        body.Children.Add(text);
        body.Children.Add(plot);
        body.Children.Add(track);
        var card = Card(body);
        AutomationProperties.SetName(card, c.Name);
        card.PointerPressed += (_, _) => Play(c, Show);
        return card;
    }

    private void Play(Curve c, Action<double> show)
    {
        _playing.RemoveAll(p => p.Show == show);
        _playing.Add((DesignClock.Elapsed.TotalSeconds, c, show));
        show(0);
        if (_designTicking) return;
        _designTicking = true;
        CompositionTarget.Rendering += OnDesignFrame;
    }

    private void OnDesignFrame(object? sender, object e)
    {
        double now = DesignClock.Elapsed.TotalSeconds;
        for (int i = _playing.Count - 1; i >= 0; i--)
        {
            var (start, c, show) = _playing[i];
            show(now - start);
            if (now - start >= c.Seconds) _playing.RemoveAt(i);
        }
        if (_playing.Count > 0) return;
        CompositionTarget.Rendering -= OnDesignFrame;
        _designTicking = false;
    }

    private void StopDesignAnimations()
    {
        if (!_designTicking) return;
        CompositionTarget.Rendering -= OnDesignFrame;
        _designTicking = false;
        foreach (var (_, curve, show) in _playing) show(curve.Seconds);
        _playing.Clear();
    }

    // ---- Corners ----

    private static UIElement RadiusCard()
    {
        // Concentric: the bar's capsule radius, a 12 inset, the inner radius that keeps the gap even.
        const double R = 30, P = 12, Ww = 240, Hh = 132;
        var demo = new Canvas { Width = Ww, Height = Hh };
        var outer = new Rectangle { Width = Ww, Height = Hh, RadiusX = R, RadiusY = R, Style = StyleOf("GlyphInkStyle") };
        var inner = new Rectangle { Width = Ww - 2 * P, Height = Hh - 2 * P, RadiusX = R - P, RadiusY = R - P, Style = StyleOf("GlyphAccentStyle") };
        Canvas.SetLeft(inner, P);
        Canvas.SetTop(inner, P);
        demo.Children.Add(outer);
        demo.Children.Add(inner);
        var label = Text(Loc.F("R {0} − inset {1} = r {2}", R, P, R - P), "RowTitleStyle");
        label.Width = Ww - 2 * P;
        label.TextAlignment = TextAlignment.Center;
        Canvas.SetLeft(label, P);
        Canvas.SetTop(label, Hh / 2 - 10);
        demo.Children.Add(label);
        var left = new StackPanel { Spacing = 10, HorizontalAlignment = HorizontalAlignment.Center };
        left.Children.Add(demo);
        var why = Text(Loc.T("Inner radii shrink with the inset; straight-edge spacing stays even."), "RowNoteStyle");
        why.HorizontalAlignment = HorizontalAlignment.Center;
        left.Children.Add(why);

        var ladder = new StackPanel { Orientation = Orientation.Horizontal, Spacing = 20, VerticalAlignment = VerticalAlignment.Center };
        (double R, string Use)[] steps = { (6, Loc.T("Min")), (10, Loc.T("Hover plate")), (22, Loc.T("Module · Widget")), (30, Loc.T("Bar and popup")) };
        foreach (var (r, use) in steps)
        {
            var s = new StackPanel { Spacing = 6, HorizontalAlignment = HorizontalAlignment.Center };
            // The bar's radius is half its height: drawn as the capsule it makes.
            s.Children.Add(new Rectangle { Width = r >= 30 ? 100 : 60, Height = 60, RadiusX = r, RadiusY = r, Style = StyleOf("GlyphInkStyle"), HorizontalAlignment = HorizontalAlignment.Center });
            var v = Text(r.ToString(), "RowTitleStyle");
            v.HorizontalAlignment = HorizontalAlignment.Center;
            s.Children.Add(v);
            var u = Text(use, "FootnoteStyle");
            u.HorizontalAlignment = HorizontalAlignment.Center;
            s.Children.Add(u);
            ladder.Children.Add(s);
        }
        var grid = new ResponsiveGrid { MinCellWidth = 300, Gap = 24 };
        grid.Children.Add(left);
        grid.Children.Add(ladder);
        return Card(grid);
    }

    // ---- Sizes ----

    private UIElement SizeCard()
    {
        // This screen: physical pixels of the window's display over its scale.
        double scale = Content.XamlRoot?.RasterizationScale ?? 1;
        var area = DisplayArea.GetFromWindowId(AppWindow.Id, DisplayAreaFallback.Primary).OuterBounds;
        double logical = area.Width / scale;
        int mine = 0;
        for (int i = 0; i < SizeClasses.Length; i++)
            if (logical >= SizeClasses[i].From) mine = i;

        var g = new Grid { ColumnSpacing = 12, RowSpacing = 0 };
        foreach (var w in new[] { 1.1, 2.6, 0.7, 0.7, 0.7, 0.8 })
            g.ColumnDefinitions.Add(new ColumnDefinition { Width = new GridLength(w, GridUnitType.Star) });
        string[] head = { Loc.T("Logical width"), Loc.T("Typical screens"), Loc.T("Bar height"), Loc.T("Icon"), Loc.T("Bottom margin"), Loc.T("Magnification") };
        g.RowDefinitions.Add(new RowDefinition { Height = GridLength.Auto });
        for (int c = 0; c < head.Length; c++)
        {
            var t = Text(head[c], "FootnoteStyle");
            t.Margin = new Thickness(c == 0 ? 10 : 0, 0, 0, 8);
            Grid.SetColumn(t, c);
            g.Children.Add(t);
        }
        for (int i = 0; i < SizeClasses.Length; i++)
        {
            var k = SizeClasses[i];
            int row = i + 1;
            g.RowDefinitions.Add(new RowDefinition { Height = GridLength.Auto });
            if (i == mine)
            {
                var band = new Border { CornerRadius = new CornerRadius(8), Background = Tokens.Brush("FillBrush") };
                Grid.SetRow(band, row);
                Grid.SetColumnSpan(band, head.Length);
                g.Children.Add(band);
            }
            string range = i + 1 < SizeClasses.Length ? $"{(k.From == 0 ? "< " : k.From + "–")}{SizeClasses[i + 1].From - (k.From == 0 ? 0 : 1)}" : $"≥ {k.From}";
            string[] cells = { range, k.Screens, k.Height.ToString(), k.Icon.ToString(), k.Margin.ToString(), k.Magnify.ToString("0.0") };
            for (int c = 0; c < cells.Length; c++)
            {
                var t = Text(cells[c], c == 0 ? "RowTitleStyle" : "RowNoteStyle");
                t.FontSize = c == 0 ? 13 : 12;
                t.Margin = new Thickness(c == 0 ? 10 : 0, 9, c == cells.Length - 1 ? 10 : 0, 9);
                t.VerticalAlignment = VerticalAlignment.Center;
                Microsoft.UI.Xaml.Documents.Typography.SetNumeralAlignment(t, FontNumeralAlignment.Tabular);
                Grid.SetRow(t, row);
                Grid.SetColumn(t, c);
                g.Children.Add(t);
            }
        }
        var k0 = SizeClasses[mine];
        var here = Text(Loc.F("This screen: {0} × {1} pixels, scale {2:0}% → logical width {3:0}. Suggested bar height {4}, icons {5}; currently bar height {6:0}, icons {7:0}.", area.Width, area.Height, scale * 100, logical, k0.Height, k0.Icon, _current.Height, _current.IconSize),
            "RowNoteStyle");
        here.Margin = new Thickness(10, 12, 10, 0);
        var s = new StackPanel();
        s.Children.Add(g);
        s.Children.Add(here);
        return Card(s);
    }

    // ---- Ink ----

    private static UIElement InkCard()
    {
        Border Panel(Color bg, Color ink, (string Name, double A)[] steps, Color warn, string title)
        {
            var s = new StackPanel { Spacing = 6 };
            var head = new TextBlock { Text = title, FontSize = 11, Foreground = new SolidColorBrush(Color.FromArgb(150, ink.R, ink.G, ink.B)) };
            s.Children.Add(head);
            foreach (var (name, a) in steps)
                s.Children.Add(new TextBlock
                {
                    Text = $"{name} · {a * 100:0}%",
                    FontSize = 13.5,
                    FontFamily = (FontFamily)Application.Current.Resources[name == Loc.T("Primary text") ? "EmphasisFont" : "TextFont"],
                    Foreground = new SolidColorBrush(Color.FromArgb((byte)Math.Round(a * 255), ink.R, ink.G, ink.B)),
                });
            var chips = new StackPanel { Orientation = Orientation.Horizontal, Spacing = 8, Margin = new Thickness(0, 6, 0, 0) };
            foreach (var (c, label) in new[] { (Color.FromArgb(255, 0x0A, 0x84, 0xFF), "#0A84FF"), (warn, $"#{warn.R:X2}{warn.G:X2}{warn.B:X2}") })
            {
                var chip = new Border { CornerRadius = new CornerRadius(10), Padding = new Thickness(10, 3, 10, 4), Background = new SolidColorBrush(c) };
                chip.Child = new TextBlock { Text = label, FontSize = 11, Foreground = new SolidColorBrush(Colors.White) };
                chips.Children.Add(chip);
            }
            s.Children.Add(chips);
            return new Border { Background = new SolidColorBrush(bg), CornerRadius = new CornerRadius(12), Padding = new Thickness(18, 14, 18, 16), Child = s };
        }
        var grid = new ResponsiveGrid { MinCellWidth = 280, Gap = 16, StretchLastRow = true };
        grid.Children.Add(Panel(Color.FromArgb(255, 0x24, 0x2A, 0x3A), Colors.White, new[] { (Loc.T("Primary text"), 0.96), (Loc.T("Secondary text"), 0.76), (Loc.T("Tertiary text"), 0.56) },
            Color.FromArgb(255, 0xFF, 0x45, 0x3A), Loc.T("Light ink · dark backdrop")));
        grid.Children.Add(Panel(Color.FromArgb(255, 0xEC, 0xEE, 0xF3), Color.FromArgb(255, 0x11, 0x13, 0x17), new[] { (Loc.T("Primary text"), 0.92), (Loc.T("Secondary text"), 0.66), (Loc.T("Tertiary text"), 0.46) },
            Color.FromArgb(255, 0xE0, 0x35, 0x2B), Loc.T("Dark ink · light backdrop")));
        return grid;
    }
}
