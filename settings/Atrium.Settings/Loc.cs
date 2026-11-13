using System.Globalization;
using System.Text.Json.Nodes;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Automation;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Documents;
using Microsoft.UI.Xaml.Media;

namespace Atrium.Settings;

// The settings window's language (SETTINGS-PROTOCOL: `language`). English is
// the source language: XAML and code name English strings, and Loc.T() looks
// up a locale file. A missing translation falls back to English. The window is
// recreated after a language change, so statics never cache a stale language.
internal static class Loc
{
    private static string _tag = "en";
    private static readonly Dictionary<string, string> Strings = new(StringComparer.Ordinal);
    private static readonly Dictionary<string, Dictionary<string, string>> Plurals = new(StringComparer.Ordinal);
    private static readonly Dictionary<string, string> Legacy = new(StringComparer.Ordinal);
    private static List<(string Tag, string Name)>? _languages;
    private static bool _legacyLoaded;

    public static string Tag => _tag;
    // Date and lunar layouts are Chinese or Latin; the Latin branch is also
    // the fallback for a language whose dates are not translated yet.
    public static bool English => !Chinese;
    public static bool Chinese => _tag.StartsWith("zh", StringComparison.OrdinalIgnoreCase);

    // The picker: manifest order, with the source language first.
    public static IReadOnlyList<(string Tag, string Name)> Languages =>
        _languages ??= LoadManifest();

    // "auto" follows Windows' UI language (Chinese -> zh-Hans, anything else ->
    // en); any other value names a locale file. Unknown values fall back to en.
    public static void Apply(string? setting)
    {
        _tag = NormalizeTag(setting);
        Strings.Clear();
        Plurals.Clear();
        if (_tag != "en" && !LoadLocale(_tag))
            _tag = "en";
        LoadLegacy();
    }

    // The saved choice, read before the window is built.
    public static void Load()
    {
        string setting = "auto";
        try
        {
            if (JsonNode.Parse(File.ReadAllText(CoreClient.SettingsPath)) is JsonObject o &&
                o["language"] is JsonValue v && v.TryGetValue(out string? text) && !string.IsNullOrWhiteSpace(text))
                setting = text;
        }
        catch (Exception error) when (error is IOException or UnauthorizedAccessException or System.Text.Json.JsonException) { }
        Apply(setting);
    }

    public static string T(string key, string? context = null)
    {
        if (string.IsNullOrEmpty(key))
            return key;
        if (Find(key, context) is { } text)
            return text;
        if (Legacy.TryGetValue(key, out string? english))
        {
            if (_tag == "en")
                return english;
            if (Find(english, null) is { } localized)
                return localized;
            return english;
        }
        return key;
    }

    public static string F(string key, params object?[] args) => string.Format(T(key), args);

    // Selects one/other from a plural entry and substitutes {0} with `count`.
    public static string Plural(string key, int count, string? context = null)
    {
        Dictionary<string, string>? forms = FindPlural(key, context) ?? FindPlural(key, null);
        string text;
        if (forms != null && forms.TryGetValue(count == 1 ? "one" : "other", out string? chosen))
            text = chosen;
        else if (forms != null && forms.TryGetValue("other", out string? other))
            text = other;
        else
            text = T(key, context);
        return text.Replace("{0}", count.ToString(CultureInfo.InvariantCulture));
    }

    private static string? Find(string key, string? context)
    {
        if (!string.IsNullOrEmpty(context) && Strings.TryGetValue($"{key}#{context}", out string? qualified))
            return qualified;
        return Strings.TryGetValue(key, out string? plain) ? plain : null;
    }

    private static Dictionary<string, string>? FindPlural(string key, string? context)
    {
        if (!string.IsNullOrEmpty(context) && Plurals.TryGetValue($"{key}#{context}", out var qualified))
            return qualified;
        return Plurals.TryGetValue(key, out var plain) ? plain : null;
    }

    private static string NormalizeTag(string? setting)
    {
        if (string.IsNullOrWhiteSpace(setting) || setting == "auto")
            return CultureInfo.CurrentUICulture.TwoLetterISOLanguageName == "zh" ? "zh-Hans" : "en";
        return setting == "zh" ? "zh-Hans" : setting;
    }

    private static string LocaleDir()
    {
        string current = AppContext.BaseDirectory;
        for (int i = 0; i < 8; i++)
        {
            string candidate = Path.Combine(current, "locales");
            if (File.Exists(Path.Combine(candidate, "manifest.json")))
                return candidate;
            DirectoryInfo? parent = Directory.GetParent(current);
            if (parent == null)
                break;
            current = parent.FullName;
        }
        return Path.Combine(AppContext.BaseDirectory, "locales");
    }

    private static bool LoadLocale(string tag)
    {
        try
        {
            string path = Path.Combine(LocaleDir(), tag + ".json");
            if (!File.Exists(path) || JsonNode.Parse(File.ReadAllText(path)) is not JsonObject root ||
                root["strings"] is not JsonObject strings)
                return false;
            foreach (var (key, value) in strings)
            {
                if (value is JsonValue v && v.TryGetValue(out string? text))
                    Strings[key] = text;
                else if (value is JsonObject forms)
                {
                    var plural = new Dictionary<string, string>(StringComparer.Ordinal);
                    foreach (var (form, formValue) in forms)
                        if (formValue is JsonValue fv && fv.TryGetValue(out string? formText))
                            plural[form] = formText;
                    if (plural.Count > 0)
                        Plurals[key] = plural;
                }
            }
            return Strings.Count > 0 || Plurals.Count > 0;
        }
        catch (Exception error) when (error is IOException or UnauthorizedAccessException or System.Text.Json.JsonException) { return false; }
    }

