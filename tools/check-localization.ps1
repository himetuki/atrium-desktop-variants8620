# Static resource checks. Dynamic OS/data names use locales/legacy.json.
param([string]$ProjectRoot = (Split-Path -Parent $PSScriptRoot))
$ErrorActionPreference = 'Stop'
$root = $ProjectRoot
$errors = [System.Collections.Generic.List[string]]::new()
if (-not ('Atrium.Settings.LocalizationValidation' -as [type])) {
    Add-Type -Path (Join-Path $PSScriptRoot '../settings/Atrium.Settings/LocalizationValidation.cs')
}

function Read-Json([string]$path) {
    try {
        $text = Get-Content -LiteralPath $path -Raw -Encoding utf8
        $duplicates = [Atrium.Settings.LocalizationValidation]::DuplicateKeys($text)
        foreach ($duplicate in $duplicates) { $errors.Add("${path}: duplicate key $duplicate") }
        if ($duplicates.Count) { return $null }
        return ($text | ConvertFrom-Json -AsHashtable)
    } catch { $errors.Add("${path}: invalid JSON: $($_.Exception.Message)"); return $null }
}

$localeDir = Join-Path $root 'locales'
$manifest = Read-Json (Join-Path $localeDir 'manifest.json')
$localeFiles = [System.Collections.Generic.Dictionary[string, object]]::new([StringComparer]::OrdinalIgnoreCase)
$tags = [System.Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
if (-not $manifest -or $manifest['source'] -ne 'en' -or -not $manifest['languages']) {
    $errors.Add('manifest.json must declare English source keys and a language list.')
} else {
    foreach ($language in $manifest['languages']) {
        $tag = [string]$language['tag']
        if (-not [Atrium.Settings.LocalizationValidation]::ValidTag($tag) -or -not $tags.Add($tag)) {
            $errors.Add("manifest: invalid or duplicate language tag '$tag'")
            continue
        }
        if ($tag -eq 'en') { continue }
        $path = Join-Path $localeDir ($tag + '.json')
        $json = Read-Json $path
        if ($json -and $json['strings'] -is [System.Collections.IDictionary]) { $localeFiles[$tag] = $json['strings'] }
        else { $errors.Add("${path}: missing strings object") }
    }
    if (-not $tags.Contains('en')) { $errors.Add('manifest: missing source language en') }
}
if (Test-Path (Join-Path $localeDir 'legacy.json')) { $null = Read-Json (Join-Path $localeDir 'legacy.json') }

$sourceKeys = [System.Collections.Generic.Dictionary[string, object]]::new([StringComparer]::Ordinal)
function Add-Key([string]$key, [string]$kind) {
    if (-not $sourceKeys.ContainsKey($key)) { $sourceKeys[$key] = [System.Collections.Generic.HashSet[string]]::new() }
    [void]$sourceKeys[$key].Add($kind)
}
function Unescaped([string]$text) { try { [regex]::Unescape($text) } catch { $text } }

$nativeFiles = Get-ChildItem (Join-Path $root 'native/core') -Recurse -File -Include *.cpp,*.h |
    Where-Object Name -ne 'localization.cpp'
foreach ($file in $nativeFiles) {
    $text = Get-Content -LiteralPath $file.FullName -Raw -Encoding utf8
    foreach ($match in [regex]::Matches($text, '\b(Tr(?:Format|Plural)?)\s*\(\s*L"((?:[^"\\]|\\.)*)"')) {
        $kind = switch ($match.Groups[1].Value) { 'TrFormat' {'native'} 'TrPlural' {'managed'} default {'raw'} }
        Add-Key (Unescaped $match.Groups[2].Value) $kind
    }
}
$settingsFiles = Get-ChildItem (Join-Path $root 'settings/Atrium.Settings') -File -Filter *.cs |
    Where-Object Name -ne 'Loc.cs'
foreach ($file in $settingsFiles) {
    $text = Get-Content -LiteralPath $file.FullName -Raw -Encoding utf8
    foreach ($match in [regex]::Matches($text, '\bLoc\.(T|F|Plural)\s*\(\s*"((?:[^"\\]|\\.)*)"')) {
        $kind = if ($match.Groups[1].Value -eq 'T') { 'raw' } else { 'managed' }
        Add-Key (Unescaped $match.Groups[2].Value) $kind
    }
}
foreach ($file in (Get-ChildItem (Join-Path $root 'settings/Atrium.Settings') -File -Filter *.xaml)) {
    $text = Get-Content -LiteralPath $file.FullName -Raw -Encoding utf8
    foreach ($match in [regex]::Matches($text, '(?:Text|Title|Note|Content|SwitchName|Header|PlaceholderText|AutomationProperties\.Name|ToolTipService\.ToolTip)="([^"]*[A-Za-z][^"]*)"')) {
        $value = [System.Net.WebUtility]::HtmlDecode($match.Groups[1].Value)
        if (-not $value.StartsWith('{')) { Add-Key $value 'raw' }
    }
}

foreach ($locale in $localeFiles.GetEnumerator()) {
    $tag = $locale.Key
    $table = $locale.Value
    foreach ($source in $sourceKeys.GetEnumerator()) {
        $key = $source.Key
        if (-not $table.Contains($key)) { $errors.Add("locale ${tag}: missing key '$key'") }
    }
    foreach ($translation in $table.GetEnumerator()) {
        $key = $translation.Key
        $baseKey = $key
        if (-not $sourceKeys.ContainsKey($baseKey)) {
            $contextAt = $key.LastIndexOf('#')
            if ($contextAt -ge 0) { $baseKey = $key.Substring(0, $contextAt) }
        }
        $kinds = if ($sourceKeys.ContainsKey($baseKey)) { $sourceKeys[$baseKey] } else { @() }
        $entry = $translation.Value
        $plural = $entry -is [System.Collections.IDictionary]
        if ($plural -and (-not $entry.Contains('other') -or [string]::IsNullOrEmpty([string]$entry['other']))) {
            $errors.Add("locale ${tag}: plural '$key' must contain an other form")
        }
        $forms = if ($plural) { @($entry.GetEnumerator() | ForEach-Object Key) } else { @('text') }
        foreach ($form in $forms) {
            $value = if ($plural) { $entry[$form] } else { $entry }
            if ($value -isnot [string] -or [string]::IsNullOrEmpty($value) -or $value.Contains([char]0)) {
                $errors.Add("locale ${tag}: '$key' ($form) must be a nonempty string")
                continue
            }
            if ($kinds -contains 'native' -and -not [Atrium.Settings.LocalizationValidation]::NativeMatches($baseKey, $value)) {
                $errors.Add("locale ${tag}: native placeholder mismatch for '$key' ($form)")
            }
            if ($plural -and -not [Atrium.Settings.LocalizationValidation]::CountMatches($baseKey, $value)) {
                $errors.Add("locale ${tag}: count placeholder mismatch for '$key' ($form)")
            }
            if ($kinds -contains 'managed' -and -not [Atrium.Settings.LocalizationValidation]::ManagedMatches($baseKey, $value)) {
                $errors.Add("locale ${tag}: managed placeholder mismatch for '$key' ($form)")
            }
        }
    }
}
if ($errors.Count) {
    foreach ($message in $errors) { Write-Host "ERROR: $message" }
    Write-Host "Localization check failed ($($errors.Count) error(s))."
    exit 1
}
Write-Host "Localization check passed: $($sourceKeys.psbase.Count) source keys, $($localeFiles.psbase.Count) translated language(s)."
exit 0
