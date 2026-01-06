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
    set arg=%%a

    REM Check if this is an output file marker
    if "!arg!"=="/Fo" set NEXT_IS_OUTPUT=1
    if "!arg!"=="-o" set NEXT_IS_OUTPUT=1

    REM Check if previous arg was output marker
    if "!NEXT_IS_OUTPUT!"=="1" (
        set OUTPUT_FILE=%%a
        set NEXT_IS_OUTPUT=0
    )

    REM Check for source file extensions
    if "!arg:~-4!"==".cpp" set SOURCE_FILE=%%a
    if "!arg:~-3!"==".cc" set SOURCE_FILE=%%a
    if "!arg:~-4!"==".cxx" set SOURCE_FILE=%%a
    if "!arg:~-2!"==".c" set SOURCE_FILE=%%a
    if "!arg:~-3!"==".cu" set SOURCE_FILE=%%a
)

REM If no source file found, run compiler normally
if "%SOURCE_FILE%"=="" (
    if "%BHA_VERBOSE%"=="1" echo [bha-capture] No source file detected, running normally 1>&2
    %*
    exit /b !ERRORLEVEL!
)

if "%BHA_VERBOSE%"=="1" echo [bha-capture] Capturing trace for: %SOURCE_FILE% 1>&2

REM Create unique trace filename using timestamp to avoid collisions
REM Get timestamp components
for /f "tokens=1-4 delims=:.," %%a in ("%time%") do (
    set HOUR=%%a
    set MIN=%%b
    set SEC=%%c
    set MSEC=%%d
)
REM Pad hour with zero if needed
if "%HOUR:~0,1%"==" " set HOUR=0%HOUR:~1,1%

REM Get basename and create unique filename
for %%f in ("%SOURCE_FILE%") do (
    set BASENAME=%%~nf
    set EXTENSION=%%~xf
)

REM Remove extension dot from EXTENSION if present
if not "!EXTENSION!"=="" set EXTENSION=!EXTENSION:~1!

REM Create timestamp-based unique suffix
set TIMESTAMP=%HOUR%%MIN%%SEC%%MSEC%
set TRACE_FILE=%BHA_TRACE_DIR%\!BASENAME!_!TIMESTAMP!.bha.txt

if "%BHA_VERBOSE%"=="1" echo [bha-capture] Trace file: %TRACE_FILE% 1>&2

REM Create temporary file for stderr capture
set TEMP_STDERR=%TEMP%\bha_stderr_%RANDOM%.tmp

REM Run compiler and capture stderr to temporary file
%* 2>"%TEMP_STDERR%"
set EXIT_CODE=!ERRORLEVEL!

REM Check if temp file has content
if exist "%TEMP_STDERR%" (
    for %%A in ("%TEMP_STDERR%") do set TEMP_SIZE=%%~zA

    if !TEMP_SIZE! GTR 0 (
        REM Check if stderr contains timing information
        REM MSVC timing markers: "time(", "c1xx.dll", "c2.dll", or just check for presence
        REM GCC/Clang markers: "Execution times", "TOTAL", "time in"

        findstr /C:"time(" /C:"c1xx.dll" /C:"c2.dll" /C:"Execution times" /C:"TOTAL" /C:"time in" "%TEMP_STDERR%" >nul 2>&1

        if !ERRORLEVEL! EQU 0 (
            REM Timing data found, create trace file
            (
                echo # BHA Trace
                echo # Source: %SOURCE_FILE%
                echo # Output: %OUTPUT_FILE%
                echo # Command: %*
                echo # Timestamp: %date% %time%
                echo # Exit code: !EXIT_CODE!
                echo # ---
                echo.
                type "%TEMP_STDERR%"
            ) > "%TRACE_FILE%"

            if "%BHA_VERBOSE%"=="1" (
                for %%F in ("%TRACE_FILE%") do (
                    set TRACE_SIZE=%%~zF
                    echo [bha-capture] Trace saved: !TRACE_SIZE! bytes 1>&2
                )
            )
        ) else (
            if "%BHA_VERBOSE%"=="1" echo [bha-capture] No timing data found in stderr 1>&2
        )

        REM Always output stderr to preserve error messages
        type "%TEMP_STDERR%" 1>&2
    )

    REM Clean up temp file
    del "%TEMP_STDERR%" 2>nul
)

exit /b !EXIT_CODE!