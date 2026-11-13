param(
    [Parameter(Mandatory = $true)][string]$Dll,
    [Parameter(Mandatory = $true)][string]$Probe,
    [Parameter(Mandatory = $true)][string]$RuntimeDirectory,
    [Parameter(Mandatory = $true)][string]$OutputDirectory,
    [ValidateSet(932, 936)][int]$CodePage = 932,
    [string]$ChildProbe = '',
    [switch]$ExpectFailure
)

$ErrorActionPreference = 'Stop'
$testDirectory = [IO.Path]::GetFullPath($OutputDirectory)
New-Item -ItemType Directory -Path $testDirectory -Force | Out-Null
New-Item -ItemType Directory -Path (Join-Path $testDirectory 'Nepgear') -Force | Out-Null
Copy-Item -LiteralPath $Dll -Destination (Join-Path $testDirectory 'winmm.dll') -Force
Copy-Item -LiteralPath $Probe -Destination (Join-Path $testDirectory 'native_hook_probe.exe') -Force
foreach ($runtimeName in @('LoaderDll_x86.dll', 'LocaleEmulatorPlus_x86.dll', 'LoaderDll_x64.dll', 'LocaleEmulatorPlus_x64.dll')) {
    Copy-Item -LiteralPath (Join-Path $RuntimeDirectory $runtimeName) -Destination (Join-Path $testDirectory ('Nepgear\' + $runtimeName)) -Force
}
$locale = if ($CodePage -eq 932) { 1041 } else { 2052 }
$charset = if ($CodePage -eq 932) { 128 } else { 134 }
$timezone = if ($CodePage -eq 932) { 'Tokyo Standard Time' } else { 'China Standard Time' }
$expectedBias = if ($CodePage -eq 932) { -540 } else { -480 }
$testIni = @"
[System]
Enable=1
ShowStartupPopup=0
[Font]
Enable=0
FileName=
FaceName=
[Window]
Enable=0
[FileRedirect]
Enable=0
Folder=Nepgear
ArchiveFile=unused.chs
[LocaleEmulator]
Enable=1
CodePage=$CodePage
LocaleID=$locale
Charset=$charset
Timezone=$timezone
[GLOBAL]
EnableKrkrz=0
EnableRioShiina=0
[Debug]
Enable=0
LogToFile=1
"@
[IO.File]::WriteAllText((Join-Path $testDirectory 'Nepgear.ini'), $testIni, [Text.Encoding]::Unicode)
$reportPath = Join-Path $testDirectory 'locale-result.json'
if (Test-Path -LiteralPath $reportPath) { Remove-Item -LiteralPath $reportPath -Force }
$probePath = Join-Path $testDirectory 'native_hook_probe.exe'
$dllPath = Join-Path $testDirectory 'winmm.dll'
# Start-Process accepts a single argument string on Windows. Quote paths and
# preserve a non-ASCII argument with spaces through Nepgear's relaunch.
$unicodeArgument = [string]([char]0x65E5) + [char]0x672C + [char]0x8A9E + ' ' + [char]0x4E2D + [char]0x6587
$argumentText = '"' + $dllPath + '" "' + $reportPath + '" "locale regression argument" "' + $unicodeArgument + '"'
if ($ChildProbe) {
    Copy-Item -LiteralPath $ChildProbe -Destination (Join-Path $testDirectory 'opposite_arch_probe.exe') -Force
    $childReport = Join-Path $testDirectory 'child-locale-result.json'
    if (Test-Path -LiteralPath $childReport) { Remove-Item -LiteralPath $childReport -Force }
    $argumentText += ' --spawn "' + (Join-Path $testDirectory 'opposite_arch_probe.exe') + '" "' + $childReport + '"'
}
$testProcess = Start-Process -FilePath $probePath -ArgumentList $argumentText -WorkingDirectory $testDirectory -WindowStyle Hidden -PassThru
$deadline = [DateTime]::UtcNow.AddSeconds(25)
while (!(Test-Path -LiteralPath $reportPath) -and [DateTime]::UtcNow -lt $deadline) {
    Start-Sleep -Milliseconds 100
}
if (!(Test-Path -LiteralPath $reportPath)) { throw "No probe result in $testDirectory" }
if (!$testProcess.WaitForExit(5000)) {
    Stop-Process -Id $testProcess.Id -Force
    throw 'Original probe process did not exit.'
}
$result = Get-Content -LiteralPath $reportPath -Raw -Encoding UTF8 | ConvertFrom-Json
$relaunchedProcess = Get-Process -Id $result.pid -ErrorAction SilentlyContinue
if ($relaunchedProcess -and $result.pid -ne $testProcess.Id -and !$relaunchedProcess.WaitForExit(5000)) {
    Stop-Process -Id $relaunchedProcess.Id -Force
    throw 'Relaunched probe process did not exit.'
}
$result | ConvertTo-Json -Depth 4
$success = $result.loaded -and $result.acp -eq $CodePage -and $result.oemcp -eq $CodePage -and
    $result.userLocale -eq $locale -and $result.systemLocale -eq $locale -and $result.bias -eq $expectedBias -and
    $result.bootstrap -eq '1' -and $result.currentDirectory -eq $testDirectory -and
    $result.arguments.Count -eq $(if ($ChildProbe) { 5 } else { 2 }) -and
    $result.arguments[0] -eq 'locale regression argument' -and $result.arguments[1] -eq $unicodeArgument
if ($ChildProbe) {
    if ($result.spawned -and (Test-Path -LiteralPath $childReport)) {
        $childResult = Get-Content -LiteralPath $childReport -Raw -Encoding UTF8 | ConvertFrom-Json
        $childResult | ConvertTo-Json -Depth 3
        $success = $success -and $childResult.observingInheritedCore -and
            $childResult.acp -eq $CodePage -and $childResult.oemcp -eq $CodePage -and
            $childResult.userLocale -eq $locale -and $childResult.systemLocale -eq $locale -and
            $childResult.bias -eq $expectedBias -and $childResult.bootstrap -eq '1'
    } else {
        $success = $false
    }
}
if ($ExpectFailure) {
    if ($success) { throw 'Baseline unexpectedly passed the locale regression.' }
    Write-Output 'REPRODUCED: the baseline did not apply the requested locale.'
} elseif (!$success) {
    throw 'Locale, code page, time zone, bootstrap marker or original arguments were not preserved.'
} else {
    Write-Output 'PASS: locale, code page, time zone, working directory and original arguments.'
}
