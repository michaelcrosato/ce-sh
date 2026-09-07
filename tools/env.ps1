# Puts a CMake (>= 4.1, needed for the "Visual Studio 18 2026" generator) and Ninja on PATH for this shell.
# Prefers an existing suitable cmake on PATH, otherwise uses the copies bundled with Visual Studio.
# Dot-source it:   . .\tools\env.ps1

function Get-LcCMakeVersion {
    $cmd = Get-Command cmake -ErrorAction SilentlyContinue
    if (-not $cmd) { return $null }
    $line = (& $cmd.Source --version 2>$null | Select-Object -First 1)
    if ($line -match 'cmake version (\d+)\.(\d+)') { return [version]::new([int]$Matches[1], [int]$Matches[2]) }
    return $null
}

$existing = Get-LcCMakeVersion
if (-not $existing -or $existing -lt [version]::new(4, 1)) {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    if (-not (Test-Path $vswhere)) {
        throw 'vswhere.exe not found. Install Visual Studio 2026 with the "Desktop development with C++" workload.'
    }
    $vsPath = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    if (-not $vsPath) {
        throw 'No Visual Studio installation with the MSVC x64 toolset was found.'
    }
    $cmakeBin = Join-Path $vsPath 'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin'
    $ninjaBin = Join-Path $vsPath 'Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja'
    foreach ($dir in @($cmakeBin, $ninjaBin)) {
        if ((Test-Path $dir) -and (($env:Path -split ';') -notcontains $dir)) {
            $env:Path = "$dir;$env:Path"
        }
    }
    Write-Host "Using Visual Studio-bundled CMake/Ninja from: $vsPath"
}

Write-Host ("cmake: " + (cmake --version | Select-Object -First 1))
$ninja = Get-Command ninja -ErrorAction SilentlyContinue
if ($ninja) { Write-Host ("ninja: " + (& $ninja.Source --version)) }
