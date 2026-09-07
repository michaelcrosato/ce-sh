param(
    [string]$Exe = "C:\dev\ce-sh\build\windows-release\bin\LastCircuit.exe",
    [string]$OutDir = "C:\dev\ce-sh\artifacts\m7\gameplay",
    [string]$Replay = "C:\dev\ce-sh\tests\replay\t16_encounter.json",
    [string]$Frames = "12,300,810,1430,1945,2632,2700,3218,3600,4373,5778,5925"
)
# The gameplay capture of spec §20 M7 ("actual gameplay capture"): the fixed encounter replay played
# in a 1920x1080 window from a 1280x720 internal image, reconstructed, with the interface on, and
# the presented back buffer written at the listed frames (one frame per tick: the controls card at
# the start, the security door, the lamp prompt, the mirror pose, the machine in the mirror, the
# Plant door, the running fan, the fuse pulled in the dark, the wait, the dark return behind the
# machine, the exit powered, the vestibule with the end card). This is the replay as presented,
# not a person's play. A card appears one frame after its condition (ImGui sizes a new window on
# the frame it first appears), so the end card's frame is 5925, five frames after the completion.
$ErrorActionPreference = "Continue"   # The executable logs to stderr; that is not an error.
New-Item -ItemType Directory -Force $OutDir | Out-Null
& $Exe --scene last_circuit --replay $Replay --mode denoised --width 1920 --height 1080 --internal 1280x720 --vsync off `
    --no-audio --capture $OutDir --capture-backbuffer $Frames --frames 5961 --log (Join-Path $OutDir "gameplay.log") *> $null
"exit code $LASTEXITCODE"
Get-ChildItem $OutDir -Filter "backbuffer_frame*.png" | Where-Object { $_.Name -notmatch "_alpha" } | Sort-Object { [int]($_.BaseName -replace '\D', '') } | ForEach-Object { "{0,-32} {1,8} bytes" -f $_.Name, $_.Length }
