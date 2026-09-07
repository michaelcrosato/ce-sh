param(
    [string]$Exe = "C:\dev\ce-sh\build\windows-release\bin\LastCircuit.exe",
    [string]$OutDir = "C:\dev\ce-sh\artifacts\m5",
    [int]$Seconds = 60,
    [int]$Warmup = 5
)
# M5 performance protocol (spec §17, §21): the M5 content (collision, body, sound on, interface off)
# in Release, vsync off, windowed 1920x1080 presented from a 1280x720 internal image, denoised mode,
# the replay looped with a world reset at its end. The interface is never drawn in a benchmark.
$ErrorActionPreference = "Stop"
New-Item -ItemType Directory -Force $OutDir | Out-Null
$runs = @(
    @{ name = "route_hunt_1"; replay = "t15_route.json" },
    @{ name = "route_hunt_2"; replay = "t15_route.json" },
    @{ name = "catch_restart_hunt"; replay = "t15_catch_restart.json" },
    @{ name = "mirror_patrol"; replay = "t12_mirror_motion.json" }
)
foreach ($r in $runs) {
    $report = Join-Path $OutDir "benchmark_release_$($r.name).json"
    $log = Join-Path $OutDir "benchmark_release_$($r.name).log"
    & $Exe --scene two_room --replay "C:\dev\ce-sh\tests\replay\$($r.replay)" --mode denoised --width 1920 --height 1080 --internal 1280x720 --vsync off `
        --benchmark-seconds $Seconds --warmup-seconds $Warmup --report $report --log $log 2>&1 | Out-Null
    if ($LASTEXITCODE -ne 0) { Write-Error "$($r.name): exit code $LASTEXITCODE"; continue }
    $j = Get-Content $report -Raw | ConvertFrom-Json
    $g = $j.timings.gpuFrameMs; $c = $j.timings.cpuFrameMs
    "{0,-20} frames {1,6} loops {2,3} | GPU {3:F2} / {4:F2} / {5:F2} / {6:F2} ms | CPU {7:F2} / {8:F2} / {9:F2} / {10:F2} ms | >33.3 ms: {11} | WS {12:F0} MB | VRAM {13:F0} MB" -f `
        $r.name, $g.frames, $j.content.replayLoops, $g.averageMs, $g.p95Ms, $g.p99Ms, $g.maxMs, $c.averageMs, $c.p95Ms, $c.p99Ms, $c.maxMs, $g.framesAbove33ms, `
        ($j.memory.processWorkingSetBytes / 1MB), ($j.memory.videoMemoryUsageBytes / 1MB)
}
