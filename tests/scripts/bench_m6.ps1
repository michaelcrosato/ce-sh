param(
    [string]$Exe = "C:\dev\ce-sh\build\windows-release\bin\LastCircuit.exe",
    [string]$OutDir = "C:\dev\ce-sh\artifacts\m6",
    [int]$Warmup = 5
)
# M6 performance protocol (spec §17 "measure a 180-second replay, three times", §21) on the six-room
# demo: Release, vsync off, windowed 1920x1080 presented from a 1280x720 internal image, denoised
# mode, sound on, interface off. The fixed encounter replay (t16_encounter: camera movement, the
# mirror, the moving machine, the carried lamp and its switch, the fuse's source change, seven doors,
# the fan) is 99 s long and loops with a world reset, so each 180-second measurement covers the whole
# encounter once and most of it a second time; three such runs, then the caught variant (a catch and
# a checkpoint restart inside the window) for 180 s. The warm-up (5 s) is the only excluded interval
# and is recorded in the report. The memory figures come from the end of each run.
$ErrorActionPreference = "Continue"   # The executable logs to stderr; that is not an error.
New-Item -ItemType Directory -Force $OutDir | Out-Null
$runs = @(
    @{ name = "encounter_180s_1"; replay = "t16_encounter.json"; seconds = 180 },
    @{ name = "encounter_180s_2"; replay = "t16_encounter.json"; seconds = 180 },
    @{ name = "encounter_180s_3"; replay = "t16_encounter.json"; seconds = 180 },
    @{ name = "caught_180s";      replay = "t16_caught.json";    seconds = 180 }
)
foreach ($r in $runs) {
    $report = Join-Path $OutDir "benchmark_release_$($r.name).json"
    $log = Join-Path $OutDir "benchmark_release_$($r.name).log"
    & $Exe --scene six_room --replay "C:\dev\ce-sh\tests\replay\$($r.replay)" --mode denoised --width 1920 --height 1080 --internal 1280x720 --vsync off `
        --benchmark-seconds $r.seconds --warmup-seconds $Warmup --report $report --log $log *> $null
    if ($LASTEXITCODE -ne 0) { Write-Output "$($r.name): exit code $LASTEXITCODE (see $log)"; continue }
    $j = Get-Content $report -Raw | ConvertFrom-Json
    $g = $j.timings.gpuFrameMs; $c = $j.timings.cpuFrameMs
    "{0,-18} frames {1,6} loops {2,3} | GPU {3:F2} / {4:F2} / {5:F2} / {6:F2} ms | CPU {7:F2} / {8:F2} / {9:F2} / {10:F2} ms | >33.3 ms: {11} | WS {12:F0} MB | VRAM {13:F0} MB" -f `
        $r.name, $g.frames, $j.content.replayLoops, $g.averageMs, $g.p95Ms, $g.p99Ms, $g.maxMs, $c.averageMs, $c.p95Ms, $c.p99Ms, $c.maxMs, $g.framesAbove33ms, `
        ($j.memory.processWorkingSetBytes / 1MB), ($j.memory.videoMemoryUsageBytes / 1MB)
}
