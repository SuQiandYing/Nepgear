param(
    [Parameter(Mandatory = $true)][string]$Dll,
    [Parameter(Mandatory = $true)][string]$Probe,
    [Parameter(Mandatory = $true)][string]$Plugin,
    [Parameter(Mandatory = $true)][string]$OutputDirectory,
    [ValidateSet(0, 1, 2)][int]$Mode = 0,
    [switch]$ExpectNative
)

$ErrorActionPreference = 'Stop'
$testDirectory = [IO.Path]::GetFullPath($OutputDirectory)
New-Item -ItemType Directory -Path $testDirectory -Force | Out-Null
Copy-Item -LiteralPath $Dll -Destination (Join-Path $testDirectory 'winmm.dll') -Force
Copy-Item -LiteralPath $Probe -Destination (Join-Path $testDirectory 'krkr_abi_probe.exe') -Force
Copy-Item -LiteralPath $Plugin -Destination (Join-Path $testDirectory 'krkr_plugin_fixture.dll') -Force
$testIni = @"
[System]
Enable=1
ShowStartupPopup=0
[Font]
Enable=1
FileName=
FaceName=Arial
EnableFaceNameReplace=1
[FontEngines]
EnableKrkr=1
KrkrDisablePrerenderedFonts=1
KrkrNativeDispatchMode=$Mode
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
$reportPath = Join-Path $testDirectory 'krkr-result.json'
if (Test-Path -LiteralPath $reportPath) { Remove-Item -LiteralPath $reportPath -Force }
$argumentText = '"' + (Join-Path $testDirectory 'winmm.dll') + '" "' + (Join-Path $testDirectory 'krkr_plugin_fixture.dll') + '" "' + $reportPath + '"'
$testProcess = Start-Process -FilePath (Join-Path $testDirectory 'krkr_abi_probe.exe') -ArgumentList $argumentText -WorkingDirectory $testDirectory -WindowStyle Hidden -PassThru
if (!$testProcess.WaitForExit(15000)) {
    Stop-Process -Id $testProcess.Id -Force
    throw 'KRKR ABI probe timed out.'
}
if ($testProcess.ExitCode -ne 0 -or !(Test-Path -LiteralPath $reportPath)) {
    throw "KRKR ABI probe exited with $($testProcess.ExitCode)."
}
$result = Get-Content -LiteralPath $reportPath -Raw -Encoding UTF8 | ConvertFrom-Json
$result | ConvertTo-Json -Depth 3
$success = $result.stackBalanced -and $result.exportArgumentsValid -and $result.unrelatedPreserved -and $result.memberIntact
if ($ExpectNative) {
    $success = $success -and !$result.wrapped -and $result.mapCalls -eq 1 -and $result.unmapCalls -eq 0 -and $result.status -eq 22
} else {
    $success = $success -and $result.wrapped -and $result.mapCalls -eq 0 -and $result.unmapCalls -eq 1 -and
        $result.status -eq 77 -and $result.result -eq 77 -and $result.forwardedArguments
}
if (!$success) { throw 'KRKR ABI dispatch, calling convention or fallback assertion failed.' }
Write-Output 'PASS: KRKR ABI dispatch, stack balance and unrelated calls.'
