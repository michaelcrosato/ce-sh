# Packages the Release build into dist\LastCircuit-<version>-<commit>-win64\ and a zip beside it
# (spec §20 M7, §21 "a packaging command"). Everything the package needs is copied from the build
# tree and the repository: the executable (static CRT: no redistributable), the shaders and assets
# beside it, the replays of the smoke test and the benchmark, the generated third-party notices,
# the README, the known-issues list, and the two commands. The executable's imports are checked
# against Windows' in-box libraries before the package is accepted.
#   .\tools\package.ps1                          build the Release preset, package, zip
#   .\tools\package.ps1 -NoBuild -NoZip          package what is built (tests)
#   .\tools\package.ps1 -SmokeTest               also run the package's smoke test from its directory
param(
    [ValidateSet('windows-debug', 'windows-release')]
    [string]$Preset = 'windows-release',
    [string]$OutDir = '',
    [switch]$NoBuild,
    [switch]$NoZip,
    [switch]$SmokeTest
)

$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
if ($OutDir -eq '') { $OutDir = Join-Path $root 'dist' }
$buildDir = Join-Path $root "build\$Preset"
$bin = Join-Path $buildDir 'bin'

function Fail($message) { Write-Error $message; exit 1 }

if (-not $NoBuild) {
    . (Join-Path $PSScriptRoot 'env.ps1')
    Push-Location $root
    try {
        cmake --preset $Preset | Out-Null
        if ($LASTEXITCODE -ne 0) { Fail "configure failed (exit $LASTEXITCODE)" }
        cmake --build --preset $Preset --target LastCircuit
        if ($LASTEXITCODE -ne 0) { Fail "build failed (exit $LASTEXITCODE)" }
    } finally { Pop-Location }
}

$exe = Join-Path $bin 'LastCircuit.exe'
if (-not (Test-Path $exe)) { Fail "no executable at $exe (build the $Preset preset first)" }

# Identification from the executable itself: "Last Circuit 0.1.0 build 35c9626f45d6-dirty (Release)".
$versionLine = (& $exe --version | Select-Object -First 1)
if ($versionLine -notmatch '^Last Circuit (\S+) build (\S+) \((\w+)\)$') { Fail "unexpected --version output: $versionLine" }
$version = $Matches[1]; $commit = $Matches[2]; $config = $Matches[3]
$name = "LastCircuit-$version-$commit-win64"
if ($config -ne 'Release') { $name = "$name-$config" }
$stage = Join-Path $OutDir $name
if (Test-Path $stage) { [IO.Directory]::Delete($stage, $true) }
New-Item -ItemType Directory -Force $stage | Out-Null

# The program and what it loads beside itself.
Copy-Item $exe $stage
if (Test-Path (Join-Path $bin 'LastCircuit.pdb')) { Copy-Item (Join-Path $bin 'LastCircuit.pdb') (Join-Path $OutDir "$name.pdb") }  # Symbols beside the package, not inside it.
Copy-Item (Join-Path $bin 'shaders') (Join-Path $stage 'shaders') -Recurse -Filter '*.cso'
Copy-Item (Join-Path $bin 'assets') (Join-Path $stage 'assets') -Recurse
New-Item -ItemType Directory -Force (Join-Path $stage 'replays') | Out-Null
foreach ($replay in 't16_encounter.json', 't16_caught.json') {
    Copy-Item (Join-Path $root "tests\replay\$replay") (Join-Path $stage 'replays')
}
Copy-Item (Join-Path $root 'docs\KNOWN_ISSUES.md') $stage
Copy-Item (Join-Path $PSScriptRoot 'package\smoke_test.cmd') $stage
Copy-Item (Join-Path $PSScriptRoot 'package\benchmark.cmd') $stage

