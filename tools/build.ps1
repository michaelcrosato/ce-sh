# Configure, build, and test one preset from any PowerShell prompt.
#   .\tools\build.ps1                       # windows-debug, with tests
#   .\tools\build.ps1 -Preset windows-release -NoTest
#   .\tools\build.ps1 -Fresh                # wipe the CMake cache first
param(
    [ValidateSet('windows-debug', 'windows-release')]
    [string]$Preset = 'windows-debug',
    [switch]$NoTest,
    [switch]$Fresh
)

$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'env.ps1')

Push-Location (Split-Path $PSScriptRoot -Parent)
try {
    if ($Fresh) {
        cmake --preset $Preset --fresh
    } else {
        cmake --preset $Preset
    }
    if ($LASTEXITCODE -ne 0) { throw "configure failed (exit $LASTEXITCODE)" }

    cmake --build --preset $Preset
    if ($LASTEXITCODE -ne 0) { throw "build failed (exit $LASTEXITCODE)" }

    if (-not $NoTest) {
        ctest --preset $Preset --output-on-failure
        if ($LASTEXITCODE -ne 0) { throw "tests failed (exit $LASTEXITCODE)" }
    }
} finally {
    Pop-Location
}
