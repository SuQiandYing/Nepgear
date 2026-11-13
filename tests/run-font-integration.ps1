param(
    [Parameter(Mandatory = $true)][string]$Dll,
    [Parameter(Mandatory = $true)][string]$Probe,
    [Parameter(Mandatory = $true)][string]$OutputDirectory,
    [ValidateSet('Generic', 'Bgi', 'TopCat', 'Krkr')][string]$Engine = 'Generic',
    [switch]$FontDisabled,
    [switch]$EngineDisabled,
    [switch]$ExpectFailure,
    [switch]$X64
)

$ErrorActionPreference = 'Stop'
$testDirectory = [IO.Path]::GetFullPath($OutputDirectory)
New-Item -ItemType Directory -Path $testDirectory -Force | Out-Null
Copy-Item -LiteralPath $Dll -Destination (Join-Path $testDirectory 'winmm.dll') -Force
Copy-Item -LiteralPath $Probe -Destination (Join-Path $testDirectory 'font_compat_probe.exe') -Force
[IO.File]::WriteAllText((Join-Path $testDirectory 'probe.tft'), 'unchanged probe resource')
$fontEnabled = if ($FontDisabled) { 0 } else { 1 }
$engineEnabled = if ($EngineDisabled) { 0 } else { 1 }
$testIni = @"
[System]
Enable=1
ShowStartupPopup=0
[Font]
Enable=$fontEnabled
FileName=
FaceName=Arial
EnableFaceNameReplace=1
[FontCompatibility]
HookGetTextFace=0
[FontEngines]
EnableBgiHook=$engineEnabled
BgiPatchGdiImports=0
BgiClearGlyphCacheOnSwitch=0
EnableTopCatHook=$engineEnabled
EnableKrkr=$engineEnabled
KrkrDisablePrerenderedFonts=1
KrkrNativeDispatchMode=0
[Window]
Enable=0
[FileRedirect]
Enable=0
[LocaleEmulator]
Enable=0
[Debug]
Enable=0
LogToFile=1
EnableFontTrace=1
FontTraceSampleLimit=8
"@
[IO.File]::WriteAllText((Join-Path $testDirectory 'Nepgear.ini'), $testIni, [Text.Encoding]::Unicode)
$reportPath = Join-Path $testDirectory 'font-result.json'
if (Test-Path -LiteralPath $reportPath) { Remove-Item -LiteralPath $reportPath -Force }
$probePath = Join-Path $testDirectory 'font_compat_probe.exe'
$argumentText = '"' + (Join-Path $testDirectory 'winmm.dll') + '" "' + $reportPath + '"'
$testProcess = Start-Process -FilePath $probePath -ArgumentList $argumentText -WorkingDirectory $testDirectory -WindowStyle Hidden -PassThru
if (!$testProcess.WaitForExit(15000)) {
    Stop-Process -Id $testProcess.Id -Force
    throw 'Font probe did not exit.'
}
if ($testProcess.ExitCode -ne 0 -or !(Test-Path -LiteralPath $reportPath)) {
    throw "Font probe failed with exit code $($testProcess.ExitCode)."
}
$result = Get-Content -LiteralPath $reportPath -Raw -Encoding UTF8 | ConvertFrom-Json
$result | ConvertTo-Json -Depth 3
$success = $result.proxyConcreteExports -and $result.proxyRawCalls -and $result.outlineBounds -and $result.metricsMeasured
if ($FontDisabled) {
    $success = $success -and $result.physicalFace -eq 'Times New Roman' -and $result.metricHeight -gt 560 -and $result.tftVisible -and $result.krkrMemberIntact
} else {
    $success = $success -and $result.physicalFace -eq 'Arial'
    if ($Engine -eq 'Bgi' -and !$EngineDisabled) {
        $success = $success -and $result.attempts -eq 1 -and $result.outlineFace -eq 'Times New Roman' -and
            $result.ansiLogicalFace -and $result.fullNamePreserved -and $result.smallBufferRejected
    }
    if ($Engine -eq 'TopCat' -and !$EngineDisabled -and !$X64) {
        $success = $success -and $result.metricHeight -le 560 -and $result.metricWidth -le 560
    } else {
        $success = $success -and $result.metricHeight -gt 560
    }
    if ($Engine -eq 'Krkr') {
        $success = $success -and $result.krkrMemberIntact -and $result.tftVisible
    }
}
if ($ExpectFailure) {
    if ($success) { throw 'Baseline unexpectedly passed the font regression.' }
    Write-Output "REPRODUCED: $Engine baseline compatibility failure."
} elseif (!$success) {
    throw "$Engine font integration assertion failed."
} else {
    Write-Output "PASS: $Engine font integration and raw winmm exports."
}
