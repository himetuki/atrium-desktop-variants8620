# Fails when an English source key has no entry in a locale file, a key is
# duplicated, or a format placeholder differs between English and a locale.
# Only static literals are checked; dynamic keys (Loc.T(name)) are not.
$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
$errors = [System.Collections.Generic.List[string]]::new()

function Read-Text([string]$path) {
    Get-Content -LiteralPath $path -Raw -Encoding utf8
}

function Line-Of([string]$text, [int]$index) {
    ($text.Substring(0, $index) -split "`n").Count
}

function Unescaped([string]$text) {
    try { [regex]::Unescape($text) } catch { $text }
}

function Native-Placeholders([string]$text) {
    (@([regex]::Matches($text, '%[-+0-9.#]*[diufegsxX]') | ForEach-Object { $_.Value }) | Sort-Object) -join ','
}

function Managed-Placeholders([string]$text) {
    (@([regex]::Matches($text, '\{(\d+)(?:[^}]*)\}') | ForEach-Object { $_.Groups[1].Value }) |
        Sort-Object { [int]$_ }) -join ','
}

function Check-Calls {
    param(
        [string]$Label,
        [string]$Path,
        [string]$Pattern,
        [hashtable]$Table,
        [string]$PlaceholderKind
    )
    $text = Read-Text $Path
    foreach ($match in [regex]::Matches($text, $Pattern)) {
        $key = Unescaped $match.Groups[1].Value
        if (-not $Table.ContainsKey($key)) {
            $line = Line-Of $text $match.Index
            $errors.Add("${Label}: ${Path}:${line}: missing locale entry for `"$key`"")
            continue
        }
        $english = $key
        $localized = $Table[$key]
        if ($localized -is [hashtable]) {
            $localized = if ($localized.ContainsKey('other')) { $localized['other'] } else { $localized.Values | Select-Object -First 1 }
        }
        if ($localized -isnot [string]) { continue }
        if ($PlaceholderKind -eq "native") {
            if ((Native-Placeholders $english) -ne (Native-Placeholders $localized)) {
                $line = Line-Of $text $match.Index
                $errors.Add("${Label}: ${Path}:${line}: placeholder mismatch for `"$key`" -> `"$localized`"")
            }
        } else {
            if ((Managed-Placeholders $english) -ne (Managed-Placeholders $localized)) {
                $line = Line-Of $text $match.Index
                $errors.Add("${Label}: ${Path}:${line}: placeholder mismatch for `"$key`" -> `"$localized`"")
            }
        }
    }
}

# --- locale files -----------------------------------------------------------
$localeDir = Join-Path $root "locales"
$manifest = Read-Text (Join-Path $localeDir "manifest.json") | ConvertFrom-Json -AsHashtable
$localeFiles = @{}
foreach ($language in $manifest.languages) {
    if ($language.tag -eq 'en') { continue } # English is the source key itself
    $path = Join-Path $localeDir ($language.tag + ".json")
    if (-not (Test-Path $path)) {
        $errors.Add("locale: missing file for $($language.tag): $path")
        continue
    }
    $json = Read-Text $path | ConvertFrom-Json -AsHashtable
    $localeFiles[$language.tag] = $json.strings
}

# The source language (English) is the key itself. Check every complete locale
# against the source keys found in code and XAML.
$sourceKeys = [System.Collections.Generic.HashSet[string]]::new([StringComparer]::Ordinal)
$nativeFiles = Get-ChildItem (Join-Path $root "native/core") -Recurse -File -Include *.cpp, *.h |
    Where-Object { $_.Name -ne "localization.cpp" }
foreach ($file in $nativeFiles) {
    $text = Read-Text $file.FullName
    foreach ($match in [regex]::Matches($text, '\bTr(?:Format|Plural)?\s*\(\s*L"((?:[^"\\]|\\.)*)"')) {
        [void]$sourceKeys.Add((Unescaped $match.Groups[1].Value))
    }
}
$settingsFiles = Get-ChildItem (Join-Path $root "settings/Atrium.Settings") -File -Filter *.cs |
    Where-Object { $_.Name -ne "Loc.cs" }
foreach ($file in $settingsFiles) {
    $text = Read-Text $file.FullName
    foreach ($match in [regex]::Matches($text, '\bLoc\.(?:T|F|Plural)\s*\(\s*"((?:[^"\\]|\\.)*)"')) {
        [void]$sourceKeys.Add((Unescaped $match.Groups[1].Value))
    }
}
$xamlFiles = Get-ChildItem (Join-Path $root "settings/Atrium.Settings") -File -Filter *.xaml
foreach ($file in $xamlFiles) {
    $text = Read-Text $file.FullName
    $pattern = '(?:Text|Title|Note|Content|SwitchName|Header|PlaceholderText|AutomationProperties\.Name|ToolTipService\.ToolTip)="([^"]*[A-Za-z][^"]*)"'
    foreach ($match in [regex]::Matches($text, $pattern)) {
        $value = $match.Groups[1].Value
        if ($value.StartsWith('{')) { continue }
        [void]$sourceKeys.Add($value)
    }
}

foreach ($tag in $localeFiles.Keys) {
    $table = $localeFiles[$tag]
    foreach ($key in $sourceKeys) {
        if (-not $table.ContainsKey($key)) {
            $errors.Add("locale ${tag}: missing key `"$key`"")
            continue
        }
        $localized = $table[$key]
        if ($localized -is [hashtable]) {
            $localized = if ($localized.ContainsKey('other')) { $localized['other'] } else { $localized.Values | Select-Object -First 1 }
        }
        if ($localized -isnot [string]) { continue }
        if ((Managed-Placeholders $key) -ne (Managed-Placeholders $localized)) {
            $errors.Add("locale ${tag}: placeholder mismatch for `"$key`" -> `"$localized`"")
        }
    }
}

# Call sites also need checking for placeholders against the locale text.
foreach ($file in $nativeFiles) {
    Check-Calls -Label "native" -Path $file.FullName -Pattern '\bTr(?:Format|Plural)?\s*\(\s*L"((?:[^"\\]|\\.)*)"' `
        -Table $localeFiles['zh-Hans'] -PlaceholderKind "native"
}
foreach ($file in $settingsFiles) {
    Check-Calls -Label "settings" -Path $file.FullName -Pattern '\bLoc\.(?:T|F|Plural)\s*\(\s*"((?:[^"\\]|\\.)*)"' `
        -Table $localeFiles['zh-Hans'] -PlaceholderKind "settings"
}

# Duplicate keys in a JSON file would silently keep only one value.
foreach ($tag in $localeFiles.Keys) {
    $path = Join-Path $localeDir ($tag + ".json")
    $raw = Read-Text $path
    $seen = @{}
    foreach ($match in [regex]::Matches($raw, '"((?:[^"\\]|\\.)*)"\s*:')) {
        $key = $match.Groups[1].Value
        if ($key -in @('schema', 'language', 'strings')) { continue }
        if ($seen.ContainsKey($key)) { $errors.Add("locale ${tag}: duplicate key `"$key`"") }
        $seen[$key] = $true
    }
}

if ($errors.Count -gt 0) {
    foreach ($message in $errors) { Write-Host "ERROR: $message" }
    Write-Host "Localization check failed ($($errors.Count) error(s))."
    exit 1
}
Write-Host "Localization check passed: $($sourceKeys.Count) source keys, $($localeFiles['zh-Hans'].Count) zh-Hans entries."
