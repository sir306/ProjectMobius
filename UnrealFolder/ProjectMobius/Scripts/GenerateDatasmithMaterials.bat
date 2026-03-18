@echo off
setlocal enabledelayedexpansion

REM ============================================================================
REM GenerateDatasmithMaterials.bat
REM Generates RuntimeDatasmithOverrides materials from engine DatasmithRuntime
REM sources by running the GenerateDatasmithMaterials commandlet.
REM ============================================================================

echo ============================================================
echo  Generate Datasmith Override Materials
echo ============================================================

REM ── Locate UE 5.5 install ────────────────────────────────────────────────────

set "UE_EDITOR="

REM Try environment variable first
if defined UE_5_5 (
    set "UE_EDITOR=%UE_5_5%\Engine\Binaries\Win64\UnrealEditor-Cmd.exe"
    if exist "!UE_EDITOR!" (
        echo Found UE via UE_5_5 environment variable.
        goto :found_ue
    )
)

REM Try registry (HKLM)
for /f "tokens=2*" %%a in ('reg query "HKLM\SOFTWARE\EpicGames\Unreal Engine\5.5" /v "InstalledDirectory" 2^>nul') do (
    set "UE_EDITOR=%%b\Engine\Binaries\Win64\UnrealEditor-Cmd.exe"
    if exist "!UE_EDITOR!" (
        echo Found UE via registry ^(HKLM^).
        goto :found_ue
    )
)

REM Try registry (HKCU)
for /f "tokens=2*" %%a in ('reg query "HKCU\SOFTWARE\EpicGames\Unreal Engine\5.5" /v "InstalledDirectory" 2^>nul') do (
    set "UE_EDITOR=%%b\Engine\Binaries\Win64\UnrealEditor-Cmd.exe"
    if exist "!UE_EDITOR!" (
        echo Found UE via registry ^(HKCU^).
        goto :found_ue
    )
)

REM Try common install paths
for %%p in (
    "C:\Program Files\Epic Games\UE_5.5"
    "D:\Program Files\Epic Games\UE_5.5"
    "C:\Epic Games\UE_5.5"
    "D:\Epic Games\UE_5.5"
) do (
    set "UE_EDITOR=%%~p\Engine\Binaries\Win64\UnrealEditor-Cmd.exe"
    if exist "!UE_EDITOR!" (
        echo Found UE at %%~p
        goto :found_ue
    )
)

echo ERROR: Could not find Unreal Engine 5.5 installation.
echo.
echo Set the UE_5_5 environment variable to your UE 5.5 root directory, e.g.:
echo   set UE_5_5=C:\Program Files\Epic Games\UE_5.5
echo.
exit /b 1

:found_ue

REM ── Locate the .uproject ────────────────────────────────────────────────────

set "SCRIPT_DIR=%~dp0"
set "PROJECT_DIR=%SCRIPT_DIR%.."
set "UPROJECT=%PROJECT_DIR%\ProjectMobius.uproject"

if not exist "%UPROJECT%" (
    echo ERROR: Cannot find ProjectMobius.uproject at:
    echo   %UPROJECT%
    exit /b 1
)

echo.
echo Editor:  %UE_EDITOR%
echo Project: %UPROJECT%
echo.

REM ── Run the commandlet ──────────────────────────────────────────────────────

echo Running GenerateDatasmithMaterials commandlet...
echo.

"%UE_EDITOR%" "%UPROJECT%" -run=GenerateDatasmithMaterials -unattended -nop4 -nosplash -nullrhi

set "EXIT_CODE=%ERRORLEVEL%"

echo.
if %EXIT_CODE% EQU 0 (
    echo ============================================================
    echo  SUCCESS: All Datasmith override materials generated.
    echo ============================================================
) else (
    echo ============================================================
    echo  FAILED: Commandlet returned error code %EXIT_CODE%.
    echo  Check the log output above for details.
    echo ============================================================
)

exit /b %EXIT_CODE%
