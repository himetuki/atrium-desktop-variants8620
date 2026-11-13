using System.Text.Json.Nodes;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Media;
using Microsoft.UI.Xaml.Shapes;
using Windows.Foundation;
using Windows.UI;
using Path = Microsoft.UI.Xaml.Shapes.Path;

namespace Atrium.Settings;

// The weather widget as the core paints it (widget_faces.cpp, PaintWeather): in its material's ink,
// the place, the temperature, the next hours and days. Its data is the core's last forecast
// (%LOCALAPPDATA%\Atrium\weather.json), else an example.
internal static partial class WidgetFaces
{
    private sealed record WeatherHour(string Label, int Code, bool Day, double Temp, int Event);
    private sealed record WeatherDay(string Label, int Code, double Lo, double Hi);
    private sealed record WeatherData(string City, double Temp, double Hi, double Lo, int Code, bool Day, WeatherHour[] Hours, WeatherDay[] Days);

    private static string WeatherPath => System.IO.Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "Atrium", "weather.json");

    public static long WeatherStamp()
    {
        try { return File.Exists(WeatherPath) ? File.GetLastWriteTimeUtc(WeatherPath).Ticks : 0; }
        catch (IOException) { return 0; }
    }

    private static WeatherData LoadWeather()
    {
        try
        {
            if (File.Exists(WeatherPath) && JsonNode.Parse(File.ReadAllText(WeatherPath)) is JsonObject o)
            {
                var hours = (o["hours"] as JsonArray ?? new()).OfType<JsonObject>()
                    .Select(h => new WeatherHour((string?)h["label"] ?? "", (int)((double?)h["code"] ?? 0), (bool?)h["day"] ?? true, (double?)h["temp"] ?? 0,
                                                 (int)((double?)h["event"] ?? 0))).ToArray();
                var days = (o["days"] as JsonArray ?? new()).OfType<JsonObject>()
                    .Select(d => new WeatherDay((string?)d["label"] ?? "", (int)((double?)d["code"] ?? 0), (double?)d["lo"] ?? 0, (double?)d["hi"] ?? 0)).ToArray();
                if (hours.Length > 0 && days.Length > 0)
                    return new WeatherData((string?)o["city"] ?? "", (double?)o["temp"] ?? 0, (double?)o["hi"] ?? 0, (double?)o["lo"] ?? 0,
                                           (int)((double?)o["code"] ?? 0), (bool?)o["day"] ?? true, hours, days);
            }
        }
        catch (Exception e) when (e is IOException or UnauthorizedAccessException or System.Text.Json.JsonException or InvalidOperationException or FormatException) { }
        return new WeatherData(Loc.T("Nanjing"), 18, 21, 12, 2, true,
            new WeatherHour[] { new(Loc.T("Now"), 2, true, 18, 0), new(Loc.T("15:00"), 1, true, 19, 0), new(Loc.T("16:00"), 1, true, 19, 0), new("17:12", 0, true, 0, 2),
                                new(Loc.T("18:00"), 1, false, 16, 0), new(Loc.T("19:00"), 0, false, 15, 0) },
            new WeatherDay[] { new(Loc.T("Today"), 2, 12, 21), new(Loc.T("Tue"), 61, 11, 17), new(Loc.T("Wed"), 3, 9, 15), new(Loc.T("Thu"), 0, 8, 18), new(Loc.T("Fri"), 1, 10, 20) });
    }

    private static string Condition(int code) => code switch
    {
        0 => Loc.T("Clear"), 1 => Loc.T("Mostly clear"), 2 => Loc.T("Partly cloudy"), 3 => Loc.T("Overcast"), 45 or 48 => Loc.T("Fog"), 51 or 53 or 55 => Loc.T("Drizzle"), 56 or 57 or 66 or 67 => Loc.T("Freezing rain"),
        61 => Loc.T("Light rain"), 63 => Loc.T("Moderate rain"), 65 => Loc.T("Heavy rain"), 71 => Loc.T("Light snow"), 73 => Loc.T("Moderate snow"), 75 => Loc.T("Heavy snow"), 77 => Loc.T("Snow grains"), 80 or 81 => Loc.T("Rain showers"), 82 => Loc.T("Violent rain showers"),
        85 or 86 => Loc.T("Snow showers"), 95 => Loc.T("Thunderstorm"), 96 or 99 => Loc.T("Thunderstorm with hail"), _ => Loc.T("Cloudy"),
    };

    private enum Sky { Clear, Partly, Cloudy, Fog, Rain, Snow, Storm }

    private static Sky SkyOf(int code) =>
        code <= 1 ? Sky.Clear : code == 2 ? Sky.Partly : code == 3 ? Sky.Cloudy : code is 45 or 48 ? Sky.Fog :
        code is >= 71 and <= 77 or 85 or 86 ? Sky.Snow : code >= 95 ? Sky.Storm : Sky.Rain;

    private static Color Rgb(uint rgb, double a = 1) => Color.FromArgb((byte)Math.Round(255 * a), (byte)(rgb >> 16), (byte)(rgb >> 8), (byte)rgb);
    private static string Deg(double t) => Math.Round(t, MidpointRounding.AwayFromZero) + "°";

    // The full-colour face's sky (SkyFace): blue by day, navy by night, greyer under clouds, rain and fog.
    private static Brush SkyBrush()
    {
        var d = LoadWeather();
        bool day = d.Day;
        var (top, bottom) = SkyOf(d.Code) switch
        {
            Sky.Clear or Sky.Partly => day ? (0x2F7BD6u, 0x63A9EAu) : (0x0C1631u, 0x2A3E68u),
            Sky.Cloudy or Sky.Fog => day ? (0x6A7E92u, 0x98A8B8u) : (0x262F3Cu, 0x47525Fu),
            Sky.Snow => day ? (0x7F95ACu, 0xAFC0D0u) : (0x2B3646u, 0x4C5868u),
            _ => day ? (0x4A5D70u, 0x728699u) : (0x1D2530u, 0x3A4552u),
        };
        var brush = new LinearGradientBrush { StartPoint = new Point(0, 0), EndPoint = new Point(0, 1) };
        brush.GradientStops.Add(new GradientStop { Color = Rgb(top), Offset = 0 });
        brush.GradientStops.Add(new GradientStop { Color = Rgb(bottom), Offset = 1 });
        return brush;
    }

    private static void Weather(Canvas c, string size, double w, double h, Pal p)
    {
        var d = LoadWeather();
        Color text = p.Text, sub = p.Sub, faint = p.Track;
        Ink ink = p.Icon;
        bool dark = ink == Ink.Dark;
        const double pad = 16;
        Text(c, d.City, pad, 12, w - pad - (size == "small" ? 0 : 120), 32, 14.5, text, 2, 0, p);
        Text(c, Deg(d.Temp), pad - 2, 30, w / 2 + 40, 82, 44, text, 0, 0, p);
        string range = Loc.F("H {0}  L {1}", Deg(d.Hi), Deg(d.Lo));
        if (size == "small")
        {
            Icon(c, d.Code, d.Day, pad + 11, h - 62, 22, 0, ink);
            Text(c, Condition(d.Code), pad, h - 48, w - pad, h - 30, 13, text, 2, 0, p);
            Text(c, range, pad, h - 30, w - pad, h - 12, 12, text, 2, 0, p);
            return;
        }
        Icon(c, d.Code, d.Day, w - pad - 10, 24, 20, 0, ink);
        Text(c, Condition(d.Code), w / 2, 40, w - pad, 58, 13, text, 2, 2, p);
        Text(c, range, w / 2 - 20, 58, w - pad, 76, 12.5, text, 2, 2, p);
        double hy = size == "large" ? 92 : 90, colW = (w - 2 * pad) / 6;
        if (size == "large") Line(c, pad, hy - 6, w - pad, hy - 6, faint, 1);
        for (int i = 0; i < Math.Min(6, d.Hours.Length); i++)
        {
            var hr = d.Hours[i];
            double x0 = pad + colW * i, cx = x0 + colW / 2;
            Text(c, hr.Label, x0 - 4, hy, x0 + colW + 4, hy + 16, 11.5, sub, 2, 1, p);
            Icon(c, hr.Code, hr.Day, cx, hy + 31, 22, hr.Event, ink);
            Text(c, hr.Event != 0 ? (hr.Event == 1 ? Loc.T("Sunrise") : Loc.T("Sunset")) : Deg(hr.Temp), x0 - 4, hy + 46, x0 + colW + 4, hy + 64, hr.Event != 0 ? 12 : 13.5, text, 2, 1, p);
        }
        if (size != "large") return;
        double top2 = hy + 76;
        Line(c, pad, top2 - 6, w - pad, top2 - 6, faint, 1);
        var days = d.Days.Take(5).ToArray();
        double weekLo = days.Min(x => x.Lo), weekHi = days.Max(x => x.Hi), span = Math.Max(1, weekHi - weekLo);
        double rowH = (h - 12 - top2) / Math.Max(1, days.Length), barL = 170, barR = w - pad - 44;
        for (int i = 0; i < days.Length; i++)
        {
            var day = days[i];
            double y = top2 + rowH * i, cy = y + rowH / 2;
            Text(c, day.Label, pad, y, pad + 60, y + rowH, 14, text, 2, 0, p);
            Icon(c, day.Code, true, pad + 84, cy, 22, 0, ink);
            Text(c, Deg(day.Lo), barL - 46, y, barL - 8, y + rowH, 14, sub, 2, 2, p);
            Fill(c, barL, cy - 2.5, barR, cy + 2.5, 2.5, faint);
            double x0 = barL + (barR - barL) * (day.Lo - weekLo) / span, x1 = Math.Max(x0 + 5, barL + (barR - barL) * (day.Hi - weekLo) / span);
            var bar = new LinearGradientBrush { StartPoint = new Point(0, 0), EndPoint = new Point(1, 0) };
            bar.GradientStops.Add(new GradientStop { Color = ink == Ink.Mono ? text : TempColor(day.Lo), Offset = 0 });
            bar.GradientStops.Add(new GradientStop { Color = ink == Ink.Mono ? text : TempColor(day.Hi), Offset = 1 });
            var seg = new Rectangle { Width = x1 - x0, Height = 5, RadiusX = 2.5, RadiusY = 2.5, Fill = bar };
            Canvas.SetLeft(seg, x0);
            Canvas.SetTop(seg, cy - 2.5);
            c.Children.Add(seg);
            if (i == 0)
            {
                double x = barL + (barR - barL) * (Math.Clamp(d.Temp, day.Lo, day.Hi) - weekLo) / span;
                Circle(c, x, cy, 4.5, dark ? Rgb(0xFFFFFF) : Rgb(0x1C2333, 0.55));
                Circle(c, x, cy, 3.2, dark ? Rgb(0x1C2333, 0.9) : Rgb(0xFFFFFF));
            }
            Text(c, Deg(day.Hi), barR + 6, y, w - pad, y + rowH, 14, text, 2, 2, p);
        }
    }

    private static Color TempColor(double t)
    {
        (double T, uint Rgb)[] stops = { (-5, 0x5AC8FA), (8, 0x63E6BE), (18, 0xFFD60A), (28, 0xFF9F0A), (36, 0xFF453A) };
        if (t <= stops[0].T) return Rgb(stops[0].Rgb);
        for (int i = 1; i < stops.Length; i++)
            if (t <= stops[i].T)
            {
                double k = (t - stops[i - 1].T) / (stops[i].T - stops[i - 1].T);
                Color a = Rgb(stops[i - 1].Rgb), b = Rgb(stops[i].Rgb);
                return Color.FromArgb(255, (byte)(a.R + (b.R - a.R) * k), (byte)(a.G + (b.G - a.G) * k), (byte)(a.B + (b.B - a.B) * k));
            }
        return Rgb(stops[^1].Rgb);
    }

    // ---- The symbols (WeatherIcon): a box s wide centred on (cx, cy) ----

    private static void Cloud(Canvas c, double cx, double cy, double s, Color ink)
    {
        Fill(c, cx - 0.42 * s, cy - 0.02 * s, cx + 0.42 * s, cy + 0.26 * s, 0.14 * s, ink);
        Circle(c, cx - 0.17 * s, cy + 0.02 * s, 0.19 * s, ink);
        Circle(c, cx + 0.08 * s, cy - 0.06 * s, 0.25 * s, ink);
        Circle(c, cx + 0.29 * s, cy + 0.09 * s, 0.15 * s, ink);
    }

    private static void Sun(Canvas c, double cx, double cy, double s, Color gold)
    {
        Circle(c, cx, cy, 0.2 * s, gold);
        for (int i = 0; i < 8; i++)
        {
            double a = i * Math.PI / 4;
            Line(c, cx + Math.Cos(a) * 0.31 * s, cy + Math.Sin(a) * 0.31 * s, cx + Math.Cos(a) * 0.43 * s, cy + Math.Sin(a) * 0.43 * s, gold, Math.Max(1, 0.075 * s));
        }
    }

    private static void Moon(Canvas c, double cx, double cy, double s, Color ink)
    {
        var disc = new EllipseGeometry { Center = new Point(cx, cy), RadiusX = 0.3 * s, RadiusY = 0.3 * s };
        var bite = new EllipseGeometry { Center = new Point(cx + 0.17 * s, cy - 0.12 * s), RadiusX = 0.25 * s, RadiusY = 0.25 * s };
        c.Children.Add(new Path { Data = new CombinedFromEllipses(disc, bite).Geometry, Fill = new SolidColorBrush(ink) });
    }

    // The disc less the bite, as one path: two arcs between the circles' crossing points.
    private sealed class CombinedFromEllipses
    {
        public Geometry Geometry { get; }

        public CombinedFromEllipses(EllipseGeometry disc, EllipseGeometry bite)
        {
            Point c0 = disc.Center, c1 = bite.Center;
            double r0 = disc.RadiusX, r1 = bite.RadiusX;
            double dx = c1.X - c0.X, dy = c1.Y - c0.Y, d = Math.Sqrt(dx * dx + dy * dy);
            double a = (r0 * r0 - r1 * r1 + d * d) / (2 * d), hh = Math.Sqrt(Math.Max(0, r0 * r0 - a * a));
            double mx = c0.X + a * dx / d, my = c0.Y + a * dy / d;
            var p1 = new Point(mx + hh * dy / d, my - hh * dx / d);
            var p2 = new Point(mx - hh * dy / d, my + hh * dx / d);
            var f = new PathFigure { StartPoint = p1, IsClosed = true, IsFilled = true };
            // Round the far side of the disc, then back along the bite's edge.
            f.Segments.Add(new ArcSegment { Point = p2, Size = new Windows.Foundation.Size(r0, r0), IsLargeArc = true, SweepDirection = SweepDirection.Counterclockwise });
            f.Segments.Add(new ArcSegment { Point = p1, Size = new Windows.Foundation.Size(r1, r1), IsLargeArc = false, SweepDirection = SweepDirection.Clockwise });
            Geometry = new PathGeometry { Figures = { f } };
        }
    }

    // The symbols' colours over each ink (IconColors): grey clouds and a gold moon over bright glass,
    // white only on the dimmed glass.
    private static (Color Sun, Color Moon, Color Cloud, Color Grey, Color Rain, Color Arrow) IconColors(Ink ink) => ink switch
    {
        Ink.Dark => (Rgb(0xFFD60A), Rgb(0xF5C451), Rgb(0xA4ADB8), Rgb(0x7D8794), Rgb(0x64D2FF), Rgb(0x6E7681)),
        Ink.Mono => (Rgb(0xFFFFFF, 0.72), Rgb(0xFFFFFF, 0.9), Rgb(0xFFFFFF, 0.96), Rgb(0xFFFFFF, 0.8), Rgb(0xFFFFFF, 0.85), Rgb(0xFFFFFF, 0.96)),
        _ => (Rgb(0xFFD60A), Rgb(0xF2F2F7), Rgb(0xF7F8FA), Rgb(0xD1D5DB), Rgb(0x64D2FF), Rgb(0xF2F5F9)),
    };

    private static void Icon(Canvas c, int code, bool day, double cx, double cy, double s, int evt, Ink ink)
    {
        var k = IconColors(ink);
        Color white = k.Cloud, grey = k.Grey, gold = k.Sun;
        double lw = Math.Max(1, 0.07 * s);
        if (evt != 0)
        {
            // A half sun on the horizon, an arrow up (sunrise) or down (sunset).
            var half = new Canvas { Width = c.Width, Height = c.Height, Clip = new RectangleGeometry { Rect = new Rect(cx - s, cy - s, 2 * s, 1.12 * s) } };
            Sun(half, cx, cy + 0.12 * s, s * 0.9, gold);
            c.Children.Add(half);
            Line(c, cx - 0.42 * s, cy + 0.2 * s, cx + 0.42 * s, cy + 0.2 * s, gold, lw);
            double y0 = cy - 0.46 * s, y1 = cy - 0.2 * s, tip = evt == 1 ? y0 : y1, back = evt == 1 ? y0 + 0.1 * s : y1 - 0.1 * s;
            Line(c, cx, y0, cx, y1, k.Arrow, lw);
            Line(c, cx - 0.1 * s, back, cx, tip, k.Arrow, lw);
            Line(c, cx + 0.1 * s, back, cx, tip, k.Arrow, lw);
            return;
        }
        switch (SkyOf(code))
        {
            case Sky.Clear:
                if (day) Sun(c, cx, cy, s, gold); else Moon(c, cx, cy, s, k.Moon);
                break;
            case Sky.Partly:
                if (day) Sun(c, cx - 0.16 * s, cy - 0.14 * s, s * 0.78, gold); else Moon(c, cx - 0.14 * s, cy - 0.14 * s, s * 0.8, k.Moon);
                Cloud(c, cx + 0.08 * s, cy + 0.06 * s, s * 0.82, white);
                break;
            case Sky.Cloudy:
                Cloud(c, cx, cy - 0.04 * s, s, white);
                break;
            case Sky.Fog:
                for (int i = 0; i < 3; i++)
                {
                    double y = cy - 0.2 * s + i * 0.2 * s, inset = i == 1 ? 0 : 0.08 * s;
                    Line(c, cx - 0.4 * s + inset, y, cx + 0.4 * s - inset, y, white, Math.Max(1, 0.09 * s));
                }
                break;
            default:
            {
                var kind = SkyOf(code);
                Cloud(c, cx, cy - 0.16 * s, s, kind == Sky.Storm ? grey : white);
                for (int i = 0; i < 3; i++)
                {
                    double x = cx - 0.2 * s + i * 0.2 * s, y = cy + 0.24 * s;
                    if (kind == Sky.Snow) Circle(c, x, y + (i % 2 == 1 ? 0.08 : 0) * s, 0.055 * s, white);
                    else if (kind != Sky.Storm || i != 1) Line(c, x + 0.03 * s, y - 0.04 * s, x - 0.04 * s, y + 0.12 * s, k.Rain, lw);
                }
                if (kind == Sky.Storm)
                {
                    double bw = Math.Max(1, 0.08 * s);
                    Color bolt = ink == Ink.Mono ? k.Arrow : gold;
                    Line(c, cx + 0.04 * s, cy + 0.08 * s, cx - 0.06 * s, cy + 0.26 * s, bolt, bw);
                    Line(c, cx - 0.06 * s, cy + 0.26 * s, cx + 0.05 * s, cy + 0.26 * s, bolt, bw);
                    Line(c, cx + 0.05 * s, cy + 0.26 * s, cx - 0.05 * s, cy + 0.44 * s, bolt, bw);
                }
                break;
            }
        }
    }
}
