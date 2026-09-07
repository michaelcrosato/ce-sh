@echo off
setlocal
rem Last Circuit benchmark (spec section 17 protocol): the fixed encounter replay for 180 measured
rem seconds after a 5 s warm-up, windowed 1920x1080 presented from a 1280x720 internal image,
rem reconstructed mode, vsync off, sound on, no interface. Writes benchmark\benchmark_<time>.json
rem (build and content hashes, device, sizes, settings, timing distributions, memory) and its log.
rem Usage: benchmark.cmd [/quiet]
cd /d "%~dp0"
set "EXE=%~dp0LastCircuit.exe"
if not exist benchmark mkdir benchmark
for /f %%i in ('powershell -NoProfile -Command "Get-Date -Format yyyyMMdd-HHmmss"') do set "STAMP=%%i"
set "REPORT=benchmark\benchmark_%STAMP%.json"
echo Last Circuit benchmark: 5 s warm-up, then 180 s measured. Leave the window alone until it closes.
"%EXE%" --scene last_circuit --replay replays\t16_encounter.json --mode denoised --width 1920 --height 1080 --internal 1280x720 --vsync off --benchmark-seconds 180 --warmup-seconds 5 --report "%REPORT%" --log "benchmark\benchmark_%STAMP%.log" --no-dialog
set CODE=%errorlevel%
if not "%CODE%"=="0" (
    echo BENCHMARK FAILED ^(exit code %CODE%^): see benchmark\benchmark_%STAMP%.log
    if /i not "%~1"=="/quiet" pause
    exit /b %CODE%
)
powershell -NoProfile -Command "$j = Get-Content '%REPORT%' -Raw | ConvertFrom-Json; $g = $j.timings.gpuFrameMs; $c = $j.timings.cpuFrameMs; Write-Output ('GPU frame ms: average {0:F2}, median {1:F2}, p95 {2:F2}, p99 {3:F2}, max {4:F2}; frames above 33.3 ms: {5}' -f $g.averageMs, $g.medianMs, $g.p95Ms, $g.p99Ms, $g.maxMs, $g.framesAbove33ms); Write-Output ('CPU frame ms: average {0:F2}, p95 {1:F2}, p99 {2:F2}' -f $c.averageMs, $c.p95Ms, $c.p99Ms); Write-Output ('Frames measured: {0}; replay loops: {1}; working set {2:F0} MB; video memory {3:F0} MB' -f $g.frames, $j.content.replayLoops, ($j.memory.processWorkingSetBytes/1MB), ($j.memory.videoMemoryUsageBytes/1MB)); Write-Output ('Pass criterion (p95 <= 16.67 ms and p99 <= 22 ms): ' + $(if ($g.p95Ms -le 16.67 -and $g.p99Ms -le 22) { 'met' } else { 'NOT met' }))"
echo Report: %REPORT%
if /i not "%~1"=="/quiet" pause
exit /b 0
