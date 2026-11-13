#nullable enable
using System;
using System.Collections.Generic;
using System.Globalization;
using System.Linq;
using System.Text;
using System.Text.Json;
using System.Text.RegularExpressions;

namespace Atrium.Settings;

// Shared by the runtime, resource checker and regression tests. Raw UI text
// is not a printf template; callers select the appropriate format grammar.
public static class LocalizationValidation
{
    public static bool ValidTag(string tag) => tag.Length is > 0 and <= 32 &&
        Regex.IsMatch(tag, @"\A[A-Za-z0-9]+(?:-[A-Za-z0-9]+)*\z", RegexOptions.CultureInvariant);

    public static string[]? NativeTokens(string text)
    {
        var tokens = new List<string>();
        for (int i = 0; i < text.Length; i++)
        {
            if (text[i] != '%') continue;
            if (i + 1 < text.Length && text[i + 1] == '%') { i++; continue; }
            var token = NativePattern.Match(text, i);
            if (!token.Success) return null;
            tokens.Add(token.Value);
            i += token.Length - 1;
        }
        return tokens.ToArray();
    }

    private static readonly Regex NativePattern = new(@"\G%[-+ #0]*(?:\*|[0-9]+)?(?:\.(?:\*|[0-9]*))?(?:I32|I64|hh|ll|[hljztLIw])?[diouxXfFeEgGaAcCsSp]",
        RegexOptions.CultureInvariant, TimeSpan.FromSeconds(1));

    public static bool NativeMatches(string source, string translated)
    {
        var expected = NativeTokens(source);
        var actual = NativeTokens(translated);
        return expected != null && actual != null && expected.SequenceEqual(actual, StringComparer.Ordinal);
    }

    public static string[]? ManagedTokens(string text)
    {
        try { CompositeFormat.Parse(text); }
        catch (FormatException) { return null; }
        var tokens = new List<string>();
        for (int i = 0; i < text.Length; i++)
        {
            if (text[i] != '{') continue;
            if (i + 1 < text.Length && text[i + 1] == '{') { i++; continue; }
            int field = i;
            int begin = ++i;
            while (i < text.Length && text[i] is >= '0' and <= '9') i++;
            if (!int.TryParse(text.AsSpan(begin, i - begin), NumberStyles.None, CultureInfo.InvariantCulture, out int index)) return null;
            while (i < text.Length && text[i] != '}') i++;
            tokens.Add(text.Substring(field, i - field + 1));
        }
        // Reordering is safe; new alignment/precision/format specifiers are not.
        tokens.Sort(StringComparer.Ordinal);
        return tokens.ToArray();
    }

    public static bool ManagedMatches(string source, string translated)
    {
        var expected = ManagedTokens(source);
        var actual = ManagedTokens(translated);
        return expected != null && actual != null && expected.SequenceEqual(actual);
    }

    public static bool CountMatches(string source, string translated)
    {
        // TrPlural and Loc.Plural share the {0}-only, unformatted count syntax.
        static bool Valid(string value) => Regex.Replace(value, @"\{\{|\}\}|\{0\}", "").IndexOfAny(new[] { '{', '}' }) < 0;
        return Valid(source) && Valid(translated) && ManagedMatches(source, translated);
    }

    public static string[] DuplicateKeys(string json)
    {
        using var document = JsonDocument.Parse(json);
        var duplicates = new List<string>();
        FindDuplicates(document.RootElement, "$", duplicates);
        return duplicates.ToArray();
    }

    private static void FindDuplicates(JsonElement value, string path, List<string> duplicates)
    {
        if (value.ValueKind == JsonValueKind.Object)
        {
            var seen = new HashSet<string>(StringComparer.Ordinal);
            foreach (var property in value.EnumerateObject())
            {
                string child = path + "." + property.Name;
                if (!seen.Add(property.Name)) duplicates.Add(child);
                FindDuplicates(property.Value, child, duplicates);
            }
        }
        else if (value.ValueKind == JsonValueKind.Array)
        {
            int index = 0;
            foreach (var child in value.EnumerateArray()) FindDuplicates(child, path + "[" + index++ + "]", duplicates);
        }
    }
}
