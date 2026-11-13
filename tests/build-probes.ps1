param(
    [ValidateSet('x86', 'x64')][string]$Architecture = 'x86',
    [Parameter(Mandatory = $true)][string]$OutputDirectory,
    [string]$VisualStudioDirectory = ''
)

$ErrorActionPreference = 'Stop'
if (!$VisualStudioDirectory) {
    if ($env:VSINSTALLDIR) {
        $VisualStudioDirectory = $env:VSINSTALLDIR
    } else {
        $installationRoot = Join-Path $env:ProgramFiles 'Microsoft Visual Studio\2022'
        $installation = Get-ChildItem -LiteralPath $installationRoot -Directory |
            Where-Object { Test-Path -LiteralPath (Join-Path $_.FullName 'Common7\Tools\Microsoft.VisualStudio.DevShell.dll') } |
            Select-Object -First 1
        if (!$installation) { throw 'Visual Studio 2022 C++ tools are required.' }
        $VisualStudioDirectory = $installation.FullName
    }
}
Import-Module (Join-Path $VisualStudioDirectory 'Common7\Tools\Microsoft.VisualStudio.DevShell.dll')
Enter-VsDevShell -VsInstallPath $VisualStudioDirectory -SkipAutomaticLocation -DevCmdArguments ('-arch=' + $Architecture + ' -host_arch=x64')
$output = [IO.Path]::GetFullPath($OutputDirectory)
New-Item -ItemType Directory -Path $output -Force | Out-Null
$probes = @(
    @{ Name = 'native_hook_probe'; Source = 'native_hook_probe.cpp'; Define = '' },
    @{ Name = 'font_generic'; Source = 'font_compat_probe.cpp'; Define = '' },
    @{ Name = 'font_bgi'; Source = 'font_compat_probe.cpp'; Define = '/DPROBE_BGI_IDENTITY' },
    @{ Name = 'font_topcat'; Source = 'font_compat_probe.cpp'; Define = '/DPROBE_TOPCAT_IDENTITY' },
    @{ Name = 'font_krkr'; Source = 'font_compat_probe.cpp'; Define = '/DPROBE_KRKR_IDENTITY' }
)
foreach ($probe in $probes) {
    [string[]]$compilerArguments = @('/nologo', '/W4', '/Od', '/EHsc', '/std:c++17', '/utf-8', '/MT')
    if ($probe.Define) { $compilerArguments += [string]$probe.Define }
    $compilerArguments += @(
        (Join-Path $PSScriptRoot $probe.Source),
        ('/Fo' + (Join-Path $output ($probe.Name + '.obj'))),
        ('/Fe' + (Join-Path $output ($probe.Name + '.exe'))),
        '/link', '/SUBSYSTEM:WINDOWS', 'shell32.lib', 'user32.lib', 'gdi32.lib'
    )
    & cl.exe @compilerArguments
    if ($LASTEXITCODE -ne 0) { throw "Compilation failed: $($probe.Name) ($Architecture)." }
}

foreach ($minor in @(3, 4)) {
    & rc.exe /nologo ('/dPROBE_VERSION_MINOR=' + $minor) ('/fo' + (Join-Path $output ('krkr_version_' + $minor + '.res'))) (Join-Path $PSScriptRoot 'krkr_probe_version.rc')
    if ($LASTEXITCODE -ne 0) { throw 'KRKR version resource compilation failed.' }
}
[string[]]$pluginArguments = @('/nologo', '/W4', '/Od', '/MT', '/LD',
    (Join-Path $PSScriptRoot 'krkr_plugin_fixture.cpp'),
    ('/Fo' + (Join-Path $output 'krkr_plugin_fixture.obj')),
    ('/Fe' + (Join-Path $output 'krkr_plugin_fixture.dll')), '/link', '/NOIMPLIB', '/NOEXP')
& cl.exe @pluginArguments
if ($LASTEXITCODE -ne 0) { throw 'KRKR plugin fixture compilation failed.' }
$abiCases = @(
    @{ Name = 'krkr_stdcall'; Define = ''; Minor = 4 },
    @{ Name = 'krkr_old'; Define = ''; Minor = 3 },
    @{ Name = 'krkr_unknown'; Define = ''; Minor = 0 }
)
if ($Architecture -eq 'x86') {
    $abiCases += @{ Name = 'krkr_cdecl'; Define = '/DPROBE_INTERFACE_CDECL'; Minor = 4 }
}
foreach ($probe in $abiCases) {
    [string[]]$compilerArguments = @('/nologo', '/W4', '/Od', '/EHsc', '/std:c++17', '/utf-8', '/MT')
    if ($probe.Define) { $compilerArguments += [string]$probe.Define }
    $compilerArguments += @(
        (Join-Path $PSScriptRoot 'krkr_abi_probe.cpp'),
        ('/Fo' + (Join-Path $output ($probe.Name + '.obj'))),
        ('/Fe' + (Join-Path $output ($probe.Name + '.exe')))
    )
    if ($probe.Minor) { $compilerArguments += (Join-Path $output ('krkr_version_' + $probe.Minor + '.res')) }
    $compilerArguments += @('/link', '/SUBSYSTEM:WINDOWS', 'shell32.lib', 'user32.lib')
    & cl.exe @compilerArguments
    if ($LASTEXITCODE -ne 0) { throw "KRKR ABI probe compilation failed: $($probe.Name)." }
}
