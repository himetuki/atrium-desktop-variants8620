// The interface's language (SETTINGS-PROTOCOL: `language`). English is the
// source language: every user-visible literal is written in English, and a
// locale file supplies another language. An untranslated key falls back to
// English, so a missing translation never blanks a label.
//
// Locale files live in `<exe>/locales`:
//   manifest.json          the languages the picker offers (the settings app)
//   <tag>.json             {"strings": {"Open": "打开", ...}}
//   legacy.json            old Chinese data names -> English (OS jump lists,
//                          cities and known app names); kept for compatibility.
#pragma once

#include <string>

namespace atrium {

// "auto" follows Windows' UI language (Chinese -> zh-Hans, anything else ->
// en); any other value names a locale file. Unknown values fall back to en.
// Returns true when the active language changed.
bool SetLanguage(const std::wstring& setting);
bool LanguageIsEnglish();
std::wstring LanguageTag();

// The text for an English key in the active language. `context` disambiguates
// the same English word in different places ("Open" file vs menu); a context
// lookup that has no entry falls back to the plain key.
const wchar_t* Tr(const wchar_t* key, const wchar_t* context = nullptr);

// Formats a localized template (printf syntax) into a string.
std::wstring TrFormat(const wchar_t* key, ...);

// Selects one/other from a plural entry and substitutes {0} with `count`.
std::wstring TrPlural(const wchar_t* key, int count, const wchar_t* context = nullptr);

} // namespace atrium
