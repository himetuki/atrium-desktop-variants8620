using Atrium.Settings;
using Microsoft.UI.Xaml.Controls;
using System.Globalization;

static void Require(bool condition, string message)
{
    if (!condition) throw new InvalidOperationException(message);
}

Loc.Apply("x-test");
Require(Loc.T("Open") == "Aç", "A non-Chinese locale loads.");
var text = new TextBlock { Text = "Open" };
Loc.Localize(text);
Require(text.Text == "Aç", "Non-Chinese UI trees are translated.");
var button = new ContentControl { Content = "Open" };
Loc.Localize(button);
Require((string?)button.Content == "Aç", "Non-Chinese button labels are translated.");
Require(Loc.T("open") == "lowercase", "Keys remain case sensitive.");
Require(Loc.T("Open", "menu") == "Menüyü aç" && Loc.T("打开", "menu") == "Menüyü aç", "Contexts work through the legacy bridge.");
Require(Loc.T("Open", "missing") == "Aç", "Missing contexts fall back to the base key.");
Require(Loc.T("Empty") == "Empty", "Empty translations fall back to the source.");
Require(Loc.T("Nul") == "Nul", "Embedded NUL translations fall back to the source.");
Require(Loc.F("Value {0}", 3) == "Value 3", "An invalid translated argument index falls back safely.");
Require(Loc.F("{0} + {1}", 1, 2) == "2 et 1", "Composite arguments can be reordered.");
var date = new DateTime(2026, 11, 13);
Require(Loc.F("Date {0}", date) == string.Format(CultureInfo.CurrentCulture, "Date {0}", date), "Invalid type-specific formats do not crash the window.");
Require(Loc.Plural("{0} files", 1) == "ONE 1" && Loc.Plural("{0} files", 2) == "OTHER 2", "Plural branches are selected.");
Require(Loc.Plural("{0} files", 2, "menu") == "MENU OTHER 2", "Plural contexts are selected.");
Require(Loc.Plural("{0} and {0}", 2) == "2 / 2", "Every count occurrence is substituted.");
Require(Loc.Plural("{{{0}}} files", 2) == "{2} test", "Literal braces remain literal.");
Require(Loc.Plural("{0} bad", 2) == "2 bad" && Loc.Plural("{0} incomplete", 1) == "1 incomplete", "Invalid or incomplete plurals fall back.");
Loc.Apply("x-malformed");
Require(Loc.Tag == "en" && Loc.T("Open") == "Open", "Malformed JSON clears prior translations.");
Loc.Apply("x-bom");
Require(Loc.T("Open") == "BOM loaded", "UTF-8 BOM resources load correctly.");
Loc.Apply("../../x-test");
Require(Loc.Tag == "en", "A setting cannot traverse outside the locale directory.");
Loc.Apply("ZH-hans");
Require(Loc.Tag == "zh-Hans" && Loc.T("Open") == "打开", "Tags are case insensitive and old zh aliases remain supported.");
Require(!LocalizationValidation.NativeMatches("%s %lu", "%lu %s"), "Native argument order and lengths are checked.");
Require(!LocalizationValidation.NativeMatches("%s", "%n") && !LocalizationValidation.NativeMatches("%s", "%2$s"), "Unsafe or unsupported conversions are rejected.");
Require(LocalizationValidation.NativeMatches("%*.*f %%", "%*.*f %%"), "Width, precision stars and escaped percent are supported.");
Require(!LocalizationValidation.NativeMatches("%.0f", "%.1000000f"), "Translations cannot add unbounded precision.");
Require(LocalizationValidation.ManagedMatches("{{value}} {0} {1}", "{1} {{value}} {0}"), "Managed escapes and reordering are supported.");
Require(!LocalizationValidation.ManagedMatches("{0}", "{{0}}") && !LocalizationValidation.ManagedMatches("{0}", "{0"), "Escaped or malformed fields do not masquerade as arguments.");
Require(!LocalizationValidation.ManagedMatches("{0}", "{0,1000000000}") && !LocalizationValidation.ManagedMatches("{0}", "{0:N999999999}"), "Translations cannot introduce huge alignment or precision.");
Require(LocalizationValidation.DuplicateKeys("{\"a\":{\"one\":\"x\"},\"b\":{\"one\":\"y\"}}").Length == 0, "Plural forms have independent object scopes.");
Require(LocalizationValidation.DuplicateKeys("{\"a\":1,\"a\":2}").Length == 1, "Actual duplicate object properties are rejected.");
await Task.WhenAll(Task.Run(() =>
{
    for (int i = 0; i < 100; i++) Loc.Apply(i % 3 == 0 ? "en" : i % 3 == 1 ? "zh-Hans" : "x-test");
}), Task.Run(() =>
{
    for (int i = 0; i < 1500; i++)
    {
        string value = Loc.T("Open");
        Require(value is "Open" or "打开" or "Aç", "Concurrent loading never exposes partially cleared dictionaries.");
        Require(Loc.F("Concurrent {0}", 3) == "Concurrent 3", "Concurrent formatting remains valid.");
    }
}));
Console.WriteLine("Localization loading, UI traversal, fallback and format checks passed.");
