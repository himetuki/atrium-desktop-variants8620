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

## Legacy names

`legacy.json` is not a UI translation table. It maps Chinese names supplied by
Windows (jump-list sections, known app names, cities, lunar festivals) to their
English source keys, so old settings and OS data keep working.
