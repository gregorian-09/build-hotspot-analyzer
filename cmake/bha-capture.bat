@echo off
REM BHA Compiler Launcher - Captures per-file timing output (Windows)
REM Used as CMAKE_CXX_COMPILER_LAUNCHER for automatic trace capture
REM
REM Environment variables:
REM   BHA_TRACE_DIR - Directory to store traces (required)
REM   BHA_VERBOSE   - Set to 1 for debug output

setlocal EnableDelayedExpansion

REM Configuration
if "%BHA_TRACE_DIR%"=="" set BHA_TRACE_DIR=bha_traces
if "%BHA_VERBOSE%"=="" set BHA_VERBOSE=0

REM Debug logging function (simulated via goto)
if "%BHA_VERBOSE%"=="1" echo [bha-capture] Starting capture... 1>&2

REM Create trace directory
if not exist "%BHA_TRACE_DIR%" (
    mkdir "%BHA_TRACE_DIR%" 2>nul
    if errorlevel 1 (
        if "%BHA_VERBOSE%"=="1" echo [bha-capture] ERROR: Cannot create trace directory: %BHA_TRACE_DIR% 1>&2
        %*
        exit /b !ERRORLEVEL!
    )
)

REM Find the source file and output file from arguments
set SOURCE_FILE=
set OUTPUT_FILE=
set NEXT_IS_OUTPUT=0

for %%a in (%*) do (
    set "arg=%%~a"

    REM Check if this is an output file marker
    if "!arg!"=="/Fo" set NEXT_IS_OUTPUT=1
    if "!arg!"=="-o" set NEXT_IS_OUTPUT=1

    REM Check if previous arg was output marker
    if "!NEXT_IS_OUTPUT!"=="1" (
        set OUTPUT_FILE=%%a
        set NEXT_IS_OUTPUT=0
    )

    REM Check for source file extensions
    if "!arg:~-4!"==".cpp" set "SOURCE_FILE=!arg!"
    if "!arg:~-3!"==".cc" set "SOURCE_FILE=!arg!"
    if "!arg:~-4!"==".cxx" set "SOURCE_FILE=!arg!"
    if "!arg:~-2!"==".c" set "SOURCE_FILE=!arg!"
    if "!arg:~-3!"==".cu" set "SOURCE_FILE=!arg!"
)

REM If no source file found, run compiler normally
if "%SOURCE_FILE%"=="" (
    if "%BHA_VERBOSE%"=="1" echo [bha-capture] No source file detected, running normally 1>&2
    %*
    exit /b !ERRORLEVEL!
)

if "%BHA_VERBOSE%"=="1" echo [bha-capture] Capturing trace for: %SOURCE_FILE% 1>&2

REM Get basename for the trace filename
for %%f in ("%SOURCE_FILE%") do (
    set BASENAME=%%~nf
)

REM GetTempFileName atomically reserves both stream files, even for parallel builds.
REM The GUID keeps trace names distinct after temporary files are removed.
set "TEMP_STDOUT="
set "TEMP_STDERR="
set "TRACE_ID="
for /f "tokens=1,2,3 delims=|" %%a in ('powershell.exe -NoProfile -NonInteractive -Command "$stdout = [System.IO.Path]::GetTempFileName(); $stderr = [System.IO.Path]::GetTempFileName(); [Console]::WriteLine($stdout + [char]124 + $stderr + [char]124 + [Guid]::NewGuid())"') do (
    set "TEMP_STDOUT=%%a"
    set "TEMP_STDERR=%%b"
    set "TRACE_ID=%%c"
)
if not defined TEMP_STDOUT (
    echo [bha-capture] Failed to reserve a unique stdout file 1>&2
    exit /b 1
)
if not defined TEMP_STDERR (
    del "%TEMP_STDOUT%" 2>nul
    echo [bha-capture] Failed to reserve a unique stderr file 1>&2
    exit /b 1
)
if not defined TRACE_ID (
    del "%TEMP_STDOUT%" 2>nul
    del "%TEMP_STDERR%" 2>nul
    echo [bha-capture] Failed to create a unique trace identifier 1>&2
    exit /b 1
)

set "TRACE_FILE=%BHA_TRACE_DIR%\!BASENAME!_!TRACE_ID!.bha.txt"
if "%BHA_VERBOSE%"=="1" echo [bha-capture] Trace file: %TRACE_FILE% 1>&2

REM MSVC /Bt+ writes timing records to stdout. Capture both streams so the
REM trace contains compiler timings while replaying each stream unchanged.
%* 1>"%TEMP_STDOUT%" 2>"%TEMP_STDERR%"
set EXIT_CODE=!ERRORLEVEL!

REM Check if either captured stream has content.
set "STDOUT_SIZE=0"
set "STDERR_SIZE=0"
if exist "%TEMP_STDOUT%" for %%A in ("%TEMP_STDOUT%") do set STDOUT_SIZE=%%~zA
if exist "%TEMP_STDERR%" for %%A in ("%TEMP_STDERR%") do set STDERR_SIZE=%%~zA

if !STDOUT_SIZE! GTR 0 if !STDERR_SIZE! GTR 0 goto write_trace
if !STDOUT_SIZE! GTR 0 goto write_trace
if !STDERR_SIZE! GTR 0 goto write_trace
goto cleanup

:write_trace
REM Preserve raw producer output. The parser, not the launcher, decides
REM whether the artifact is a supported timing report.
(
    echo # BHA Trace
    echo # Source: %SOURCE_FILE%
    echo # Output: %OUTPUT_FILE%
    echo # Command: %*
    echo # Timestamp: %date% %time%
    echo # Exit code: !EXIT_CODE!
    echo # --- stdout ---
    if !STDOUT_SIZE! GTR 0 type "%TEMP_STDOUT%"
    echo # --- stderr ---
    if !STDERR_SIZE! GTR 0 type "%TEMP_STDERR%"
) > "%TRACE_FILE%"

if "%BHA_VERBOSE%"=="1" (
    for %%F in ("%TRACE_FILE%") do (
        set TRACE_SIZE=%%~zF
        echo [bha-capture] Trace saved: !TRACE_SIZE! bytes 1>&2
    )
)

REM Replay each stream on its original descriptor.
if !STDOUT_SIZE! GTR 0 type "%TEMP_STDOUT%"
if !STDERR_SIZE! GTR 0 type "%TEMP_STDERR%" 1>&2

:cleanup
del "%TEMP_STDOUT%" 2>nul
del "%TEMP_STDERR%" 2>nul

exit /b !EXIT_CODE!