# The notices: the required NVIDIA attribution, then every shipped third-party license text verbatim.
$notices = @()
$notices += "THIRD-PARTY NOTICES FOR LAST CIRCUIT $version (build $commit)"
$notices += ''
$notices += 'This software contains source code provided by NVIDIA Corporation.'
$notices += ''
$notices += 'The following third-party components are compiled into LastCircuit.exe. Their license texts follow verbatim.'
$notices += ''
$components = @(
    @{ name = 'NVIDIA Real-Time Denoisers (NRD) v4.17.3'; file = 'external\NRD\LICENSE.txt'; note = 'NVIDIA RTX SDKs License. Used for image reconstruction (REBLUR).' },
    @{ name = 'NVIDIA MathLib v11'; file = 'external\MathLib\LICENSE.txt'; note = 'MIT License. Header-only math used by NRD.' },
    @{ name = 'Dear ImGui v1.92.9b'; file = 'external\imgui\LICENSE.txt'; note = 'MIT License. The on-screen interface.' },
    @{ name = 'miniaudio 0.11.25'; file = 'external\miniaudio\LICENSE'; note = 'Public domain (Unlicense) or MIT No Attribution, at the user''s choice; MIT No Attribution chosen. The audio device layer.' }
)
foreach ($c in $components) {
    $path = Join-Path $root $c.file
    if (-not (Test-Path $path)) { Fail "license text missing: $path (run git submodule update --init --recursive)" }
    $notices += '=' * 78
    $notices += $c.name
    $notices += $c.note
    $notices += '=' * 78
    $notices += (Get-Content $path -Raw).TrimEnd()
    $notices += ''
}
Set-Content -Path (Join-Path $stage 'NOTICES.txt') -Value ($notices -join "`r`n") -Encoding UTF8

# The README from its template.
$readme = Get-Content (Join-Path $PSScriptRoot 'package\README.txt') -Raw
$readme = $readme.Replace('{BUILD_ID}', $versionLine).Replace('{DATE}', (Get-Date -Format 'yyyy-MM-dd HH:mm'))
Set-Content -Path (Join-Path $stage 'README.txt') -Value $readme -Encoding UTF8

# The executable's imports must all be Windows in-box libraries (spec T18: no developer SDK, no
# redistributable). Parsed from the PE import and delay-import directories.
function Get-PeImports([string]$path) {
    $bytes = [IO.File]::ReadAllBytes($path)
    $peOffset = [BitConverter]::ToInt32($bytes, 0x3C)
    if ([BitConverter]::ToUInt32($bytes, $peOffset) -ne 0x00004550) { throw "not a PE file: $path" }
    $coff = $peOffset + 4
    $sections = [BitConverter]::ToUInt16($bytes, $coff + 2)
    $optionalSize = [BitConverter]::ToUInt16($bytes, $coff + 16)
    $optional = $coff + 20
    $magic = [BitConverter]::ToUInt16($bytes, $optional)
    if ($magic -ne 0x20B) { throw "not a 64-bit executable: $path" }
    $dataDirectories = $optional + 112
    $sectionTable = $optional + $optionalSize
    $sectionList = @()
    for ($i = 0; $i -lt $sections; $i++) {
        $s = $sectionTable + $i * 40
        $sectionList += @{ va = [BitConverter]::ToUInt32($bytes, $s + 12); size = [BitConverter]::ToUInt32($bytes, $s + 8); raw = [BitConverter]::ToUInt32($bytes, $s + 20) }
    }
    function RvaToOffset([uint32]$rva) {
        foreach ($s in $sectionList) { if ($rva -ge $s.va -and $rva -lt $s.va + [Math]::Max($s.size, 1)) { return $s.raw + ($rva - $s.va) } }
        throw "RVA 0x$($rva.ToString('X')) is in no section"
    }
    function ReadName([uint32]$rva) {
        $o = RvaToOffset $rva
        $end = $o
        while ($bytes[$end] -ne 0) { $end++ }
        return [Text.Encoding]::ASCII.GetString($bytes, $o, $end - $o)
    }
    $names = @()
    $importRva = [BitConverter]::ToUInt32($bytes, $dataDirectories + 1 * 8)
    if ($importRva -ne 0) {
        $d = RvaToOffset $importRva
        while ($true) {
            $nameRva = [BitConverter]::ToUInt32($bytes, $d + 12)
            if ($nameRva -eq 0) { break }
            $names += ReadName $nameRva
            $d += 20
        }
    }
    $delayRva = [BitConverter]::ToUInt32($bytes, $dataDirectories + 13 * 8)
    if ($delayRva -ne 0) {
        $d = RvaToOffset $delayRva
        while ($true) {
            $nameRva = [BitConverter]::ToUInt32($bytes, $d + 4)
            if ($nameRva -eq 0) { break }
            $names += (ReadName $nameRva) + ' (delay)'
            $d += 32
        }
    }
    return $names
}
$inBox = @('kernel32.dll', 'user32.dll', 'gdi32.dll', 'shell32.dll', 'ole32.dll', 'oleaut32.dll', 'advapi32.dll', 'imm32.dll', 'd3d12.dll', 'dxgi.dll', 'd3dcompiler_47.dll', 'version.dll', 'ws2_32.dll', 'shlwapi.dll', 'uxtheme.dll', 'dwmapi.dll', 'setupapi.dll', 'winmm.dll', 'avrt.dll')
$imports = Get-PeImports (Join-Path $stage 'LastCircuit.exe')
$foreign = @($imports | Where-Object { $inBox -notcontains ($_ -replace ' \(delay\)$', '').ToLowerInvariant() })
Write-Output ("imports: " + ($imports -join ', '))
if ($foreign.Count -gt 0) { Fail ("the executable imports libraries that are not part of Windows: " + ($foreign -join ', ')) }

