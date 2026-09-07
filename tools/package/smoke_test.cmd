@echo off
setlocal
rem Last Circuit smoke test (spec section 21): four short runs of the packaged executable from this
rem directory. Writes smoke\smoke.log, smoke\environment.json, and smoke\benchmark_smoke.json.
rem Usage: smoke_test.cmd            (pauses at the end when double-clicked)
rem        smoke_test.cmd /quiet     (no pause; the exit code is 0 on success)
cd /d "%~dp0"
set "EXE=%~dp0LastCircuit.exe"
set "OUT=smoke"
if not exist "%OUT%" mkdir "%OUT%"
set FAIL=0
echo Last Circuit smoke test (results in %OUT%\)
echo.

echo [1/4] Graphics adapters and the environment report
"%EXE%" --list-adapters --env-report "%OUT%\environment.json" --no-dialog --log "%OUT%\smoke.log"
call :result %errorlevel%

echo [2/4] Hardware ray tracing: geometry and identifiers on the test scene (T01)
"%EXE%" --scene rt_boxes --mode diag --validate --headless --frames 2 --no-dialog --log "%OUT%\smoke.log"
call :result %errorlevel%

echo [3/4] The demo's encounter: 600 reconstructed frames with the layout probe and the world-state checks
"%EXE%" --scene last_circuit --replay replays\t16_encounter.json --mode denoised --frames 600 --validate --headless --no-dialog --log "%OUT%\smoke.log"
call :result %errorlevel%

echo [4/4] A ten-second benchmark report (the full protocol is benchmark.cmd)
"%EXE%" --scene last_circuit --replay replays\t16_encounter.json --mode denoised --benchmark-seconds 10 --warmup-seconds 2 --report "%OUT%\benchmark_smoke.json" --headless --no-dialog --log "%OUT%\smoke.log"
call :result %errorlevel%

echo.
if %FAIL%==0 (
    echo SMOKE TEST PASSED
) else if %FAIL%==3 (
    echo SMOKE TEST NOT RUN: this machine has no supported graphics adapter ^(see %OUT%\environment.json^)
) else (
    echo SMOKE TEST FAILED: see %OUT%\smoke.log
)
if /i not "%~1"=="/quiet" pause
exit /b %FAIL%

:result
if "%~1"=="0" (
    echo     PASS
) else if "%~1"=="3" (
    echo     UNSUPPORTED: no graphics adapter with DirectX Raytracing Tier 1.1 and Shader Model 6.5 was found ^(exit code 3^)
    if "%FAIL%"=="0" set FAIL=3
) else (
    echo     FAIL ^(exit code %~1^)
    set FAIL=1
)
exit /b 0
