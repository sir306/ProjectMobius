<#
.SYNOPSIS
    TEST 3 of 3 - geometry + agents only, no B-RISK.

.DESCRIPTION
    Proves the three arguments are INDEPENDENT: -MobiusBRisk is simply not passed, which is not an
    error and does not affect the other two.

        -MobiusGeometry     Technical_School_R2027-3DView-{3D}.udatasmith
        -MobiusPedestrian   TechnicalSchool_1000.json

    The Smoke (B-RISK) field is expected to stay on "Click Browse to choose file" - that is the
    correct result here, not a failure.

    Right-click > Run with PowerShell, or run it from a prompt. No arguments needed.

.PARAMETER Editor
    Open the editor instead of a standalone game world. The files are then consumed by the first
    Play In Editor session of that editor run.

.PARAMETER DryRun
    Print the command line and the console-command equivalents without launching.

.PARAMETER NoPause
    Do not wait for Enter before closing (for use from another script).

.EXAMPLE
    .\PIE-3-GeometryAndAgents.ps1

.EXAMPLE
    .\PIE-3-GeometryAndAgents.ps1 -DryRun
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
    -Title         'TEST 3 of 3 - GEOMETRY + AGENTS ONLY (B-RISK deliberately omitted)' `
    -RelGeometry   'TechSchoolTest\RevitTwinmotionExpt\Technical_School_R2027-3DView-{3D}.udatasmith' `
    -RelPedestrian 'TechSchoolTest\TechnicalSchool_1000.json' `
    -Expectation   ("Geometry and Pedestrian vectors fields show their filenames.`n" +
                    "Smoke (B-RISK) stays on 'Click Browse to choose file' - expected.`n" +
                    "Log shows two 'dispatched' lines and BRisk='<none>' in the request line.") `
    -Editor:$Editor -DryRun:$DryRun -NoPause:$NoPause `
    -EnginePath $EnginePath -DataRoot $DataRoot -ExtraArgs $ExtraArgs
