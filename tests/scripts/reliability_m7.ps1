param(
    [string]$Exe = "C:\dev\ce-sh\build\windows-release\bin\LastCircuit.exe",
    [string]$OutDir = "C:\dev\ce-sh\artifacts\m7\reliability",
    [int]$LoopSeconds = 1800,
    [int]$ResizeSeconds = 300,
    [switch]$SkipLoop,
    [switch]$SkipResize
)
# T17 (spec §19 reliability target): "run a 30-minute loop containing gameplay, restart, source
# switching, and camera motion. Run a separate sequence with repeated resize and focus changes.
# Report memory trends and failures."
#  1. The loop: the caught replay (a catch and a checkpoint restart per pass, the fuse's source
#     change, seven doors, the mirror, the moving machine, the camera walk) under the benchmark
#     loop for 30 measured minutes in a 1920x1080 window from a 1280x720 internal image, denoised,
#     vsync off, sound on. The program logs a memory line every minute; the report holds the
#     frame-time distributions and the final and peak memory.
#  2. The resize and focus sequence: the encounter replay looped for five minutes while this script
#     resizes the window every five seconds through four sizes and minimises and restores it every
#     twenty seconds (focus lost and regained). The program must survive with exit code 0, no
#     error in its log, and its memory line steady.
$ErrorActionPreference = "Continue"   # The executable logs to stderr; that is not an error.
New-Item -ItemType Directory -Force $OutDir | Out-Null
$replays = "C:\dev\ce-sh\tests\replay"

function Summarise([string]$report, [string]$log, [string]$title) {
    if (-not (Test-Path $report)) { Write-Output "$title : no report"; return }
    $j = Get-Content $report -Raw | ConvertFrom-Json
    $g = $j.timings.gpuFrameMs; $c = $j.timings.cpuFrameMs
    "{0}: {1} frames, {2} loops, {3:F0} s | GPU {4:F2} / p95 {5:F2} / p99 {6:F2} / max {7:F2} ms | CPU p95 {8:F2} ms | >33.3 ms {9} | WS {10:F0} MB (peak {11:F0}) | VRAM {12:F0} MB" -f `
        $title, $g.frames, $j.content.replayLoops, $j.durations.measuredSeconds, $g.averageMs, $g.p95Ms, $g.p99Ms, $g.maxMs, $c.p95Ms, $g.framesAbove33ms, `
        ($j.memory.processWorkingSetBytes / 1MB), ($j.memory.processPeakWorkingSetBytes / 1MB), ($j.memory.videoMemoryUsageBytes / 1MB)
    Write-Output "memory trend (one line a minute):"
    Select-String -Path $log -Pattern "memory at" | ForEach-Object { "  " + ($_.Line -replace '^\[\s*[\d.]+s\] \[INFO \] ', '') }
    $errors = Select-String -Path $log -Pattern "\[ERROR\]" | Measure-Object | Select-Object -ExpandProperty Count
    Write-Output "errors in the log: $errors"
}

if (-not $SkipLoop) {
    $report = Join-Path $OutDir "loop30.json"
    $log = Join-Path $OutDir "loop30.log"
    if (Test-Path $log) { [IO.File]::Delete($log) }
    & $Exe --scene last_circuit --replay "$replays\t16_caught.json" --mode denoised --width 1920 --height 1080 --internal 1280x720 --vsync off `
        --benchmark-seconds $LoopSeconds --warmup-seconds 5 --report $report --log $log --no-dialog *> $null
    Write-Output "loop: exit code $LASTEXITCODE"
    Summarise $report $log "30-minute loop (caught replay)"
}

if (-not $SkipResize) {
    Add-Type @"
using System;
using System.Runtime.InteropServices;
using System.Text;
public static class WinResize {
    [DllImport("user32.dll")] public static extern bool EnumWindows(EnumWindowsProc cb, IntPtr lParam);
    public delegate bool EnumWindowsProc(IntPtr hWnd, IntPtr lParam);
    [DllImport("user32.dll", CharSet = CharSet.Unicode)] public static extern int GetWindowText(IntPtr hWnd, StringBuilder text, int count);
    [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr hWnd);
    [DllImport("user32.dll")] public static extern bool SetWindowPos(IntPtr hWnd, IntPtr after, int x, int y, int cx, int cy, uint flags);
    [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr hWnd, int cmd);
    public static IntPtr Find(string prefix) {
        IntPtr found = IntPtr.Zero;
        EnumWindows((h, l) => {
            if (!IsWindowVisible(h)) return true;
            var sb = new StringBuilder(512);
            GetWindowText(h, sb, sb.Capacity);
            if (sb.ToString().StartsWith(prefix)) { found = h; return false; }
            return true;
        }, IntPtr.Zero);
        return found;
    }
}
"@
    $report = Join-Path $OutDir "resize_focus.json"
    $log = Join-Path $OutDir "resize_focus.log"
    if (Test-Path $log) { [IO.File]::Delete($log) }
    $p = Start-Process -FilePath $Exe -ArgumentList '--scene','last_circuit','--replay',"$replays\t16_encounter.json",'--mode','denoised','--width','1280','--height','720','--vsync','off', `
        '--benchmark-seconds',"$ResizeSeconds",'--warmup-seconds','5','--report',$report,'--log',$log,'--no-dialog' -PassThru
    Start-Sleep -Seconds 8
    $sizes = @(@(1600, 900), @(800, 600), @(1920, 1080), @(1280, 720))
    $i = 0; $resizes = 0; $minimises = 0
    $SWP_NOMOVE = 0x0002; $SWP_NOZORDER = 0x0004
    while (-not $p.HasExited) {
        $h = [WinResize]::Find("Last Circuit")
        if ($h -ne [IntPtr]::Zero) {
            $s = $sizes[$i % $sizes.Count]
            [void][WinResize]::SetWindowPos($h, [IntPtr]0, 0, 0, $s[0], $s[1], $SWP_NOMOVE -bor $SWP_NOZORDER)
            $resizes++
            if ($i % 4 -eq 3) {
                [void][WinResize]::ShowWindow($h, 6)   # SW_MINIMIZE: focus lost, the loop idles.
                Start-Sleep -Seconds 2
                [void][WinResize]::ShowWindow($h, 9)   # SW_RESTORE: focus regained.
                $minimises++
            }
        }
        $i++
        Start-Sleep -Seconds 5
    }
    Write-Output "resize/focus: exit code $($p.ExitCode); $resizes resizes, $minimises minimise/restore cycles"
    Summarise $report $log "resize and focus sequence (encounter replay)"
    Write-Output ("resize events in the log: " + (Select-String -Path $log -Pattern "resiz" | Measure-Object).Count)
}
