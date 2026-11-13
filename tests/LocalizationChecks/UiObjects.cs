// Minimal UI objects isolate Loc.cs's traversal without opening an application.
namespace Microsoft.UI.Xaml { public class DependencyObject { } }
namespace Microsoft.UI.Xaml.Documents {
    public class Inline : Microsoft.UI.Xaml.DependencyObject { }
    public class InlineCollection : List<Inline> { }
    public class Run : Inline { public string Text { get; set; } = ""; }
    public class Span : Inline { public InlineCollection Inlines { get; } = new(); }
}
namespace Microsoft.UI.Xaml.Controls {
    using Microsoft.UI.Xaml;
    public class TextBlock : DependencyObject { public string Text { get; set; } = ""; public Documents.InlineCollection Inlines { get; } = new(); }
    public class ContentControl : DependencyObject { public object? Content { get; set; } }
    public class ComboBoxItem : ContentControl { }
    public class ToggleSwitch : DependencyObject { public object? Header { get; set; } public object? OnContent { get; set; } public object? OffContent { get; set; } }
    public class ComboBox : DependencyObject { public object? Header { get; set; } public string PlaceholderText { get; set; } = ""; public List<object> Items { get; } = new(); }
    public class TextBox : DependencyObject { public object? Header { get; set; } public string PlaceholderText { get; set; } = ""; }
    public class MenuFlyoutItem : DependencyObject { public string Text { get; set; } = ""; }
    public class MenuFlyoutSubItem : DependencyObject { public string Text { get; set; } = ""; }
    public static class ToolTipService { public static object? GetToolTip(DependencyObject d) => null; public static void SetToolTip(DependencyObject d, object o) { } }
}
namespace Microsoft.UI.Xaml.Media { public static class VisualTreeHelper { public static int GetChildrenCount(Microsoft.UI.Xaml.DependencyObject d) => 0; public static Microsoft.UI.Xaml.DependencyObject GetChild(Microsoft.UI.Xaml.DependencyObject d, int i) => throw new NotSupportedException(); } }
namespace Microsoft.UI.Xaml.Automation { public static class AutomationProperties { public static string GetName(Microsoft.UI.Xaml.DependencyObject d) => ""; public static void SetName(Microsoft.UI.Xaml.DependencyObject d, string s) { } } }
namespace Atrium.Settings {
    public static class CoreClient { public static string SettingsPath => "unused-review-fixture.json"; }
    public class ToggleTile : Microsoft.UI.Xaml.DependencyObject { public string Title { get; set; } = ""; public string Note { get; set; } = ""; public string SwitchName { get; set; } = ""; }
    public class NavLabel : Microsoft.UI.Xaml.DependencyObject { public string Text { get; set; } = ""; }
}
