$ErrorActionPreference = 'Stop'
$project = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$checker = Join-Path $project 'tools/check-localization.ps1'
$pwsh = (Get-Process -Id $PID).Path
$temporary = [IO.Path]::GetFullPath([IO.Path]::GetTempPath())
$fixtureRoot = Join-Path $temporary ('Atrium-locale-tests-' + [guid]::NewGuid().ToString('N'))
$expectedRoot = [IO.Path]::GetFullPath($fixtureRoot)
New-Item -ItemType Directory -Path $fixtureRoot | Out-Null

function New-Case([string]$name) {
    $directory = Join-Path $fixtureRoot $name
    New-Item -ItemType Directory -Path "$directory/locales","$directory/native/core","$directory/settings/Atrium.Settings" | Out-Null
    Set-Content -LiteralPath "$directory/native/core/probe.cpp" -Encoding utf8 -Value @'
Tr(L"Raw 70% of all");
Tr(L"Open", L"menu");
Tr(L"Keys");
Tr(L"Count");
TrFormat(L"Read %s: %lu", L"file", 7ul);
TrPlural(L"{0} items", 1);
TrPlural(L"{0} files", 2);
'@
    Set-Content -LiteralPath "$directory/settings/Atrium.Settings/Probe.cs" -Encoding utf8 -Value 'Loc.F("Item {0}", 1); Loc.F("Values {0} {1}", 1, 2); Loc.F("Issue #{0}", 1);'
    Set-Content -LiteralPath "$directory/settings/Atrium.Settings/Probe.xaml" -Encoding utf8 -Value '<TextBlock Text="Open" />'
    @{source='en';languages=@(@{tag='en';name='English'},@{tag='zh-Hans';name='中文'},@{tag='fr';name='Français'})} |
        ConvertTo-Json -Depth 10 | Set-Content -LiteralPath "$directory/locales/manifest.json" -Encoding utf8
    foreach ($tag in @('zh-Hans','fr')) {
        @{strings=@{
            'Raw 70% of all'='原始 70%'; 'Open'='打开'; 'Open#menu'='打开菜单';
            'Keys'='按键'; 'Count'='计数';
            'Issue #{0}'='问题 #{0}';
            'Read %s: %lu'='读取 %s：%lu'; 'Item {0}'='项目 {0}'; 'Values {0} {1}'='{1} 然后 {0}';
            '{0} items'=@{one='{0} 项';other='{0} 项'};
            '{0} files'=@{one='{0} 文件';other='{0} 文件'}
        }} | ConvertTo-Json -Depth 10 | Set-Content -LiteralPath "$directory/locales/$tag.json" -Encoding utf8
    }
    return $directory
}

function Change-Entry([string]$directory, [string]$key, $value) {
    $path = Join-Path $directory 'locales/fr.json'
    $json = Get-Content -LiteralPath $path -Raw | ConvertFrom-Json -AsHashtable
    $json.strings[$key] = $value
    $json | ConvertTo-Json -Depth 10 | Set-Content -LiteralPath $path -Encoding utf8
}

function Check-Case([string]$directory, [int]$expected, [string]$message = '') {
    $log = Join-Path $directory 'result.log'
    & $pwsh -NoProfile -File $checker -ProjectRoot $directory *> $log
    if ($LASTEXITCODE -ne $expected -or ($message -and (Get-Content -LiteralPath $log -Raw) -notmatch $message)) {
        Get-Content -LiteralPath $log | Write-Host
        throw "Wrong checker result: $directory"
    }
}

try {
    Check-Case (New-Case 'valid-plurals-and-contexts') 0
    $case = New-Case 'case-sensitive-keys'
    Add-Content -LiteralPath "$case/native/core/probe.cpp" -Value 'Tr(L"open");'
    foreach ($tag in @('zh-Hans','fr')) {
        $path = "$case/locales/$tag.json"
        $raw = Get-Content -LiteralPath $path -Raw
        $raw -replace '"Open"\s*:\s*"打开"', '"Open":"打开","open":"lowercase"' | Set-Content -LiteralPath $path -Encoding utf8
    }
    Check-Case $case 0
    $case = New-Case 'native-order'; Change-Entry $case 'Read %s: %lu' '%lu %s'; Check-Case $case 1 'native placeholder mismatch'
    $case = New-Case 'native-size'; Change-Entry $case 'Read %s: %lu' '%s %u'; Check-Case $case 1 'native placeholder mismatch'
    $case = New-Case 'native-width'; Change-Entry $case 'Read %s: %lu' '%1000000000s %lu'; Check-Case $case 1 'native placeholder mismatch'
    $case = New-Case 'plural-one'; Change-Entry $case '{0} items' @{one='{1} item';other='{0} items'}; Check-Case $case 1 'count placeholder mismatch'
    $case = New-Case 'plural-other'; Change-Entry $case '{0} items' @{one='{0} item'}; Check-Case $case 1 'other form'
    $case = New-Case 'composite'; Change-Entry $case 'Item {0}' '{1}'; Check-Case $case 1 'managed placeholder mismatch'
    $case = New-Case 'literal-hash'; Change-Entry $case 'Issue #{0}' '{1}'; Check-Case $case 1 'managed placeholder mismatch'
    $case = New-Case 'composite-width'; Change-Entry $case 'Item {0}' '{0,1000000000}'; Check-Case $case 1 'managed placeholder mismatch'
    $case = New-Case 'composite-precision'; Change-Entry $case 'Item {0}' '{0:N999999999}'; Check-Case $case 1 'managed placeholder mismatch'
    $case = New-Case 'duplicate'
    Set-Content -LiteralPath "$case/locales/fr.json" -Encoding utf8 -Value '{"strings":{"Open":"a","Open":"b"}}'
    Check-Case $case 1 'duplicate key'
    $case = New-Case 'malformed'
    Set-Content -LiteralPath "$case/locales/fr.json" -Encoding utf8 -Value '{broken'
    Check-Case $case 1 'invalid JSON'
    $case = New-Case 'tag'
    Set-Content -LiteralPath "$case/locales/manifest.json" -Encoding utf8 -Value '{"source":"en","languages":[{"tag":"../escape","name":"invalid"}]}'
    Check-Case $case 1 'invalid or duplicate language tag'
    Write-Host 'Localization checker regressions passed (14 cases).'
} finally {
    $resolved = [IO.Path]::GetFullPath($fixtureRoot)
    if ($resolved -ne $expectedRoot -or -not $resolved.StartsWith($temporary, [StringComparison]::OrdinalIgnoreCase) -or
        -not [IO.Path]::GetFileName($resolved).StartsWith('Atrium-locale-tests-')) { throw 'Unexpected test cleanup path' }
    Remove-Item -LiteralPath $resolved -Recurse -Force
}
exit 0
