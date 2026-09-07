# Runs LastCircuit several times on one scene with --stats and compares the patch means of every
# run against the first (reference) run: |mean_a - mean_b| <= max(RelTol * |mean_ref|, 3 * sqrt(se_a^2 + se_b^2))
# per channel and patch (spec §19: patch-mean agreement within the larger of a relative tolerance
# and three estimated standard errors).
#
#   compare_runs.ps1 -Exe <LastCircuit.exe> -Scene t08_box -OutDir <dir> -Runs "mis:256,light:256,bsdf:2048" [-RelTol 0.03] [-MaxHits 4] [-Informational]
#
# Each run entry is "<strategy>:<spp>[:<max-hits>]". Exit codes: 0 agree, 1 disagree, 3 the executable
# reported unsupported hardware (CTest treats 3 as skipped). -Informational reports without failing.
param(
    [Parameter(Mandatory = $true)][string]$Exe,
    [Parameter(Mandatory = $true)][string]$Scene,
    [Parameter(Mandatory = $true)][string]$OutDir,
    [Parameter(Mandatory = $true)][string]$Runs,
    [double]$RelTol = 0.03,
    [int]$MaxHits = 4,
    [switch]$Informational
)

$ErrorActionPreference = 'Stop'
New-Item -ItemType Directory -Force -Path $OutDir | Out-Null

$results = @()
foreach ($entry in ($Runs -split ',')) {
    $parts = $entry.Trim() -split ':'
    $strategy = $parts[0]
    $spp = [int]$parts[1]
    $hits = $MaxHits
    if ($parts.Count -ge 3) { $hits = [int]$parts[2] }
    $stats = Join-Path $OutDir ("{0}_{1}_spp{2}_hits{3}.json" -f $Scene, $strategy, $spp, $hits)
    Write-Host ("running {0} strategy={1} spp={2} max-hits={3}" -f $Scene, $strategy, $spp, $hits)
    # The executable logs to stderr; Windows PowerShell would treat that as a terminating error under 'Stop'.
    $previousPreference = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    & $Exe --scene $Scene --mode reference --strategy $strategy --spp $spp --max-hits $hits --headless --stats $stats 2>&1 | Out-Null
    $code = $LASTEXITCODE
    $ErrorActionPreference = $previousPreference
    if ($code -eq 3) { Write-Host 'unsupported hardware: skipping'; exit 3 }
    if ($code -ne 0) { Write-Host ("run failed with exit code {0}" -f $code); exit 1 }
    $results += [pscustomobject]@{ Label = ("{0}/spp{1}/hits{2}" -f $strategy, $spp, $hits); Data = (Get-Content $stats -Raw | ConvertFrom-Json) }
}

$reference = $results[0]
$failures = 0
$channels = @('R', 'G', 'B')
foreach ($patchProperty in $reference.Data.patches.PSObject.Properties) {
    $name = $patchProperty.Name
    $ref = $patchProperty.Value
    if (-not $ref.visible) { Write-Host ("patch {0}: not visible in the reference run" -f $name); continue }
    for ($i = 1; $i -lt $results.Count; $i++) {
        $other = $results[$i].Data.patches.$name
        if (-not $other.visible) { Write-Host ("patch {0}: not visible in {1}" -f $name, $results[$i].Label); $failures++; continue }
        for ($c = 0; $c -lt 3; $c++) {
            $a = [double]$ref.mean[$c]
            $b = [double]$other.mean[$c]
            $sea = [double]$ref.standardError[$c]
            $seb = [double]$other.standardError[$c]
            $tolerance = [Math]::Max($RelTol * [Math]::Abs($a), 3.0 * [Math]::Sqrt($sea * $sea + $seb * $seb))
            $diff = [Math]::Abs($a - $b)
            $ok = $diff -le $tolerance
            $relative = if ($a -ne 0) { ($b - $a) / $a * 100.0 } else { 0.0 }
            $verdict = if ($ok) { 'AGREE' } else { 'DIFFER' }
            Write-Host ("patch {0} {1}: {2} {3:F5} vs {4} {5:F5} ({6:+0.00;-0.00}%, tolerance {7:F5}) -> {8}" -f $name, $channels[$c], $reference.Label, $a, $results[$i].Label, $b, $relative, $tolerance, $verdict)
            if (-not $ok) { $failures++ }
        }
    }
}

if ($failures -eq 0) {
    Write-Host 'COMPARISON PASSED'
    exit 0
}
if ($Informational) {
    Write-Host ("COMPARISON REPORTED {0} difference(s) (informational run, not a gate)" -f $failures)
    exit 0
}
Write-Host ("COMPARISON FAILED: {0} difference(s)" -f $failures)
exit 1
