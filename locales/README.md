# Locales

English is the source language. Code and XAML use English keys; a locale file
maps those keys to another language. An untranslated key falls back to English.

```text
locales/
  manifest.json     language picker entries
  zh-Hans.json      Simplified Chinese
  legacy.json       old Chinese OS/data names -> English (compatibility only)
```

## Add a language

1. Copy `zh-Hans.json` to `<tag>.json` (a BCP 47 tag such as `ja` or `fr`).
2. Translate the values; keep every key and its `{0}` / `%s` placeholders.
3. Add the tag and its native name to `manifest.json`.
4. Run `pwsh ./tools/check-localization.ps1`, then rebuild.

No C++ or C# change is needed: the native core loads `<exe>/locales/<tag>.json`
and the settings app reads the same directory.

Restart Atrium after editing resources. The core keeps one immutable snapshot
per loaded language so text remains valid while settings change or workers use it.
Files use UTF-8 (a BOM is accepted). Missing, empty or malformed translations
fall back to the English key. Language tags cannot contain directory separators.

## Contexts

Use the same English word in different places with a context suffix:

```json
{
  "Open": "打开",
  "Open#menu": "打开菜单"
}
```

Code: `Tr(L"Open", L"menu")` / `Loc.T("Open", "menu")`. A missing context entry
falls back to the plain key.

## Plurals

A value may be an object with `one` and `other` (the loader also accepts other
CLDR categories for future languages):

```json
{
  "{0} items": {
    "one": "{0} item",
    "other": "{0} items"
  }
}
```

Use `TrPlural(L"{0} items", count)` / `Loc.Plural("{0} items", count)`.

Every plural entry needs an `other` form; `one` is optional. The count syntax is
`{0}` and may occur more than once. Use `{{` and `}}` for literal braces.

## Formats and checks

Keep complete placeholders unchanged, including size modifiers, alignment and
precision. Native printf conversions must remain in order (`%s`, `%lu`, etc.);
managed composite fields may be reordered (`{1}` before `{0}`). Never add `%n`
or positional printf conversions. The runtime falls back to the source format
when a translated format is incompatible.

The resource checker validates every language, context override and plural form.
Duplicate JSON properties are checked within each object, so multiple plural
entries can each contain `one` and `other`.

## Legacy names

`legacy.json` is not a UI translation table. It maps Chinese names supplied by
Windows (jump-list sections, known app names, cities, lunar festivals) to their
English source keys, so old settings and OS data keep working.