# The manifest: every file with its size and SHA-256 (.NET directly: CTest's environment does not
# load the PowerShell utility modules that provide Get-FileHash and Compress-Archive).
function Get-Sha256([string]$path) {
    $sha = [System.Security.Cryptography.SHA256]::Create()
    $stream = [IO.File]::OpenRead($path)
    try { return ([BitConverter]::ToString($sha.ComputeHash($stream)) -replace '-', '').ToLowerInvariant() } finally { $stream.Dispose(); $sha.Dispose() }
}
$manifest = @("Last Circuit package $name ($versionLine), created $(Get-Date -Format 'yyyy-MM-dd HH:mm')", '')
$total = 0
Get-ChildItem $stage -Recurse -File | Sort-Object FullName | ForEach-Object {
    $relative = $_.FullName.Substring($stage.Length + 1)
    if ($relative -eq 'manifest.txt') { return }
    $hash = Get-Sha256 $_.FullName
    $manifest += ('{0,12}  {1}  {2}' -f $_.Length, $hash, $relative)
    $total += $_.Length
}
$manifest += ''
$manifest += ('{0,12}  bytes in total' -f $total)
Set-Content -Path (Join-Path $stage 'manifest.txt') -Value ($manifest -join "`r`n") -Encoding UTF8
Write-Output ("package: {0} ({1:F1} MB, {2} files)" -f $stage, ($total / 1MB), (Get-ChildItem $stage -Recurse -File).Count)

if ($SmokeTest) {
    # The command changes to its own directory (cd /d "%~dp0"); the full path avoids the child's
    # working directory, which Push-Location does not set for child processes in Windows PowerShell.
    & cmd.exe /c "`"$stage\smoke_test.cmd`" /quiet"
    $code = $LASTEXITCODE
    if ($code -eq 3) { Write-Output 'smoke test: NOT RUN (no supported adapter)'; exit 3 }
    if ($code -ne 0) { Fail "the package's smoke test failed (exit $code); see $stage\smoke\smoke.log" }
    Write-Output 'smoke test: PASSED'
}

if (-not $NoZip) {
    $zip = Join-Path $OutDir "$name.zip"
    if (Test-Path $zip) { [IO.File]::Delete($zip) }
    Add-Type -AssemblyName System.IO.Compression.FileSystem
    [IO.Compression.ZipFile]::CreateFromDirectory($stage, $zip, [IO.Compression.CompressionLevel]::Optimal, $true)  # The package directory as the zip's root folder.
    $sha = Get-Sha256 $zip
    Set-Content -Path "$zip.sha256" -Value "$sha  $name.zip" -Encoding ASCII
    Write-Output ("zip: {0} ({1:F1} MB) sha256 {2}" -f $zip, ((Get-Item $zip).Length / 1MB), $sha)
}
