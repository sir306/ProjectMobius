<#
.SYNOPSIS
    TEST 2 of 3 - all three arguments valid (geometry + agents + B-RISK).

.DESCRIPTION
    The full positive case. All three files load and every field populates.

        -MobiusGeometry     ISO-Test-8-FireSmoke-3DView-{3D}.udatasmith
        -MobiusPedestrian   ISO-Test-8-FireSmoke-ok-no-fire.json
        -MobiusBRisk        basemodel_default.smv

    Note what the geometry path exercises: the folder name contains a SPACE ("12 RoomTest") and
    the filename contains CURLY BRACES ("{3D}"). Both are the usual reasons a caller's argument
    building breaks, so a pass here is meaningful.

    Right-click > Run with PowerShell, or run it from a prompt. No arguments needed.

.PARAMETER Editor
    Open the editor instead of a standalone game world. The files are then consumed by the first
    Play In Editor session of that editor run.

.PARAMETER DryRun
    Print the command line and the console-command equivalents without launching.

.PARAMETER NoPause
    Do not wait for Enter before closing (for use from another script).

.EXAMPLE
    .\PIE-2-AllValid.ps1

.EXAMPLE
    .\PIE-2-AllValid.ps1 -DryRun
#>
[CmdletBinding()]
param(
    [switch]   $Editor,
    [switch]   $DryRun,
    [switch]   $NoPause,
    [string]   $EnginePath,
    [string]   $DataRoot,
    [string[]] $ExtraArgs = @()
)

$here = $PSScriptRoot
if (-not $here) { $here = Split-Path -Parent $MyInvocation.MyCommand.Path }
. (Join-Path $here '_MobiusPieLaunch.ps1')

Invoke-MobiusPieLaunch `
    -ScriptDir     $here `
    -Title         'TEST 2 of 3 - ALL THREE VALID (expect everything to load)' `
    -RelGeometry   '12 RoomTest\Exported-model\ISO-Test-8-FireSmoke-3DView-{3D}.udatasmith' `
    -RelPedestrian '12 RoomTest\ISO-Revit-Simulex-Tests\ISO-Test-8-FireSmoke-ok-no-fire.json' `
    -RelBRisk      '12-room-test-v2\basemodel_default\basemodel_default.smv' `
    -Expectation   ("All three File-tab fields show their filenames.`n" +
                    "Building geometry renders; agents spawn; B-RISK room/vent wireframes appear.`n" +
                    "Log shows three 'dispatched' lines under LogMobiusPreload and no rejections.") `
    -Editor:$Editor -DryRun:$DryRun -NoPause:$NoPause `
    -EnginePath $EnginePath -DataRoot $DataRoot -ExtraArgs $ExtraArgs