    private static void LoadLegacy()
    {
        if (_legacyLoaded)
            return;
        try
        {
            string path = Path.Combine(LocaleDir(), "legacy.json");
            if (File.Exists(path) && JsonNode.Parse(File.ReadAllText(path)) is JsonObject root &&
                root["strings"] is JsonObject strings)
            {
                foreach (var (key, value) in strings)
                    if (value is JsonValue v && v.TryGetValue(out string? text))
                        Legacy[key] = text;
            }
            _legacyLoaded = true;
        }
        catch (Exception error) when (error is IOException or UnauthorizedAccessException or System.Text.Json.JsonException) { }
    }

    private static List<(string Tag, string Name)> LoadManifest()
    {
        var languages = new List<(string Tag, string Name)> { ("en", "English"), ("zh-Hans", "简体中文") };
        try
        {
            string path = Path.Combine(LocaleDir(), "manifest.json");
            if (File.Exists(path) && JsonNode.Parse(File.ReadAllText(path)) is JsonObject root &&
                root["languages"] is JsonArray list)
            {
                var found = new List<(string Tag, string Name)>();
                foreach (JsonNode? entry in list)
                {
                    if (entry is JsonObject language &&
                        language["tag"] is JsonValue tagValue && tagValue.TryGetValue(out string? tag) &&
                        language["name"] is JsonValue nameValue && nameValue.TryGetValue(out string? name))
                        found.Add((tag, name));
                }
                if (found.Count > 0)
                    languages = found;
            }
        }
        catch (Exception error) when (error is IOException or UnauthorizedAccessException or System.Text.Json.JsonException) { }
        return languages;
    }

    public static void Localize(DependencyObject root)
    {
        if (English) return;
        ApplyTo(root);
        int children = VisualTreeHelper.GetChildrenCount(root);
        for (int i = 0; i < children; i++)
            Localize(VisualTreeHelper.GetChild(root, i));
    }

    private static void TranslateInlines(InlineCollection inlines)
    {
        foreach (var inline in inlines)
        {
            if (inline is Run run && !string.IsNullOrEmpty(run.Text))
                run.Text = T(run.Text);
            else if (inline is Span span)
                TranslateInlines(span.Inlines);
        }
    }

    private static void ApplyTo(DependencyObject d)
    {
        switch (d)
        {
            // Run by run: setting Text would replace the inlines, and code that
            // keeps a named Run (a slider's value) would go on updating a detached one.
            case TextBlock text when text.Inlines.Count > 0:
                TranslateInlines(text.Inlines);
                break;
            case TextBlock text when !string.IsNullOrEmpty(text.Text):
                text.Text = T(text.Text);
                break;
            case Run run when !string.IsNullOrEmpty(run.Text):
                run.Text = T(run.Text);
                break;
            case ToggleSwitch toggle:
                if (toggle.Header is string header) toggle.Header = T(header);
                if (toggle.OnContent is string on) toggle.OnContent = T(on);
                if (toggle.OffContent is string off) toggle.OffContent = T(off);
                break;
            case ComboBox combo:
                if (combo.Header is string comboHeader) combo.Header = T(comboHeader);
                if (!string.IsNullOrEmpty(combo.PlaceholderText)) combo.PlaceholderText = T(combo.PlaceholderText);
                for (int i = 0; i < combo.Items.Count; i++)
                {
                    if (combo.Items[i] is ComboBoxItem item && item.Content is string itemText && !string.IsNullOrEmpty(itemText))
                        item.Content = T(itemText);
                }
                break;
            case TextBox box:
                if (box.Header is string boxHeader) box.Header = T(boxHeader);
                if (!string.IsNullOrEmpty(box.PlaceholderText)) box.PlaceholderText = T(box.PlaceholderText);
                break;
            case ToggleTile tile:
                tile.Title = T(tile.Title);
                tile.Note = T(tile.Note);
                tile.SwitchName = T(tile.SwitchName);
                break;
            case NavLabel nav:
                nav.Text = T(nav.Text);
                break;
            case ContentControl content when content.Content is string text2 && !string.IsNullOrEmpty(text2):
                content.Content = T(text2);
                break;
            // A RadioButton's card (the presets) is not in the visual tree until
            // its template is applied: translate the content object directly.
            case ContentControl content2 when content2.Content is DependencyObject child:
                Localize(child);
                break;
            case MenuFlyoutItem item:
                item.Text = T(item.Text);
                break;
            case MenuFlyoutSubItem sub:
                sub.Text = T(sub.Text);
                break;
        }
        if (ToolTipService.GetToolTip(d) is string tip)
            ToolTipService.SetToolTip(d, T(tip));
        string name = AutomationProperties.GetName(d);
        if (!string.IsNullOrEmpty(name))
            AutomationProperties.SetName(d, T(name));
    }

}
